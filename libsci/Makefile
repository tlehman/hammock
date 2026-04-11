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

clean:
	rm -rf target libsci.$(LIB_EXT) libsci.h graal_isolate.h graal_isolate_dynamic.h libsci_dynamic.h
