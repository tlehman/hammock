# Build libsci: SCI as a GraalVM native-image shared library
#
# Requires: GraalVM JDK with native-image (use `nix develop` from project root)

UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
  LIB_EXT = dylib
else
  LIB_EXT = so
endif

TARGET = libsci.$(LIB_EXT)
UBER_JAR = target/libsci.jar

.PHONY: all clean jar native-image

all: $(TARGET)

# Step 1: Build uber JAR with SCI + entry points
jar: $(UBER_JAR)

$(UBER_JAR):
	clojure -T:build uber

# Step 2: Compile to native shared library
# --shared mode scans the classpath for @CEntryPoint methods, so no
# main class is needed. Using -cp (not -jar) since the uber jar has
# no Main-Class manifest.
$(TARGET): $(UBER_JAR)
	native-image \
		--shared \
		-cp $(UBER_JAR) \
		-H:Name=libsci \
		--no-fallback \
		--initialize-at-build-time \
		-H:+ReportExceptionStackTraces \
		-o libsci
ifeq ($(UNAME_S),Darwin)
	@# Make the dylib relocatable and point libz at the macOS system copy
	@# so the shipped artifact has no /nix/store paths.
	install_name_tool -id @rpath/libsci.dylib $(TARGET)
	@NIX_LIBZ=$$(otool -L $(TARGET) | awk '/\/nix\/store\/.*libz/ {print $$1; exit}'); \
	if [ -n "$$NIX_LIBZ" ]; then \
		echo "install_name_tool -change $$NIX_LIBZ /usr/lib/libz.1.dylib $(TARGET)"; \
		install_name_tool -change "$$NIX_LIBZ" /usr/lib/libz.1.dylib $(TARGET); \
	fi
	@echo "--- otool -L $(TARGET) ---"; otool -L $(TARGET)
	@if otool -L $(TARGET) | grep -q /nix/store; then \
		echo "ERROR: /nix/store path leaked into $(TARGET)"; exit 1; \
	fi
endif

clean:
	rm -rf target libsci.$(LIB_EXT) libsci.h graal_isolate.h graal_isolate_dynamic.h libsci_dynamic.h
