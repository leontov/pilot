# Copyright (c) 2024 Кочуров Владислав Евгеньевич

CC ?= gcc
CFLAGS := -std=c11 -Wall -Wextra -O2 -Isrc -Iinclude -I/usr/include/json-c -pthread


LDFLAGS := -lpthread -lm -luuid -lcrypto -lcurl


BUILD_DIR := build/obj
BIN_DIR := bin
TARGET := $(BIN_DIR)/kolibri_node

SRC := \
  src/main.c \
  src/util/log.c \
  src/util/bench.c \
  src/util/config.c \
  src/vm/vm.c \
  src/fkv/fkv.c \
  src/kolibri_ai.c \
  src/http/http_server.c \
  src/http/http_routes.c \
  src/blockchain.c \
  src/formula_runtime.c \
  src/synthesis/search.c \
  src/synthesis/formula_vm_eval.c \
  src/formula_stub.c \
  src/protocol/swarm.c


TEST_VM_SRC := tests/unit/test_vm.c src/vm/vm.c src/util/log.c src/util/config.c src/fkv/fkv.c
TEST_FKV_SRC := tests/unit/test_fkv.c src/fkv/fkv.c src/util/log.c src/util/config.c
TEST_CONFIG_SRC := tests/unit/test_config.c src/util/config.c src/util/log.c

TEST_KOLIBRI_ITER_SRC := tests/test_kolibri_ai_iterations.c src/kolibri_ai.c src/formula_runtime.c src/synthesis/search.c src/synthesis/formula_vm_eval.c src/vm/vm.c src/fkv/fkv.c
TEST_SWARM_PROTOCOL_SRC := tests/unit/test_swarm_protocol.c src/protocol/swarm.c



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

.PHONY: test test-vm test-fkv test-config test-kolibri-ai test-http-routes bench clean run build



test: build test-vm test-fkv test-config test-kolibri-ai test-swarm-protocol


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

$(BUILD_DIR)/tests/test_kolibri_ai_iterations: $(TEST_KOLIBRI_ITER_SRC)
	@mkdir -p $(BUILD_DIR)/tests
	$(CC) $(CFLAGS) $(TEST_KOLIBRI_ITER_SRC) -o $@ $(LDFLAGS)

test-kolibri-ai: $(BUILD_DIR)/tests/test_kolibri_ai_iterations
	$<

$(BUILD_DIR)/tests/unit/test_swarm_protocol: $(TEST_SWARM_PROTOCOL_SRC)
	@mkdir -p $(BUILD_DIR)/tests/unit
	$(CC) $(CFLAGS) $(TEST_SWARM_PROTOCOL_SRC) -o $@ $(filter-out -ljson-c -luuid,$(LDFLAGS))

test-swarm-protocol: $(BUILD_DIR)/tests/unit/test_swarm_protocol
	$<

bench: build
	$(TARGET) --bench $(BENCH_ARGS)
