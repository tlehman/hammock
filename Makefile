CC = cc
CFLAGS = -Wall -Wextra -std=c11 -g -O2 -Ilibsci
LDFLAGS = -lncurses -Llibsci -lsci -Wl,-rpath,@executable_path/libsci

SRC_DIR = src
BUILD_DIR = build
# Exclude old nrepl.c and clojure.c, use sci.c instead
SRCS = $(filter-out $(SRC_DIR)/nrepl.c $(SRC_DIR)/clojure.c, $(wildcard $(SRC_DIR)/*.c))
OBJS = $(SRCS:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o)
TARGET = hammock

$(TARGET): libsci/libsci.dylib $(OBJS)
	$(CC) $(OBJS) -o $@ $(LDFLAGS)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# Build libsci shared library (requires nix develop for GraalVM)
libsci/libsci.dylib:
	$(MAKE) -C libsci

clean:
	rm -rf $(BUILD_DIR) $(TARGET)

clean-all: clean
	$(MAKE) -C libsci clean

.PHONY: clean clean-all
