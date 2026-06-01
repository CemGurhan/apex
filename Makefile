BUILD_DIR := build
BIN       := $(BUILD_DIR)/apex

.PHONY: all build build-apex sim clear html test clean help

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

clear: build-apex
	@$(BIN) --clear

html: build-apex
	@test -n "$(RUN_DIR)" || (echo "Usage: make html RUN_DIR=<path/to/run_<uuid>>"; exit 1)
	@$(BIN) --html $(RUN_DIR)

test: build
	@cd $(BUILD_DIR) && ctest --output-on-failure

clean:
	@rm -rf $(BUILD_DIR)

help:
	@echo "Targets:"
	@echo "  make                       Build the apex binary."
	@echo "  make sim                   Build apex and run the --sim demo."
	@echo "  make clear                 Build apex and remove all run_<uuid> subdirs under the cfg's pnl_csv_dir."
	@echo "  make html RUN_DIR=<path>   Build apex and generate report.html inside the given run dir."
	@echo "  make test                  Build everything and run all tests."
	@echo "  make clean                 Remove the build directory."
