
# --- компилятор и флаги ---
CC      ?= gcc
CFLAGS  ?= -Wall -Wextra -O2
# включаем санитайзер только в Debug (опционально)
ifeq ($(DEBUG),1)
CFLAGS  += -fsanitize=address -fno-omit-frame-pointer
SAN     := -fsanitize=address
endif

# Источники/заголовки
SRC := src/kolibri_globals.c \
       src/kolibri_node_v1.c \
       src/kolibri_proto.c \
       src/kolibri_rules.c \
       src/kolibri_knowledge.c \
       src/kolibri_ping.c \
       src/kolibri_decimal_cell.c \
       src/kolibri_rule_stats.c \
       src/http_status_server.c \
       src/formula.c \
       src/kovian_blockchain.c \
       src/kolibri_ai.c \
       src/kolibri_ai_api.c

INCLUDES := -Iinclude -Isrc

# Используем pkg-config вместо хардкодных путей Homebrew
PKG_CFLAGS := $(shell pkg-config --cflags json-c libcurl libmicrohttpd 2>/dev/null)
PKG_LIBS   := $(shell pkg-config --libs   json-c libcurl libmicrohttpd 2>/dev/null)

# Если pkg-config ничего не дал (редкий случай), оставим пустые
CFLAGS += $(INCLUDES) $(PKG_CFLAGS)

# ВАЖНО: -lm обязательно, и порядок такой: [объектники] [LIBS] [SAN]
LIBS := $(PKG_LIBS) -lcrypto -lpthread -luuid -lm

BIN_DIR := bin
TARGET  := $(BIN_DIR)/kolibri_node_v1

.PHONY: all clean run

all: $(TARGET)

$(TARGET): $(SRC) | $(BIN_DIR)
	$(CC) $(CFLAGS) $(SRC) -o $@ $(LIBS) $(SAN)

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

clean:
	rm -f $(TARGET) *.o

run: $(TARGET)
	$(TARGET) --id nodeA --port 9000 --data chainA.db --root-key root.key
