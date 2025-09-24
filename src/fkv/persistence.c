#include "fkv/persistence.h"

#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zlib.h>

#define WAL_MAGIC 0x464B574C /* FKWL */
#define WAL_VERSION 1
#define WAL_HEADER_SIZE (sizeof(uint32_t) + sizeof(uint16_t))
#define WAL_OP_PUT 1

#define DELTA_MAGIC 0x464B5644 /* FKVD */
#define DELTA_VERSION 1

#define BASE_SNAPSHOT_NAME "base.fkz"
#define DELTA_PREFIX "delta_"
#define DELTA_SUFFIX ".fkz"

typedef struct {
    bool enabled;
    char wal_path[PATH_MAX];
    char snapshot_dir[PATH_MAX];
    char base_snapshot_path[PATH_MAX];
    size_t snapshot_interval;
    size_t wal_ops_since_checkpoint;
    uint64_t next_delta_seq;
    FILE *wal_fp;
} fkv_persistence_state_t;

typedef struct {
    uint64_t index;
    char path[PATH_MAX];
} fkv_delta_file_t;

static fkv_persistence_state_t g_state = {0};

static uint32_t compute_crc32(const uint8_t *data, size_t len) {
    uLong crc = crc32(0L, Z_NULL, 0);
    while (len > 0) {
        uInt chunk = len > UINT_MAX ? UINT_MAX : (uInt)len;
        crc = crc32(crc, data, chunk);
        data += chunk;
        len -= chunk;
    }
    return (uint32_t)crc;
}

static int ensure_dir_recursive(const char *path) {
    if (!path || *path == '\0') {
        errno = EINVAL;
        return -1;
    }
    char buffer[PATH_MAX];
    size_t len = strlen(path);
    if (len == 0 || len >= sizeof(buffer)) {
        errno = ENAMETOOLONG;
        return -1;
    }
    memcpy(buffer, path, len);
    buffer[len] = '\0';
    if (buffer[len - 1] == '/') {
        buffer[len - 1] = '\0';
    }
    for (char *p = buffer + 1; *p; ++p) {
        if (*p == '/') {
            *p = '\0';
            if (buffer[0] != '\0') {
                if (mkdir(buffer, 0755) != 0 && errno != EEXIST) {
                    return -1;
                }
            }
            *p = '/';
        }
    }
    if (buffer[0] != '\0') {
        if (mkdir(buffer, 0755) != 0 && errno != EEXIST) {
            return -1;
        }
    }
    return 0;
}

static int ensure_parent_dir(const char *path) {
    if (!path) {
        errno = EINVAL;
        return -1;
    }
    char buffer[PATH_MAX];
    size_t len = strlen(path);
    if (len == 0 || len >= sizeof(buffer)) {
        errno = ENAMETOOLONG;
        return -1;
    }
    memcpy(buffer, path, len);
    buffer[len] = '\0';
    char *slash = strrchr(buffer, '/');
    if (!slash || slash == buffer) {
        return 0;
    }
    *slash = '\0';
    if (*buffer == '\0') {
        return 0;
    }
    return ensure_dir_recursive(buffer);
}

static int wal_write_header(FILE *fp) {
    uint32_t magic = WAL_MAGIC;
    uint16_t version = WAL_VERSION;
    if (fwrite(&magic, sizeof(magic), 1, fp) != 1) {
        return -1;
    }
    if (fwrite(&version, sizeof(version), 1, fp) != 1) {
        return -1;
    }
    return fflush(fp);
}

static int wal_read_header(FILE *fp) {
    uint32_t magic = 0;
    uint16_t version = 0;
    if (fread(&magic, sizeof(magic), 1, fp) != 1) {
        return -1;
    }
    if (fread(&version, sizeof(version), 1, fp) != 1) {
        return -1;
    }
    if (magic != WAL_MAGIC || version != WAL_VERSION) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

static int open_wal(void) {
    if (!g_state.enabled) {
        return 0;
    }
    FILE *fp = fopen(g_state.wal_path, "r+b");
    if (!fp) {
        if (errno != ENOENT) {
            return -1;
        }
        fp = fopen(g_state.wal_path, "w+b");
        if (!fp) {
            return -1;
        }
        if (wal_write_header(fp) != 0) {
            fclose(fp);
            return -1;
        }
    } else {
        if (fseek(fp, 0, SEEK_END) != 0) {
            fclose(fp);
            return -1;
        }
        long size = ftell(fp);
        if (size < (long)WAL_HEADER_SIZE) {
            if (fseek(fp, 0, SEEK_SET) != 0) {
                fclose(fp);
                return -1;
            }
            if (wal_write_header(fp) != 0) {
                fclose(fp);
                return -1;
            }
        }
    }
    if (fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return -1;
    }
    if (wal_read_header(fp) != 0) {
        fclose(fp);
        return -1;
    }
    g_state.wal_fp = fp;
    return 0;
}

static int wal_read_payload(uint8_t **buffer, size_t *len) {
    if (!g_state.wal_fp || !buffer || !len) {
        errno = EINVAL;
        return -1;
    }
    if (fflush(g_state.wal_fp) != 0) {
        return -1;
    }
    if (fseek(g_state.wal_fp, 0, SEEK_SET) != 0) {
        return -1;
    }
    if (wal_read_header(g_state.wal_fp) != 0) {
        return -1;
    }
    if (fseek(g_state.wal_fp, 0, SEEK_END) != 0) {
        return -1;
    }
    long end = ftell(g_state.wal_fp);
    if (end < (long)WAL_HEADER_SIZE) {
        errno = EINVAL;
        return -1;
    }
    size_t payload_size = (size_t)((unsigned long)end - WAL_HEADER_SIZE);
    if (payload_size == 0) {
        *buffer = NULL;
        *len = 0;
        return fseek(g_state.wal_fp, 0, SEEK_END);
    }
    if (fseek(g_state.wal_fp, WAL_HEADER_SIZE, SEEK_SET) != 0) {
        return -1;
    }
    uint8_t *buf = malloc(payload_size);
    if (!buf) {
        return -1;
    }
    if (fread(buf, 1, payload_size, g_state.wal_fp) != payload_size) {
        free(buf);
        return -1;
    }
    if (fseek(g_state.wal_fp, 0, SEEK_END) != 0) {
        free(buf);
        return -1;
    }
    *buffer = buf;
    *len = payload_size;
    return 0;
}

static int iterate_log_buffer(const uint8_t *buffer,
                              size_t len,
                              fkv_persistence_apply_fn apply,
                              void *userdata,
                              size_t *count_out) {
    if (!buffer && len != 0) {
        errno = EINVAL;
        return -1;
    }
    size_t offset = 0;
    size_t count = 0;
    while (offset < len) {
        if (len - offset < 1 + 1 + sizeof(uint64_t) + sizeof(uint64_t)) {
            errno = EINVAL;
            return -1;
        }
        uint8_t op = buffer[offset++];
        if (op != WAL_OP_PUT) {
            errno = EINVAL;
            return -1;
        }
        uint8_t type = buffer[offset++];
        uint64_t key_len = 0;
        memcpy(&key_len, buffer + offset, sizeof(uint64_t));
        offset += sizeof(uint64_t);
        if (key_len > len - offset) {
            errno = EINVAL;
            return -1;
        }
        const uint8_t *key_ptr = key_len ? buffer + offset : NULL;
        offset += (size_t)key_len;
        if (len - offset < sizeof(uint64_t)) {
            errno = EINVAL;
            return -1;
        }
        uint64_t value_len = 0;
        memcpy(&value_len, buffer + offset, sizeof(uint64_t));
        offset += sizeof(uint64_t);
        if (value_len > len - offset) {
            errno = EINVAL;
            return -1;
        }
        const uint8_t *value_ptr = value_len ? buffer + offset : NULL;
        offset += (size_t)value_len;
        if (apply) {
            if (apply(key_ptr,
                      (size_t)key_len,
                      value_ptr,
                      (size_t)value_len,
                      (fkv_entry_type_t)type,
                      userdata) != 0) {
                return -1;
            }
        }
        ++count;
    }
    if (count_out) {
        *count_out = count;
    }
    return 0;
}

static int delta_file_cmp(const void *a, const void *b) {
    const fkv_delta_file_t *da = (const fkv_delta_file_t *)a;
    const fkv_delta_file_t *db = (const fkv_delta_file_t *)b;
    if (da->index < db->index) {
        return -1;
    }
    if (da->index > db->index) {
        return 1;
    }
    return strcmp(da->path, db->path);
}

static int collect_delta_files(fkv_delta_file_t **files_out, size_t *count_out) {
    if (!files_out || !count_out) {
        errno = EINVAL;
        return -1;
    }
    *files_out = NULL;
    *count_out = 0;
    DIR *dir = opendir(g_state.snapshot_dir);
    if (!dir) {
        if (errno == ENOENT) {
            g_state.next_delta_seq = 0;
            return 0;
        }
        return -1;
    }
    size_t capacity = 8;
    size_t count = 0;
    fkv_delta_file_t *files = malloc(capacity * sizeof(*files));
    if (!files) {
        closedir(dir);
        return -1;
    }
    struct dirent *entry;
    size_t prefix_len = strlen(DELTA_PREFIX);
    size_t suffix_len = strlen(DELTA_SUFFIX);
    while ((entry = readdir(dir)) != NULL) {
        const char *name = entry->d_name;
        size_t name_len = strlen(name);
        if (name_len <= prefix_len + suffix_len) {
            continue;
        }
        if (strncmp(name, DELTA_PREFIX, prefix_len) != 0) {
            continue;
        }
        if (strcmp(name + name_len - suffix_len, DELTA_SUFFIX) != 0) {
            continue;
        }
        size_t digits_len = name_len - prefix_len - suffix_len;
        if (digits_len == 0 || digits_len >= 32) {
            continue;
        }
        char number_buf[32];
        memcpy(number_buf, name + prefix_len, digits_len);
        number_buf[digits_len] = '\0';
        char *endptr = NULL;
        errno = 0;
        unsigned long long idx = strtoull(number_buf, &endptr, 10);
        if (errno != 0 || !endptr || *endptr != '\0') {
            continue;
        }
        if (count == capacity) {
            size_t new_capacity = capacity * 2;
            fkv_delta_file_t *tmp = realloc(files, new_capacity * sizeof(*files));
            if (!tmp) {
                free(files);
                closedir(dir);
                return -1;
            }
            files = tmp;
            capacity = new_capacity;
        }
        files[count].index = (uint64_t)idx;
        if (snprintf(files[count].path,
                     sizeof(files[count].path),
                     "%s/%s",
                     g_state.snapshot_dir,
                     name) >= (int)sizeof(files[count].path)) {
            free(files);
            closedir(dir);
            errno = ENAMETOOLONG;
            return -1;
        }
        ++count;
    }
    closedir(dir);
    if (count > 1) {
        qsort(files, count, sizeof(*files), delta_file_cmp);
    }
    if (count > 0) {
        g_state.next_delta_seq = files[count - 1].index + 1;
    } else {
        g_state.next_delta_seq = 0;
    }
    *files_out = files;
    *count_out = count;
    return 0;
}

static int apply_log_buffer(const uint8_t *buffer,
                            size_t len,
                            fkv_persistence_apply_fn apply,
                            void *userdata,
                            size_t *applied) {
    return iterate_log_buffer(buffer, len, apply, userdata, applied);
}

static int apply_delta_file(const char *path, fkv_persistence_apply_fn apply, void *userdata) {
    FILE *fp = fopen(path, "rb");
    if (!fp) {
        return -1;
    }
    int rc = -1;
    uint32_t magic = 0;
    uint16_t version = 0;
    uint64_t raw_size = 0;
    uint64_t record_count = 0;
    uint32_t crc32_expected = 0;
    uint64_t compressed_size = 0;
    if (fread(&magic, sizeof(magic), 1, fp) != 1 || magic != DELTA_MAGIC) {
        goto out;
    }
    if (fread(&version, sizeof(version), 1, fp) != 1 || version != DELTA_VERSION) {
        goto out;
    }
    if (fread(&raw_size, sizeof(raw_size), 1, fp) != 1) {
        goto out;
    }
    if (fread(&record_count, sizeof(record_count), 1, fp) != 1) {
        goto out;
    }
    if (fread(&crc32_expected, sizeof(crc32_expected), 1, fp) != 1) {
        goto out;
    }
    if (fread(&compressed_size, sizeof(compressed_size), 1, fp) != 1) {
        goto out;
    }
    uint8_t *compressed = NULL;
    if (compressed_size > 0) {
        compressed = malloc((size_t)compressed_size);
        if (!compressed) {
            goto out;
        }
        if (fread(compressed, 1, (size_t)compressed_size, fp) != (size_t)compressed_size) {
            free(compressed);
            goto out;
        }
    }
    fclose(fp);
    fp = NULL;
    uint8_t *raw = NULL;
    if (raw_size > 0) {
        raw = malloc((size_t)raw_size);
        if (!raw) {
            free(compressed);
            return -1;
        }
        uLongf dest_len = (uLongf)raw_size;
        if (uncompress(raw, &dest_len, compressed, (uLongf)compressed_size) != Z_OK || dest_len != raw_size) {
            free(raw);
            free(compressed);
            return -1;
        }
    }
    free(compressed);
    uint32_t crc32_actual = compute_crc32(raw, (size_t)raw_size);
    if (crc32_actual != crc32_expected) {
        free(raw);
        errno = EINVAL;
        return -1;
    }
    size_t applied = 0;
    if (raw_size > 0) {
        if (apply_log_buffer(raw, (size_t)raw_size, apply, userdata, &applied) != 0) {
            free(raw);
            return -1;
        }
    }
    free(raw);
    if (applied != record_count) {
        if (!(applied == 0 && record_count == 0)) {
            errno = EINVAL;
            return -1;
        }
    }
    rc = 0;
out:
    if (fp) {
        fclose(fp);
    }
    return rc;
}

static int apply_delta_files(const fkv_delta_file_t *files,
                             size_t count,
                             fkv_persistence_apply_fn apply,
                             void *userdata) {
    for (size_t i = 0; i < count; ++i) {
        if (apply_delta_file(files[i].path, apply, userdata) != 0) {
            return -1;
        }
    }
    return 0;
}

static int load_base_snapshot(fkv_persistence_apply_fn apply, void *userdata) {
    if (g_state.base_snapshot_path[0] == '\0') {
        return 0;
    }
    gzFile fp = gzopen(g_state.base_snapshot_path, "rb");
    if (!fp) {
        if (errno == ENOENT) {
            return 0;
        }
        return -1;
    }
    uint64_t entry_count = 0;
    if (gzread(fp, &entry_count, sizeof(entry_count)) != (int)sizeof(entry_count)) {
        gzclose(fp);
        return -1;
    }
    for (uint64_t i = 0; i < entry_count; ++i) {
        uint64_t key_len = 0;
        uint64_t value_len = 0;
        if (gzread(fp, &key_len, sizeof(key_len)) != (int)sizeof(key_len)) {
            gzclose(fp);
            return -1;
        }
        uint8_t *key = NULL;
        if (key_len > 0) {
            key = malloc((size_t)key_len);
            if (!key) {
                gzclose(fp);
                return -1;
            }
            if (gzread(fp, key, (unsigned int)key_len) != (int)key_len) {
                free(key);
                gzclose(fp);
                return -1;
            }
        }
        if (gzread(fp, &value_len, sizeof(value_len)) != (int)sizeof(value_len)) {
            free(key);
            gzclose(fp);
            return -1;
        }
        uint8_t *value = NULL;
        if (value_len > 0) {
            value = malloc((size_t)value_len);
            if (!value) {
                free(key);
                gzclose(fp);
                return -1;
            }
            if (gzread(fp, value, (unsigned int)value_len) != (int)value_len) {
                free(key);
                free(value);
                gzclose(fp);
                return -1;
            }
        }
        unsigned char type_byte = 0;
        if (gzread(fp, &type_byte, sizeof(type_byte)) != (int)sizeof(type_byte)) {
            free(key);
            free(value);
            gzclose(fp);
            return -1;
        }
        if (apply(key,
                  (size_t)key_len,
                  value,
                  (size_t)value_len,
                  (fkv_entry_type_t)type_byte,
                  userdata) != 0) {
            free(key);
            free(value);
            gzclose(fp);
            return -1;
        }
        free(key);
        free(value);
    }
    gzclose(fp);
    return 0;
}

static int reset_wal(void) {
    if (g_state.wal_fp) {
        fclose(g_state.wal_fp);
        g_state.wal_fp = NULL;
    }
    FILE *fp = fopen(g_state.wal_path, "w+b");
    if (!fp) {
        return -1;
    }
    if (wal_write_header(fp) != 0) {
        fclose(fp);
        return -1;
    }
    fclose(fp);
    return open_wal();
}

int fkv_persistence_force_checkpoint(void) {
    if (!g_state.enabled || !g_state.wal_fp) {
        return 0;
    }
    uint8_t *payload = NULL;
    size_t payload_len = 0;
    if (wal_read_payload(&payload, &payload_len) != 0) {
        free(payload);
        return -1;
    }
    if (payload_len == 0) {
        free(payload);
        g_state.wal_ops_since_checkpoint = 0;
        return 0;
    }
    size_t record_count = 0;
    if (iterate_log_buffer(payload, payload_len, NULL, NULL, &record_count) != 0) {
        free(payload);
        return -1;
    }
    if (record_count == 0) {
        free(payload);
        g_state.wal_ops_since_checkpoint = 0;
        return 0;
    }
    uint32_t crc = compute_crc32(payload, payload_len);
    uLongf compressed_capacity = compressBound(payload_len);
    uint8_t *compressed = malloc(compressed_capacity);
    if (!compressed) {
        free(payload);
        return -1;
    }
    uLongf compressed_len = compressed_capacity;
    if (compress2(compressed, &compressed_len, payload, payload_len, Z_BEST_SPEED) != Z_OK) {
        free(payload);
        free(compressed);
        return -1;
    }
    if (ensure_dir_recursive(g_state.snapshot_dir) != 0) {
        free(payload);
        free(compressed);
        return -1;
    }
    char delta_path[PATH_MAX];
    if (snprintf(delta_path,
                 sizeof(delta_path),
                 "%s/%s%012llu%s",
                 g_state.snapshot_dir,
                 DELTA_PREFIX,
                 (unsigned long long)g_state.next_delta_seq,
                 DELTA_SUFFIX) >= (int)sizeof(delta_path)) {
        free(payload);
        free(compressed);
        errno = ENAMETOOLONG;
        return -1;
    }
    FILE *dfp = fopen(delta_path, "wb");
    if (!dfp) {
        free(payload);
        free(compressed);
        return -1;
    }
    uint32_t magic = DELTA_MAGIC;
    uint16_t version = DELTA_VERSION;
    uint64_t raw_size = payload_len;
    uint64_t record_count64 = record_count;
    uint64_t compressed_size = (uint64_t)compressed_len;
    int rc = 0;
    if (fwrite(&magic, sizeof(magic), 1, dfp) != 1 ||
        fwrite(&version, sizeof(version), 1, dfp) != 1 ||
        fwrite(&raw_size, sizeof(raw_size), 1, dfp) != 1 ||
        fwrite(&record_count64, sizeof(record_count64), 1, dfp) != 1 ||
        fwrite(&crc, sizeof(crc), 1, dfp) != 1 ||
        fwrite(&compressed_size, sizeof(compressed_size), 1, dfp) != 1) {
        rc = -1;
    }
    if (rc == 0 && compressed_len > 0) {
        if (fwrite(compressed, 1, (size_t)compressed_len, dfp) != (size_t)compressed_len) {
            rc = -1;
        }
    }
    if (fflush(dfp) != 0) {
        rc = -1;
    }
    if (fclose(dfp) != 0) {
        rc = -1;
    }
    free(compressed);
    if (rc != 0) {
        free(payload);
        unlink(delta_path);
        return -1;
    }
    g_state.next_delta_seq += 1;
    free(payload);
    if (reset_wal() != 0) {
        return -1;
    }
    g_state.wal_ops_since_checkpoint = 0;
    return 0;
}
static int apply_wal(fkv_persistence_apply_fn apply, void *userdata) {
    uint8_t *payload = NULL;
    size_t payload_len = 0;
    if (wal_read_payload(&payload, &payload_len) != 0) {
        free(payload);
        return -1;
    }
    size_t applied = 0;
    int rc = 0;
    if (payload_len > 0) {
        rc = apply_log_buffer(payload, payload_len, apply, userdata, &applied);
    }
    free(payload);
    if (rc == 0) {
        g_state.wal_ops_since_checkpoint = applied;
    }
    return rc;
}

int fkv_persistence_configure(const fkv_persistence_config_t *config) {
    if (!config || !config->wal_path || !config->snapshot_dir) {
        errno = EINVAL;
        return -1;
    }
    fkv_persistence_disable();
    g_state.enabled = true;
    size_t wal_len = strlen(config->wal_path);
    if (wal_len == 0 || wal_len >= sizeof(g_state.wal_path)) {
        fkv_persistence_disable();
        errno = ENAMETOOLONG;
        return -1;
    }
    memcpy(g_state.wal_path, config->wal_path, wal_len);
    g_state.wal_path[wal_len] = '\0';
    size_t dir_len = strlen(config->snapshot_dir);
    if (dir_len == 0 || dir_len >= sizeof(g_state.snapshot_dir)) {
        fkv_persistence_disable();
        errno = ENAMETOOLONG;
        return -1;
    }
    memcpy(g_state.snapshot_dir, config->snapshot_dir, dir_len);
    g_state.snapshot_dir[dir_len] = '\0';
    if (snprintf(g_state.base_snapshot_path,
                 sizeof(g_state.base_snapshot_path),
                 "%s/%s",
                 g_state.snapshot_dir,
                 BASE_SNAPSHOT_NAME) >= (int)sizeof(g_state.base_snapshot_path)) {
        fkv_persistence_disable();
        errno = ENAMETOOLONG;
        return -1;
    }
    g_state.snapshot_interval = config->snapshot_interval ? config->snapshot_interval : 64;
    g_state.wal_ops_since_checkpoint = 0;
    g_state.next_delta_seq = 0;
    return 0;
}

void fkv_persistence_disable(void) {
    if (g_state.wal_fp) {
        fclose(g_state.wal_fp);
    }
    memset(&g_state, 0, sizeof(g_state));
}

bool fkv_persistence_enabled(void) {
    return g_state.enabled;
}

int fkv_persistence_start(fkv_persistence_apply_fn apply, void *userdata) {
    if (!g_state.enabled) {
        return 0;
    }
    if (!apply) {
        errno = EINVAL;
        return -1;
    }
    if (ensure_parent_dir(g_state.wal_path) != 0) {
        return -1;
    }
    if (ensure_dir_recursive(g_state.snapshot_dir) != 0) {
        return -1;
    }
    fkv_delta_file_t *delta_files = NULL;
    size_t delta_count = 0;
    if (collect_delta_files(&delta_files, &delta_count) != 0) {
        return -1;
    }
    if (open_wal() != 0) {
        free(delta_files);
        return -1;
    }
    if (load_base_snapshot(apply, userdata) != 0) {
        free(delta_files);
        fkv_persistence_shutdown();
        return -1;
    }
    if (apply_delta_files(delta_files, delta_count, apply, userdata) != 0) {
        free(delta_files);
        fkv_persistence_shutdown();
        return -1;
    }
    free(delta_files);
    if (apply_wal(apply, userdata) != 0) {
        fkv_persistence_shutdown();
        return -1;
    }
    if (fseek(g_state.wal_fp, 0, SEEK_END) != 0) {
        fkv_persistence_shutdown();
        return -1;
    }
    return 0;
}

int fkv_persistence_record_put(const uint8_t *key,
                               size_t key_len,
                               const uint8_t *value,
                               size_t value_len,
                               fkv_entry_type_t type) {
    if (!g_state.enabled) {
        return 0;
    }
    if (!g_state.wal_fp && open_wal() != 0) {
        return -1;
    }
    if (fseek(g_state.wal_fp, 0, SEEK_END) != 0) {
        return -1;
    }
    uint8_t opcode = WAL_OP_PUT;
    uint8_t type_byte = (uint8_t)type;
    uint64_t klen = key_len;
    uint64_t vlen = value_len;
    if (fwrite(&opcode, 1, 1, g_state.wal_fp) != 1 ||
        fwrite(&type_byte, 1, 1, g_state.wal_fp) != 1 ||
        fwrite(&klen, sizeof(klen), 1, g_state.wal_fp) != 1) {
        return -1;
    }
    if (klen > 0 && fwrite(key, 1, (size_t)klen, g_state.wal_fp) != (size_t)klen) {
        return -1;
    }
    if (fwrite(&vlen, sizeof(vlen), 1, g_state.wal_fp) != 1) {
        return -1;
    }
    if (vlen > 0 && fwrite(value, 1, (size_t)vlen, g_state.wal_fp) != (size_t)vlen) {
        return -1;
    }
    if (fflush(g_state.wal_fp) != 0) {
        return -1;
    }
    g_state.wal_ops_since_checkpoint += 1;
    if (g_state.snapshot_interval > 0 &&
        g_state.wal_ops_since_checkpoint >= g_state.snapshot_interval) {
        return fkv_persistence_force_checkpoint();
    }
    return 0;
}

void fkv_persistence_shutdown(void) {
    if (g_state.wal_fp) {
        fflush(g_state.wal_fp);
        fclose(g_state.wal_fp);
        g_state.wal_fp = NULL;
    }
}

const char *fkv_persistence_wal_path(void) {
    return g_state.enabled && g_state.wal_path[0] ? g_state.wal_path : NULL;
}

const char *fkv_persistence_base_snapshot_path(void) {
    return g_state.enabled && g_state.base_snapshot_path[0] ? g_state.base_snapshot_path : NULL;
}
