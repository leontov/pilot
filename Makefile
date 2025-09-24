CC ?= gcc
CFLAGS ?= -std=c11 -Wall -Wextra -O2
CFLAGS += -Isrc -Iinclude -pthread

LDFLAGS ?=
LDFLAGS += -lpthread -lm -luuid -lcurl -lssl -lcrypto

BUILD_DIR := build/obj
BIN_DIR := bin
TARGET := $(BIN_DIR)/kolibri_node

SIGNING_KEY ?= keys/code_signing.pem
SIGNING_CERT ?= keys/code_signing.pem.pub

SRC := \
	src/main.c \
	src/util/log.c \
	src/util/config.c \
	src/util/key_manager.c \
	src/util/jwt.c \
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
TEST_SWARM_PROTOCOL_SRC := tests/unit/test_swarm_protocol.c src/protocol/swarm.c src/util/log.c

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

docker-build: build
	@if command -v docker >/dev/null 2>&1; then \
		docker build -t kolibri-node:latest . ; \
	else \
		echo "docker not available" >&2; \
		exit 1; \
	fi

sbom:
	@if command -v syft >/dev/null 2>&1; then \
		syft packages dir:. -o json > sbom.json ; \
		echo "SBOM written to sbom.json"; \
	elif command -v docker >/dev/null 2>&1; then \
		docker sbom kolibri-node:latest > sbom.txt ; \
		echo "SBOM written to sbom.txt"; \
	else \
		echo "No SBOM tooling available" >&2; \
		exit 1; \
	fi

deps-check:
	@if command -v npm >/dev/null 2>&1; then \
		(cd web && npm audit --production || true); \
	else \
		echo "npm not available for dependency audit" >&2; \
	fi

sign-binaries: build
	@if [ -f "$(SIGNING_KEY)" ]; then \
		mkdir -p build/signatures; \
		openssl dgst -sha256 -sign "$(SIGNING_KEY)" -out build/signatures/kolibri_node.sig "$(TARGET)"; \
		if [ -f "$(SIGNING_CERT)" ]; then \
			openssl dgst -sha256 -verify "$(SIGNING_CERT)" -signature build/signatures/kolibri_node.sig "$(TARGET)"; \
		fi; \
		echo "Binary signature stored in build/signatures"; \
	else \
		echo "Signing key $(SIGNING_KEY) not found" >&2; \
		exit 1; \
	fi

deploy: build
	@mkdir -p dist/kolibri
	cp "$(TARGET)" dist/kolibri/
	cp kolibri.sh dist/kolibri/
	cp cfg/kolibri.jsonc dist/kolibri/ 2>/dev/null || true
	tar -czf dist/kolibri-node.tar.gz -C dist kolibri
	echo "Deployment bundle created at dist/kolibri-node.tar.gz"

clean:
	rm -rf $(BUILD_DIR) $(BIN_DIR) logs/* data/* web/node_modules web/dist dist sbom.json sbom.txt build/signatures

.PHONY: test test-vm test-fkv test-config test-kolibri-ai test-swarm-protocol bench clean run build docker-build sbom deps-check sign-binaries deploy

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
	$(TARGET) --bench
