BUILD_DIR := build
BIN       := $(BUILD_DIR)/apex

.PHONY: all build basic test clean help

all: build

# Configure on first run, then always invoke cmake --build (it's a no-op when nothing changed).
build:
	@test -d $(BUILD_DIR) || cmake -S . -B $(BUILD_DIR)
	@cmake --build $(BUILD_DIR)

basic: build
	@$(BIN) --basic

test: build
	@cd $(BUILD_DIR) && ctest --output-on-failure

clean:
	@rm -rf $(BUILD_DIR)

help:
	@echo "Targets:"
	@echo "  make           Configure (if needed) and compile."
	@echo "  make basic     Build and run the --basic demo."
	@echo "  make test      Build and run all tests."
	@echo "  make clean     Remove the build directory."
