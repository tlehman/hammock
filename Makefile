CC = cc
CFLAGS = -Wall -Wextra -std=c11 -g -O2 -Ilibsci
LDFLAGS = -lncurses -Llibsci -lsci -Wl,-rpath,@executable_path/libsci

SRC_DIR = src
BUILD_DIR = build
# Exclude old nrepl.c and clojure.c, use sci.c instead
SRCS = $(filter-out $(SRC_DIR)/nrepl.c $(SRC_DIR)/clojure.c, $(wildcard $(SRC_DIR)/*.c))
OBJS = $(SRCS:$(SRC_DIR)/%.c=$(BUILD_DIR)/%.o)
TARGET = hammock
NEWS_HEADER = $(SRC_DIR)/news_data.h

$(TARGET): libsci/libsci.dylib $(NEWS_HEADER) $(OBJS)
	$(CC) $(OBJS) -o $@ $(LDFLAGS)

# Embed NEWS.md into the binary as a C byte array
$(NEWS_HEADER): NEWS.md
	xxd -i NEWS.md > $(NEWS_HEADER)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c $(NEWS_HEADER) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# Build libsci shared library (requires nix develop for GraalVM)
libsci/libsci.dylib:
	$(MAKE) -C libsci

clean:
	rm -rf $(BUILD_DIR) $(TARGET) $(NEWS_HEADER)

clean-all: clean
	$(MAKE) -C libsci clean

.PHONY: clean clean-all
