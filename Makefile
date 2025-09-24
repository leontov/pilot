# Copyright (c) 2025 Кочуров Владислав Евгеньевич

CC ?= gcc

JSONC_CFLAGS := $(shell pkg-config --cflags json-c 2>/dev/null)
JSONC_LIBS := $(shell pkg-config --libs json-c 2>/dev/null)

CFLAGS ?= -std=c11 -Wall -Wextra -O2
CFLAGS += -Isrc -Iinclude -pthread
CFLAGS += $(JSONC_CFLAGS)
CFLAGS += -I/usr/include/json-c

LDFLAGS ?=
LDFLAGS += -lpthread -lm -luuid -lcrypto -lcurl
LDFLAGS += $(JSONC_LIBS)

BUILD_DIR := build/obj
BIN_DIR := bin
TARGET := $(BIN_DIR)/kolibri_node

# Сборка использует новый узел из `src/main.c`; устаревший `kolibri_node_v1.c`
# удалён, поэтому список исходников соответствует актуальным модулям.
SRC := \
    src/main.c \
    src/util/log.c \
    src/util/bench.c \
    src/util/config.c \
    src/util/json_compat.c \
    src/vm/vm.c \
    src/fkv/fkv.c \
    src/kolibri_ai.c \
    src/http/http_server.c \
    src/http/http_routes.c \
    src/http_status_server.c \
    src/blockchain.c \
    src/formula.c \
    src/formula_runtime.c \
    src/formula_stub.c \
    src/fkv/fkv.c \
    src/http/http_routes.c \
    src/http/http_server.c \
    src/kolibri_ai.c \
    src/protocol/swarm.c \
    src/synthesis/formula_vm_eval.c \
    src/synthesis/search.c \
    src/synthesis/selfplay.c \
    src/util/bench.c \
    src/util/config.c \
    src/util/json_compat.c \
    src/util/log.c \
    src/vm/vm.c \
    src/protocol/swarm.c



OBJ := $(SRC:src/%.c=$(BUILD_DIR)/%.o)

TEST_VM_SRC := tests/unit/test_vm.c src/vm/vm.c src/util/log.c src/util/config.c src/fkv/fkv.c
TEST_FKV_SRC := tests/unit/test_fkv.c src/fkv/fkv.c src/util/log.c src/util/config.c
TEST_CONFIG_SRC := tests/unit/test_config.c src/util/config.c src/util/log.c
TEST_KOLIBRI_ITER_SRC := \
  tests/test_kolibri_ai_iterations.c \
  src/kolibri_ai.c \
  src/formula_runtime.c \
  src/formula_stub.c \
  src/synthesis/search.c \
  src/synthesis/formula_vm_eval.c \
  src/vm/vm.c \
  src/fkv/fkv.c \
  src/util/log.c \
  src/util/config.c \
  src/util/json_compat.c
TEST_SWARM_PROTOCOL_SRC := tests/unit/test_swarm_protocol.c src/protocol/swarm.c src/util/log.c src/util/config.c
TEST_HTTP_ROUTES_SRC := \
  tests/unit/test_http_routes.c \
  src/http/http_routes.c \
  src/blockchain.c \
  src/fkv/fkv.c \
  src/util/log.c \
  src/util/config.c \
  src/vm/vm.c \
  src/formula_runtime.c \
  src/synthesis/formula_vm_eval.c \
  src/synthesis/search.c \
  src/kolibri_ai.c \
  src/formula_stub.c
TEST_REGRESS_SRC := tests/test_blockchain_verifier.c src/blockchain.c src/formula_runtime.c src/formula_stub.c src/util/log.c

.PHONY: all build clean run test test-vm test-fkv test-config test-kolibri-ai test-swarm-protocol test-http-routes test-regress bench

all: build

build: $(TARGET)

$(TARGET): $(OBJ) | $(BIN_DIR)
	$(CC) $(CFLAGS) $(OBJ) -o $@ $(LDFLAGS)

$(BUILD_DIR)/%.o: src/%.c | $(BUILD_DIR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BIN_DIR):
	@mkdir -p $@

$(BUILD_DIR):
	@mkdir -p $@

run: build
	$(TARGET)

clean:
	rm -rf $(BUILD_DIR) $(BIN_DIR) logs/* data/* web/node_modules web/dist

$(BUILD_DIR)/tests/unit/test_vm: $(TEST_VM_SRC)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

test-vm: $(BUILD_DIR)/tests/unit/test_vm
	$<

$(BUILD_DIR)/tests/unit/test_fkv: $(TEST_FKV_SRC)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

test-fkv: $(BUILD_DIR)/tests/unit/test_fkv
	$<

$(BUILD_DIR)/tests/unit/test_config: $(TEST_CONFIG_SRC)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

test-config: $(BUILD_DIR)/tests/unit/test_config
	$<

$(BUILD_DIR)/tests/test_kolibri_ai_iterations: $(TEST_KOLIBRI_ITER_SRC)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

test-kolibri-ai: $(BUILD_DIR)/tests/test_kolibri_ai_iterations
	$<

$(BUILD_DIR)/tests/unit/test_swarm_protocol: $(TEST_SWARM_PROTOCOL_SRC)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

test-swarm-protocol: $(BUILD_DIR)/tests/unit/test_swarm_protocol
	$<

$(BUILD_DIR)/tests/unit/test_http_routes: $(TEST_HTTP_ROUTES_SRC)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

test-http-routes: $(BUILD_DIR)/tests/unit/test_http_routes
	$<

$(BUILD_DIR)/tests/test_blockchain_verifier: $(TEST_REGRESS_SRC)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

test-regress: $(BUILD_DIR)/tests/test_blockchain_verifier
	$<

BENCH_ARGS ?=

bench: build
	$(TARGET) --bench $(BENCH_ARGS)

test: build test-vm test-fkv test-config test-kolibri-ai test-swarm-protocol test-http-routes test-regress
