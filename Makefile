CC ?= gcc
CFLAGS := -std=c11 -Wall -Wextra -O2 -Isrc -Iinclude -pthread

LDFLAGS := -lpthread -ljson-c

BUILD_DIR := build/obj
BIN_DIR := bin
TARGET := $(BIN_DIR)/kolibri_node

SRC := \
  src/main.c \
  src/util/log.c \
  src/util/config.c \
  src/vm/vm.c \
  src/fkv/fkv.c \
  src/formula_runtime.c \
  src/synthesis/selfplay.c \
  src/kolibri_ai.c \
  src/http/http_server.c \
  src/http/http_routes.c \
  src/blockchain.c \
  src/formula_stub.c

TEST_VM_SRC := tests/unit/test_vm.c src/vm/vm.c src/util/log.c src/util/config.c src/fkv/fkv.c
TEST_FKV_SRC := tests/unit/test_fkv.c src/fkv/fkv.c src/util/log.c src/util/config.c
TEST_CONFIG_SRC := tests/unit/test_config.c src/util/config.c src/util/log.c


OBJ := $(SRC:src/%.c=$(BUILD_DIR)/%.o)

all: build

$(BUILD_DIR)/%.o: src/%.c | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

build: $(TARGET)

$(TARGET): $(OBJ) | $(BIN_DIR)
	$(CC) $(CFLAGS) $(OBJ) -o $@ $(LDFLAGS)

$(BUILD_DIR):
	@mkdir -p $@

$(BIN_DIR):
	@mkdir -p $@

run: build
	$(TARGET)

clean:
	rm -rf $(BUILD_DIR) $(BIN_DIR) logs/* data/* web/node_modules web/dist

.PHONY: test test-vm test-fkv test-http-routes bench clean run build



test: build test-vm test-fkv test-config


$(BUILD_DIR)/tests/unit/test_vm: $(TEST_VM_SRC)
	@mkdir -p $(BUILD_DIR)/tests/unit
	$(CC) $(CFLAGS) $(TEST_VM_SRC) -o $@ $(LDFLAGS)

$(BUILD_DIR)/tests/unit/test_fkv: $(TEST_FKV_SRC)
	@mkdir -p $(BUILD_DIR)/tests/unit
	$(CC) $(CFLAGS) $(TEST_FKV_SRC) -o $@ $(LDFLAGS)

test-vm: $(BUILD_DIR)/tests/unit/test_vm
	$<

test-fkv: $(BUILD_DIR)/tests/unit/test_fkv
	$<


$(BUILD_DIR)/tests/unit/test_config: $(TEST_CONFIG_SRC)
	@mkdir -p $(BUILD_DIR)/tests/unit
	$(CC) $(CFLAGS) $(TEST_CONFIG_SRC) -o $@ $(LDFLAGS)

test-config: $(BUILD_DIR)/tests/unit/test_config

	$<

bench: build
	$(TARGET) --bench
