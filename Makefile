CC = cc
CFLAGS = -Wall -Wextra -std=c11 -g -O2 -Ilibsci

UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
  LIB_EXT = dylib
  RPATH   = -Wl,-rpath,@executable_path/libsci
else
  LIB_EXT = so
  RPATH   = -Wl,-rpath,$$ORIGIN/libsci
endif

LDFLAGS = -lncurses -Llibsci -lsci $(RPATH)

SRC_DIR = src
BUILD_DIR = build
# Exclude old nrepl.c and clojure.c, use sci.c instead
SRCS = $(filter-out $(SRC_DIR)/nrepl.c $(SRC_DIR)/clojure.c, $(wildcard $(SRC_DIR)/*.c))
OBJS = $(SRCS:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o)
TARGET = hammock
NEWS_HEADER = $(SRC_DIR)/news_data.h
LIBSCI = libsci/libsci.$(LIB_EXT)

$(TARGET): $(LIBSCI) $(NEWS_HEADER) $(OBJS)
	$(CC) $(OBJS) -o $@ $(LDFLAGS)

# Non-interactive smoke test: links against libsci and runs the Clojure
# load sequence plus a handful of probes. Run via `make check`.
SMOKE_SRC = test/smoke.c
SMOKE_BIN = $(BUILD_DIR)/smoke

$(SMOKE_BIN): $(SMOKE_SRC) $(LIBSCI) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(SMOKE_SRC) -o $(SMOKE_BIN) $(LDFLAGS)

check: $(SMOKE_BIN)
	./$(SMOKE_BIN)

# Embed NEWS.md into the binary as a C byte array
$(NEWS_HEADER): NEWS.md
	xxd -i NEWS.md > $(NEWS_HEADER)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c $(NEWS_HEADER) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# Build libsci shared library (requires nix develop for GraalVM)
$(LIBSCI):
	$(MAKE) -C libsci

clean:
	rm -rf $(BUILD_DIR) $(TARGET) $(NEWS_HEADER)

clean-all: clean
	$(MAKE) -C libsci clean

.PHONY: clean clean-all check
