BUILD_DIR := build
BIN       := $(BUILD_DIR)/apex

.PHONY: all build build-apex sim test clean help

all: build-apex

# Builds just the apex binary. Doesn't gate on tests compiling.
build-apex:
	@test -d $(BUILD_DIR) || cmake -S . -B $(BUILD_DIR)
	@cmake --build $(BUILD_DIR) --target apex

# Builds everything (apex + all test targets).
build:
	@test -d $(BUILD_DIR) || cmake -S . -B $(BUILD_DIR)
	@cmake --build $(BUILD_DIR)

sim: build-apex
	@$(BIN) --sim

test: build
	@cd $(BUILD_DIR) && ctest --output-on-failure

clean:
	@rm -rf $(BUILD_DIR)

help:
	@echo "Targets:"
	@echo "  make           Build the apex binary."
	@echo "  make sim       Build apex and run the --sim demo."
	@echo "  make test      Build everything and run all tests."
	@echo "  make clean     Remove the build directory."
