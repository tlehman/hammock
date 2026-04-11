CC = cc
# Single source of truth for the version lives in clj/core.clj.
# Extract the literal from (defn hammock-version [] "X.Y.Z") and inject it
# into every C compilation unit as HAMMOCK_VERSION.
HAMMOCK_VERSION := $(shell awk -F\" '/defn hammock-version/ {print $$2; exit}' clj/core.clj)
CFLAGS = -Wall -Wextra -std=c11 -g -O2 -Ilibsci -DHAMMOCK_VERSION='"$(HAMMOCK_VERSION)"'

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
LOGO_HEADER = $(SRC_DIR)/hammock_logo_data.h
LIBSCI = libsci/libsci.$(LIB_EXT)

$(TARGET): $(LIBSCI) $(NEWS_HEADER) $(LOGO_HEADER) $(OBJS)
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

# Embed hammock logo ASCII art into the binary as a C byte array.
# Source file: assets/hammock-logo.txt.
# To regenerate the source from logo-bw.png (wide aspect ratio so the image
# isn't vertically stretched in the terminal's 2:1 cell geometry):
#   chafa --format=symbols --colors=none --symbols=block --size=56x28 logo-bw.png > assets/hammock-logo.txt
$(LOGO_HEADER): assets/hammock-logo.txt
	cd assets && xxd -i hammock-logo.txt > ../$(LOGO_HEADER)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c $(NEWS_HEADER) $(LOGO_HEADER) clj/core.clj | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# Build libsci shared library (requires nix develop for GraalVM)
$(LIBSCI):
	$(MAKE) -C libsci

clean:
	rm -rf $(BUILD_DIR) $(TARGET) $(NEWS_HEADER) $(LOGO_HEADER)

clean-all: clean
	$(MAKE) -C libsci clean

.PHONY: clean clean-all check
