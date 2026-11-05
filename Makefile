CC ?= cc
AR ?= ar
CPPFLAGS = -D_XOPEN_SOURCE=700 -D_DARWIN_C_SOURCE -Isrc
CFLAGS = -std=c99 -Wall -Wextra -Werror -Wpedantic -O3
LDLIBS = -lpthread

BUILD_DIR = build
LIB_ARCHIVE = $(BUILD_DIR)/liboyez.a
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

.PHONY: all lib test sanitize bench clean

all: $(LIB_ARCHIVE) $(BUILD_DIR)/oyez $(BUILD_DIR)/test_oyez

lib: $(LIB_ARCHIVE)

$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)

$(BUILD_DIR)/%.o: src/%.c $(LIB_HDRS) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(LIB_ARCHIVE): $(LIB_OBJS) | $(BUILD_DIR)
	rm -f $@
	$(AR) rcs $@ $(LIB_OBJS)

$(BUILD_DIR)/oyez: $(LIB_ARCHIVE) src/main.c $(LIB_HDRS) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) src/main.c $(LIB_ARCHIVE) -o $@ $(LDLIBS)

$(BUILD_DIR)/test_oyez: $(LIB_ARCHIVE) tests/test_oyez.c $(LIB_HDRS) | $(BUILD_DIR)
	$(CC) $(CPPFLAGS) $(CFLAGS) tests/test_oyez.c $(LIB_ARCHIVE) -o $@ $(LDLIBS)

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
