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

# ---- Perf harness ----

PERF_DIR      = perf
PERF_FIXTURES = $(PERF_DIR)/fixtures
PERF_SCRIPTS  = $(PERF_DIR)/scripts
PERF_BASELINE_DIR = $(PERF_DIR)/baselines
HOST_SHORT    = $(shell hostname -s)
PTY_BENCH_BIN = $(BUILD_DIR)/pty_bench
PTY_BENCH_SRC = test/pty_bench.c

$(PERF_FIXTURES)/small.txt $(PERF_FIXTURES)/medium.txt $(PERF_FIXTURES)/large.txt &: | $(PERF_FIXTURES)
	@cat clj/*.clj > $(PERF_FIXTURES)/_base.txt
	@head -n 100 $(PERF_FIXTURES)/_base.txt > $(PERF_FIXTURES)/small.txt
	@awk 'BEGIN{for(i=0;i<25;i++)for(j=0;j<400;j++)print "(def perf-line-"j" :padding-"i")"}' > $(PERF_FIXTURES)/medium.txt
	@awk 'BEGIN{for(i=0;i<250;i++)for(j=0;j<400;j++)print "(def perf-line-"j" :padding-"i")"}' > $(PERF_FIXTURES)/large.txt
	@rm -f $(PERF_FIXTURES)/_base.txt
	@wc -l $(PERF_FIXTURES)/small.txt $(PERF_FIXTURES)/medium.txt $(PERF_FIXTURES)/large.txt

$(PERF_FIXTURES):
	mkdir -p $(PERF_FIXTURES)

perf-fixtures: $(PERF_FIXTURES)/small.txt $(PERF_FIXTURES)/medium.txt $(PERF_FIXTURES)/large.txt

perf-run: $(TARGET) $(PERF_FIXTURES)/medium.txt
	./$(TARGET) --bench $(PERF_SCRIPTS)/keystroke-latency.edn
	./$(TARGET) --bench $(PERF_SCRIPTS)/cursor-macro.edn
	./$(TARGET) --bench $(PERF_SCRIPTS)/dispatch-mix.edn

perf-baseline: $(TARGET) $(PERF_FIXTURES)/medium.txt
	@mkdir -p $(PERF_BASELINE_DIR)
	./$(TARGET) --bench $(PERF_SCRIPTS)/keystroke-latency.edn
	@cp $$(ls -t $(PERF_DIR)/runs/*.edn | head -1) \
	    $(PERF_BASELINE_DIR)/v$(HAMMOCK_VERSION)-$(HOST_SHORT)-keystroke.edn
	./$(TARGET) --bench $(PERF_SCRIPTS)/cursor-macro.edn
	@cp $$(ls -t $(PERF_DIR)/runs/*.edn | head -1) \
	    $(PERF_BASELINE_DIR)/v$(HAMMOCK_VERSION)-$(HOST_SHORT)-cursor-macro.edn
	./$(TARGET) --bench $(PERF_SCRIPTS)/dispatch-mix.edn
	@cp $$(ls -t $(PERF_DIR)/runs/*.edn | head -1) \
	    $(PERF_BASELINE_DIR)/v$(HAMMOCK_VERSION)-$(HOST_SHORT)-dispatch-mix.edn
	@echo "perf: baseline written to $(PERF_BASELINE_DIR)/v$(HAMMOCK_VERSION)-$(HOST_SHORT)-*.edn"

perf-diff: $(TARGET) $(PERF_FIXTURES)/medium.txt
	./$(TARGET) --bench $(PERF_SCRIPTS)/keystroke-latency.edn
	@LATEST=$$(find $(PERF_DIR)/runs -name '*.edn' -type f -exec stat -f '%m %N' {} \; | sort -rn | head -1 | cut -d' ' -f2-); \
	./$(TARGET) -e "(hammock.perf/report \"$(PERF_BASELINE_DIR)/v$(HAMMOCK_VERSION)-$(HOST_SHORT)-keystroke.edn\" \"$$LATEST\")"

pty-bench: $(PTY_BENCH_BIN)

$(PTY_BENCH_BIN): $(PTY_BENCH_SRC) | $(BUILD_DIR)
	$(CC) -Wall -Wextra -std=c11 -g -O2 $(PTY_BENCH_SRC) -o $(PTY_BENCH_BIN)

perf-pty: $(PTY_BENCH_BIN) $(TARGET) $(PERF_FIXTURES)/medium.txt
	$(PTY_BENCH_BIN) $(PERF_SCRIPTS)/pty-smoke.txt

clean:
	rm -rf $(BUILD_DIR) $(TARGET) $(NEWS_HEADER) $(LOGO_HEADER)

clean-all: clean
	$(MAKE) -C libsci clean

.PHONY: clean clean-all check perf-fixtures perf-run perf-baseline perf-diff pty-bench perf-pty
