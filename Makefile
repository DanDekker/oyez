CC ?= cc
CPPFLAGS = -D_XOPEN_SOURCE=700 -D_DARWIN_C_SOURCE -Isrc
CFLAGS = -std=c99 -Wall -Wextra -Werror -Wpedantic -O3
LDLIBS = -lpthread

BUILD_DIR = build
LIB_SRCS = \
	src/core.c \
	src/checksum.c \
	src/token.c \
	src/m1_tokenizer.c \
	src/markup.c \
	src/ontology.c \
	src/catalog.c \
	src/index_engine.c \
	src/url_fetch.c \
	src/http_server.c

LIB_HDRS = $(wildcard src/*.h)
LIB_OBJS = $(patsubst src/%.c,$(BUILD_DIR)/%.o,$(LIB_SRCS))

.PHONY: all test sanitize bench clean

all: $(BUILD_DIR)/oyez $(BUILD_DIR)/test_oyez

$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/%.o: src/%.c $(LIB_HDRS) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/oyez: $(LIB_OBJS) src/main.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LIB_OBJS) src/main.c -o $@ $(LDLIBS)

$(BUILD_DIR)/test_oyez: $(LIB_OBJS) tests/test_oyez.c | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(LIB_OBJS) tests/test_oyez.c -o $@ $(LDLIBS)

test: $(BUILD_DIR)/test_oyez
	./$(BUILD_DIR)/test_oyez

sanitize:
	$(MAKE) BUILD_DIR=$(BUILD_DIR)/sanitize CC=clang \
		CFLAGS='-std=c99 -Wall -Wextra -Werror -Wpedantic -O1 -g -fsanitize=address,undefined -fno-omit-frame-pointer' all
	UBSAN_OPTIONS=halt_on_error=1 ./$(BUILD_DIR)/sanitize/test_oyez

bench: $(BUILD_DIR)/oyez
	./$(BUILD_DIR)/oyez bench

clean:
	rm -rf $(BUILD_DIR)
