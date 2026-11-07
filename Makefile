# =============================================================================
# Vitte — Root Makefile
# =============================================================================
#
# Repository layout:
#
#   src/
#     CMakeLists.txt
#     Makefile
#     runtime/
#     bootstrap/
#       api/
#       arena/
#       ast/
#       backend/c17/
#       builtin/
#       cli/
#       codegen/
#       config/
#       constant_fold/
#       diagnostic/
#       driver/
#       filesystem/
#       hir/
#       import/
#       ir/
#       lexer/
#       module/
#       parser/
#       printer/
#       scanner/
#       scope/
#       sema/
#       source/
#       symbol/
#       type/
#       unicode/
#       util/
#       main.c
#     tests/
#
# This Makefile is intended to be invoked from the repository root:
#
#   /Users/vincent/Documents/Github/vitte
#
# =============================================================================

PROJECT := Vitte
TARGET  := vitte
.DEFAULT_GOAL := all

BUILD_HOST_OS := $(shell uname -s 2>/dev/null || echo unknown)
EXE_SUFFIX ?= $(if $(filter MINGW% MSYS% CYGWIN%,$(BUILD_HOST_OS)),.exe,)
PATH_LIST_SEPARATOR := $(if $(filter MINGW% MSYS% CYGWIN%,$(BUILD_HOST_OS)),;,:)

CC ?= cc
AR ?= ar
RANLIB ?= ranlib

PYTHON ?= python3

# -----------------------------------------------------------------------------
# Repository layout
# -----------------------------------------------------------------------------

VITTE_ROOT := src

SRC_ROOT     := $(VITTE_ROOT)/bootstrap
RUNTIME_ROOT := $(VITTE_ROOT)/runtime
TEST_DIR     := $(VITTE_ROOT)/tests
DOC_DIR      := $(VITTE_ROOT)/docs

# -----------------------------------------------------------------------------
# Build directories
# -----------------------------------------------------------------------------

BUILD   := build
OBJ_DIR := $(BUILD)/obj
DEP_DIR := $(BUILD)/dep
BIN_DIR := $(BUILD)/bin
TMP_DIR := $(BUILD)/tmp
LOG_DIR := $(BUILD)/logs
BOOTSTRAP_ARCHIVE := $(BUILD)/lib/libvitte-bootstrap.a

TARGET_BIN := $(BIN_DIR)/$(TARGET)$(EXE_SUFFIX)
BOOTSTRAP_TARGET_BIN := $(BIN_DIR)/$(TARGET)-bootstrap$(EXE_SUFFIX)

# -----------------------------------------------------------------------------
# Source discovery
# -----------------------------------------------------------------------------

SOURCES := $(shell find $(SRC_ROOT) -type f -name '*.c' | LC_ALL=C sort)

OBJECTS := $(patsubst $(SRC_ROOT)/%.c,$(OBJ_DIR)/%.o,$(SOURCES))
DEPS    := $(patsubst $(SRC_ROOT)/%.c,$(DEP_DIR)/%.d,$(SOURCES))

MAIN_SOURCE := $(SRC_ROOT)/main.c
MAIN_OBJECT := $(OBJ_DIR)/main.o

LIB_OBJECTS := $(filter-out $(MAIN_OBJECT),$(OBJECTS))

# -----------------------------------------------------------------------------
# Include paths
# -----------------------------------------------------------------------------
#
# IMPORTANT:
#
# Do NOT add:
#
#   -Isrc/include/vitte
#   -Iinclude/vitte
#
# or another directory capable of shadowing libc headers such as <string.h>.
#
# Headers are colocated with their implementation directories and source files
# should normally include sibling headers directly.
#
# SRC_ROOT is sufficient for module-qualified includes such as:
#
#   #include "diagnostic/diagnostic.h"
#   #include "parser/parser.h"
#
# Local "foo.h" includes are resolved relative to foo.c automatically.
#
# -----------------------------------------------------------------------------

CPPFLAGS := \
	-I$(SRC_ROOT) \
	-I$(VITTE_ROOT) \
	-Iinclude \
	-Isrc/include

# -----------------------------------------------------------------------------
# Common compiler warnings
# -----------------------------------------------------------------------------

CFLAGS_COMMON := \
	-std=c17 \
	-Wall \
	-Wextra \
	-Wpedantic \
	-Wconversion \
	-Wsign-conversion \
	-Wshadow \
	-Wcast-align \
	-Wwrite-strings \
	-Wundef \
	-Wformat \
	-Wstrict-prototypes \
	-Wold-style-definition \
	-Wimplicit-fallthrough \
	-Wvla \
	-Wpointer-arith \
	-Wbad-function-cast

# -----------------------------------------------------------------------------
# Build modes
# -----------------------------------------------------------------------------

CFLAGS_DEBUG := \
	-O0 \
	-g3 \
	-fno-omit-frame-pointer

CFLAGS_RELEASE := \
	-O2 \
	-DNDEBUG

CFLAGS_ASAN := \
	-O1 \
	-g3 \
	-fno-omit-frame-pointer \
	-fsanitize=address,undefined

CFLAGS_UBSAN := \
	-O1 \
	-g3 \
	-fno-omit-frame-pointer \
	-fsanitize=undefined

MODE ?= debug

LDFLAGS :=
LDLIBS   :=

# constant_fold and generated/runtime code require the C math library on
# Unix platforms where libm is linked separately.
UNAME_S := $(shell uname -s 2>/dev/null || echo unknown)
UNAME_M := $(shell uname -m 2>/dev/null || echo unknown)

# Solaris and illumos hide some POSIX structure fields in strict C modes unless
# their extension namespace is enabled (for example, struct stat timestamps).
ifeq ($(UNAME_S),SunOS)
CPPFLAGS += -D__EXTENSIONS__
endif

ifneq ($(filter Linux FreeBSD OpenBSD NetBSD DragonFly SunOS,$(UNAME_S)),)
LDLIBS += -lm
endif

ifeq ($(MODE),release)
	CFLAGS := $(CFLAGS_COMMON) $(CFLAGS_RELEASE)
else ifeq ($(MODE),asan)
	CFLAGS := $(CFLAGS_COMMON) $(CFLAGS_ASAN)
	LDFLAGS += -fsanitize=address,undefined
else ifeq ($(MODE),ubsan)
	CFLAGS := $(CFLAGS_COMMON) $(CFLAGS_UBSAN)
	LDFLAGS += -fsanitize=undefined
else
	CFLAGS := $(CFLAGS_COMMON) $(CFLAGS_DEBUG)
endif

# Keep assertions enabled in C test harnesses for every build profile. Several
# harnesses use assert expressions to validate setup and API return values.
TEST_CFLAGS := $(filter-out -DNDEBUG,$(CFLAGS)) -UNDEBUG

# Keep build products valid when the host, compiler, flags, or profile change.
# A stable configuration is a no-op; a changed signature invalidates objects
# and linked binaries.
shell_quote = '$(subst ','"'"',$(1))'
BUILD_CONFIG_KEY := $(shell printf '%s\n' \
	$(call shell_quote,$(MODE)) \
	$(call shell_quote,$(UNAME_S)) \
	$(call shell_quote,$(UNAME_M)) \
	$(call shell_quote,$(CC)) \
	$(call shell_quote,$(AR)) \
	$(call shell_quote,$(CPPFLAGS)) \
	$(call shell_quote,$(CFLAGS)) \
	$(call shell_quote,$(LDFLAGS)) \
	$(call shell_quote,$(LDLIBS)) | \
	if command -v shasum >/dev/null 2>&1; then \
		shasum -a 256 | awk '{print $$1}'; \
	elif command -v sha256sum >/dev/null 2>&1; then \
		sha256sum | awk '{print $$1}'; \
	else \
		cksum | awk '{print $$1 "-" $$2}'; \
	fi)
BUILD_CONFIG_STAMP := $(BUILD)/.config-$(BUILD_CONFIG_KEY)
ACTIVE_BUILD_CONFIG := $(BUILD)/.active-config
# A conditional phony stamp makes a profile switch invalidate outputs even on
# filesystems whose timestamp precision is coarser than a fast rebuild.
BUILD_CONFIG_CHANGED := $(shell \
	if test -f '$(ACTIVE_BUILD_CONFIG)' && \
		test "$$(cat '$(ACTIVE_BUILD_CONFIG)')" = '$(BUILD_CONFIG_KEY)'; then \
		printf no; \
	else \
		printf yes; \
	fi)

ifeq ($(BUILD_CONFIG_CHANGED),yes)
.PHONY: $(BUILD_CONFIG_STAMP)
endif

# -----------------------------------------------------------------------------
# Default build
# -----------------------------------------------------------------------------

.PHONY: all
all: $(TARGET_BIN)

# Build only the C-compiled seed; `all` also needs it to build the native frontend.
.PHONY: bootstrap
bootstrap: $(BOOTSTRAP_TARGET_BIN)
	@printf '\nBootstrap compiler ready: %s\n' "$(BOOTSTRAP_TARGET_BIN)"

$(BUILD_CONFIG_STAMP):
	@mkdir -p $(BUILD)
	@touch "$@"
	@printf '%s\n' '$(BUILD_CONFIG_KEY)' > "$(ACTIVE_BUILD_CONFIG)"

# The bootstrap binary emits the Vitte API translation unit. The standard
# executable links that Vitte frontend with only OS and ABI boundary objects;
# the historical C compiler remains isolated in the bootstrap target.
NATIVE_FRONTEND_ENTRY := $(VITTE_ROOT)/compiler/api/api.vitte
NATIVE_FRONTEND_SOURCES := $(shell find $(VITTE_ROOT)/compiler -type f -name '*.vitte' | LC_ALL=C sort)
# The manifest defines the package prefix stripped by the source module loader.
NATIVE_MODULE_SOURCES := modules/core/Vitte.toml $(shell find modules/core/src -type f \( -name '*.vit' -o -name '*.vitte' \) | LC_ALL=C sort)
VITTE_MODULE_PATH ?= modules/core/src
NATIVE_FRONTEND_COMPILER ?= $(BOOTSTRAP_TARGET_BIN)
NATIVE_FRONTEND_DIR := $(BUILD)/native-frontend
NATIVE_FRONTEND_C := $(NATIVE_FRONTEND_DIR)/api-implementation.c
NATIVE_FRONTEND_OBJECT := $(NATIVE_FRONTEND_DIR)/api-implementation.o
NATIVE_HOST_OBJECT := $(NATIVE_FRONTEND_DIR)/native-host.o
# C ABI bridge retained for the parity harness; never link it into vitte.
NATIVE_API_OBJECT := $(NATIVE_FRONTEND_DIR)/api.o
NATIVE_API_DEP := $(NATIVE_FRONTEND_DIR)/api.d
NATIVE_MAIN_OBJECT := $(NATIVE_FRONTEND_DIR)/main.o
NATIVE_IO_OBJECT := $(NATIVE_FRONTEND_DIR)/io.o
NATIVE_PLATFORM_OBJECT := $(OBJ_DIR)/platform/platform.o
NATIVE_FILESYSTEM_OBJECT := $(OBJ_DIR)/filesystem/filesystem.o
NATIVE_FRONTEND_LIBRARY := $(NATIVE_FRONTEND_DIR)/libvitte-native-frontend.a
SELFHOST_DIR := $(BUILD)/selfhost
SELFHOST_STAGE1_C := $(SELFHOST_DIR)/stage1-api.c
SELFHOST_STAGE2_C := $(SELFHOST_DIR)/stage2-api.c
SELFHOST_STAGE3_C := $(SELFHOST_DIR)/stage3-api.c
SELFHOST_STAGE1_BUILD := $(SELFHOST_DIR)/stage1-build
SELFHOST_STAGE2_BUILD := $(SELFHOST_DIR)/stage2-build
SELFHOST_STAGE3_BUILD := $(SELFHOST_DIR)/stage3-build
SELFHOST_STAGE1_BIN := $(SELFHOST_STAGE1_BUILD)/bin/$(TARGET)$(EXE_SUFFIX)
SELFHOST_STAGE2_BIN := $(SELFHOST_STAGE2_BUILD)/bin/$(TARGET)$(EXE_SUFFIX)
SELFHOST_STAGE3_BIN := $(SELFHOST_STAGE3_BUILD)/bin/$(TARGET)$(EXE_SUFFIX)
SELFHOST_STEP_RUNNER := scripts/run-selfhost-step.sh
SELFHOST_EVIDENCE_RUN ?= $(abspath $(SELFHOST_DIR))/evidence/$(shell date -u +%Y%m%dT%H%M%SZ)-$(shell git rev-parse --short HEAD 2>/dev/null || printf no-commit)-$(shell printf '%s' $$$$)
SELFHOST_EVIDENCE_RUN := $(SELFHOST_EVIDENCE_RUN)
export SELFHOST_EVIDENCE_RUN
SELFHOST_EVIDENCE_OPTIONS := MODE=$(MODE); CC=$(CC); CPPFLAGS=$(CPPFLAGS); CFLAGS=$(CFLAGS); LDFLAGS=$(LDFLAGS); LDLIBS=$(LDLIBS); VITTE_MODULE_PATH=$(VITTE_MODULE_PATH)
SELFHOST_STEP_EVIDENCE = --evidence-dir "$(SELFHOST_EVIDENCE_RUN)/stage$(1)" --options "$(SELFHOST_EVIDENCE_OPTIONS)"
SELFHOST_STAGE0_STAMP := $(SELFHOST_DIR)/stage0-verified.stamp
SELFHOST_STAGE1_STAMP := $(SELFHOST_DIR)/stage1-verified.stamp
SELFHOST_STAGE2_STAMP := $(SELFHOST_DIR)/stage2-verified.stamp
SELFHOST_STAGE3_STAMP := $(SELFHOST_DIR)/stage3-verified.stamp
SELFHOST_C_INPUTS := $(SOURCES) \
	$(shell find $(SRC_ROOT) include src/include -type f -name '*.h' 2>/dev/null | LC_ALL=C sort)
SELFHOST_TEST_INPUTS := $(shell find $(TEST_DIR) -type f 2>/dev/null | LC_ALL=C sort) \
	grammar/vitte.ebnf scripts/check-ebnf-frontend-sync.py
NATIVE_LINK_OBJECTS := \
	$(NATIVE_PLATFORM_OBJECT) \
	$(NATIVE_FILESYSTEM_OBJECT) \
	$(NATIVE_HOST_OBJECT) \
	$(NATIVE_IO_OBJECT) \
	$(NATIVE_MAIN_OBJECT)

.PHONY: native-frontend
native-frontend: $(NATIVE_FRONTEND_LIBRARY)

$(NATIVE_FRONTEND_C): $(NATIVE_FRONTEND_COMPILER) $(NATIVE_FRONTEND_SOURCES) $(NATIVE_MODULE_SOURCES)
	@mkdir -p $(NATIVE_FRONTEND_DIR)
	@printf '[VITTE-C] %s\n' "$(NATIVE_FRONTEND_ENTRY)"
	VITTE_MODULE_PATH="$(VITTE_MODULE_PATH)$(PATH_LIST_SEPARATOR)$(VITTE_ROOT)" $(NATIVE_FRONTEND_COMPILER) compile "$(NATIVE_FRONTEND_ENTRY)" --emit-c --quiet --no-color -o "$@"

$(NATIVE_FRONTEND_OBJECT): $(NATIVE_FRONTEND_C) $(BUILD_CONFIG_STAMP)
	@printf '[CC] %s\n' "$<"
	$(CC) $(CPPFLAGS) $(CFLAGS) -c "$<" -o "$@"

$(NATIVE_HOST_OBJECT): $(VITTE_ROOT)/compiler/driver/native_host.c $(SRC_ROOT)/platform/platform.h $(BUILD_CONFIG_STAMP)
	@mkdir -p $(NATIVE_FRONTEND_DIR)
	@printf '[CC-NATIVE-HOST] %s\n' "$<"
	$(CC) $(CPPFLAGS) $(CFLAGS) -DVITTE_NATIVE_FRONTEND_LINKED \
		-MMD -MP -c "$<" -o "$@"

$(NATIVE_API_OBJECT): $(SRC_ROOT)/api/api.c $(SRC_ROOT)/api/native_api_bridge.inc $(BUILD_CONFIG_STAMP)
	@mkdir -p $(NATIVE_FRONTEND_DIR)
	@printf '[CC-NATIVE-API] %s\n' "$<"
	$(CC) $(CPPFLAGS) $(CFLAGS) -DVITTE_NATIVE_FRONTEND_LINKED \
		-MMD -MP -MF "$(NATIVE_API_DEP)" -c "$<" -o "$@"

$(NATIVE_MAIN_OBJECT): $(VITTE_ROOT)/compiler/driver/native_main.c $(BUILD_CONFIG_STAMP)
	@mkdir -p $(NATIVE_FRONTEND_DIR)
	@printf '[CC-NATIVE-MAIN] %s\n' "$<"
	$(CC) $(CPPFLAGS) $(CFLAGS) -DVITTE_NATIVE_FRONTEND_LINKED \
		-MMD -MP -c "$<" -o "$@"

$(NATIVE_IO_OBJECT): $(VITTE_ROOT)/compiler/driver/native_io.c $(BUILD_CONFIG_STAMP)
	@mkdir -p $(NATIVE_FRONTEND_DIR)
	@printf '[CC-NATIVE-IO] %s\n' "$<"
	$(CC) $(CPPFLAGS) $(CFLAGS) -DVITTE_NATIVE_FRONTEND_LINKED \
		-MMD -MP -c "$<" -o "$@"

$(NATIVE_FRONTEND_LIBRARY): $(NATIVE_FRONTEND_OBJECT) Makefile
	@printf '[AR] %s\n' "$@"
	@rm -f "$@"
	$(AR) rcs "$@" $(NATIVE_FRONTEND_OBJECT)

$(BOOTSTRAP_TARGET_BIN): $(OBJECTS) $(BUILD_CONFIG_STAMP)
	@mkdir -p $(dir $@)
	@printf '\n[LD] %s\n' "$@"
	$(CC) \
		$(OBJECTS) \
		$(LDFLAGS) \
		$(LDLIBS) \
		-o $@
	@printf '\nBuilt Vitte bootstrap compiler:\n'
	@printf '  %s\n\n' "$@"

.PHONY: bootstrap-archive
bootstrap-archive: $(BOOTSTRAP_ARCHIVE)

$(BOOTSTRAP_ARCHIVE): $(LIB_OBJECTS)
	@mkdir -p $(dir $@)
	@printf '[AR-BOOTSTRAP] %s\n' "$@"
	@rm -f "$@"
	$(AR) rcs "$@" $(LIB_OBJECTS)
	$(RANLIB) "$@"

$(TARGET_BIN): $(NATIVE_LINK_OBJECTS) $(NATIVE_FRONTEND_LIBRARY) $(BUILD_CONFIG_STAMP)
	@mkdir -p $(dir $@)
	@printf '\n[LD] %s\n' "$@"
	$(CC) \
		$(NATIVE_LINK_OBJECTS) \
		$(NATIVE_FRONTEND_LIBRARY) \
		$(LDFLAGS) \
		$(LDLIBS) \
		-o $@
	@printf '\nBuilt Vitte compiler with native frontend:\n'
	@printf '  %s\n\n' "$@"

# -----------------------------------------------------------------------------
# Object compilation
# -----------------------------------------------------------------------------

$(OBJ_DIR)/%.o: $(SRC_ROOT)/%.c $(BUILD_CONFIG_STAMP)
	@mkdir -p $(dir $@)
	@mkdir -p $(dir $(DEP_DIR)/$*.d)
	@printf '[CC] %s\n' "$<"
	$(CC) \
		$(CPPFLAGS) \
		$(CFLAGS) \
		-MMD \
		-MP \
		-MF $(DEP_DIR)/$*.d \
		-c $< \
		-o $@

-include $(DEPS)
-include $(NATIVE_CLI_PIPELINE_DEP)
-include $(NATIVE_API_DEP)

# -----------------------------------------------------------------------------
# Build modes
# -----------------------------------------------------------------------------

.PHONY: debug
debug:
	$(MAKE) MODE=debug all

.PHONY: release
release:
	$(MAKE) MODE=release all

.PHONY: asan
asan:
	$(MAKE) MODE=asan all

.PHONY: ubsan
ubsan:
	$(MAKE) MODE=ubsan all

# -----------------------------------------------------------------------------
# Compiler execution
# -----------------------------------------------------------------------------

.PHONY: run
run: $(TARGET_BIN)
	$(TARGET_BIN)

.PHONY: version
version: $(TARGET_BIN)
	$(TARGET_BIN) --version

.PHONY: cli-help
cli-help: $(TARGET_BIN)
	$(TARGET_BIN) --help

# -----------------------------------------------------------------------------
# Vitte frontend
# -----------------------------------------------------------------------------

.PHONY: compile
compile: $(TARGET_BIN)
ifndef FILE
	$(error FILE is required: make compile FILE=program.vit)
endif
	$(TARGET_BIN) compile "$(FILE)"

.PHONY: vitte-check
vitte-check: $(TARGET_BIN)
ifndef FILE
	$(error FILE is required: make vitte-check FILE=program.vit)
endif
	$(TARGET_BIN) check "$(FILE)"

.PHONY: test-cli-driver
test-cli-driver: $(TARGET_BIN) $(BOOTSTRAP_TARGET_BIN)
	@mkdir -p $(BUILD)
	@set -e; \
	fixture="$(TEST_DIR)/native_selfhost_smoke.vit"; \
	printf '[CLI] check, lex and parse stages\n'; \
	if $(TARGET_BIN) check "$$fixture" \
		>/dev/null \
		2>$(BUILD)/cli-check.stderr; then \
		:; \
	else \
		rc=$$?; \
		printf '[CLI] check failed for %s (exit %s)\n' "$$fixture" "$$rc" >&2; \
		cat $(BUILD)/cli-check.stderr >&2; \
		exit "$$rc"; \
	fi; \
	if test -s $(BUILD)/cli-check.stderr; then \
		printf '[CLI] check wrote unexpected stderr for %s\n' "$$fixture" >&2; \
		cat $(BUILD)/cli-check.stderr >&2; \
		exit 1; \
	fi; \
	$(TARGET_BIN) lex "$$fixture" \
		>$(BUILD)/cli-tokens.txt; \
	test -s $(BUILD)/cli-tokens.txt; \
	$(TARGET_BIN) parse "$$fixture" \
		>$(BUILD)/cli-ast.txt; \
	grep -q "AST root kind:" $(BUILD)/cli-ast.txt; \
	printf '[CLI] compile and run native programs\n'; \
	$(TARGET_BIN) build "$$fixture" \
		-o "$(BUILD)/cli-native"; \
	"$(BUILD)/cli-native"; \
	$(TARGET_BIN) run "$$fixture" --quiet; \
	printf '[CLI] emit C and build expo_math test declarations\n'; \
	$(TARGET_BIN) compile "$(TEST_DIR)/expo_math.vit" \
		--emit-c --quiet --no-color -o "$(BUILD)/cli-expo-math.c"; \
	grep -q "vitte_test_0" \
		"$(BUILD)/cli-expo-math.c"; \
	$(TARGET_BIN) build "$(TEST_DIR)/expo_math.vit" \
		-o "$(BUILD)/cli-expo-math"; \
	"$(BUILD)/cli-expo-math"; \
	printf '[CLI] build and run game program\n'; \
	$(TARGET_BIN) build "$(TEST_DIR)/game.vit" \
		-o "$(BUILD)/cli-game"; \
	printf '6\n' | $(TARGET_BIN) run "$(TEST_DIR)/game.vit" --quiet \
		>$(BUILD)/cli-game.stdout; \
	grep -q "PARTIE TERMINEE" $(BUILD)/cli-game.stdout; \
	printf '[CLI] resolve a project-local installed module\n'; \
	cd "$(TEST_DIR)/project_local_modules" && \
		"$(abspath $(BOOTSTRAP_TARGET_BIN))" run main.vit --quiet

.PHONY: emit-c
emit-c: $(TARGET_BIN)
ifndef FILE
	$(error FILE is required: make emit-c FILE=program.vit)
endif
	@mkdir -p $(BUILD)
	$(TARGET_BIN) compile "$(FILE)" --emit-c -o "$(BUILD)/generated.c"
	@printf '\nGenerated C:\n'
	@printf '  %s\n' "$(BUILD)/generated.c"

# -----------------------------------------------------------------------------
# C source verification
# -----------------------------------------------------------------------------

.PHONY: check
check:
	@printf 'Checking Vitte C sources...\n\n'
	@set -e; \
	for file in $(SOURCES); do \
		printf '[CHECK] %s\n' "$$file"; \
		$(CC) \
			$(CPPFLAGS) \
			$(CFLAGS_COMMON) \
			-fsyntax-only \
			"$$file"; \
	done
	@printf '\nAll C translation units passed syntax checking.\n'

# -----------------------------------------------------------------------------
# Source manager test
# -----------------------------------------------------------------------------

SOURCE_TEST_BIN := $(BIN_DIR)/source_manager_test

.PHONY: test-source
test-source:
	@mkdir -p $(BIN_DIR)
	@printf '[TEST-CC] source_manager_test\n'
	$(CC) \
		$(CPPFLAGS) \
		$(TEST_CFLAGS) \
		$(TEST_DIR)/source_manager_test.c \
		$(SRC_ROOT)/source/source.c \
		$(SRC_ROOT)/api/error.c \
		$(LDFLAGS) \
		$(LDLIBS) \
		-o $(SOURCE_TEST_BIN)
	$(SOURCE_TEST_BIN)

# -----------------------------------------------------------------------------
# Diagnostic rendering
# -----------------------------------------------------------------------------

DIAGNOSTIC_RENDER_TEST_BIN := $(BIN_DIR)/diagnostic_render_test

.PHONY: test-diagnostic
test-diagnostic: $(TARGET_BIN) $(BOOTSTRAP_TARGET_BIN) $(LIB_OBJECTS)
	@mkdir -p $(BIN_DIR)
	@printf '[TEST-CC] diagnostic_render_test\n'
	$(CC) \
		$(CPPFLAGS) \
		$(TEST_CFLAGS) \
		$(TEST_DIR)/diagnostic_render_test.c \
		$(LIB_OBJECTS) \
		$(LDFLAGS) \
		$(LDLIBS) \
		-o $(DIAGNOSTIC_RENDER_TEST_BIN)
	$(DIAGNOSTIC_RENDER_TEST_BIN)
	@set +e; \
	output=`$(BOOTSTRAP_TARGET_BIN) parse \
		$(TEST_DIR)/diagnostic_parser_invalid.vit \
		--quiet --no-color 2>&1`; \
	rc=$$?; \
	test "$$rc" -eq 1 && \
	printf '%s\n' "$$output" | grep -F 'E0203' >/dev/null && \
	printf '%s\n' "$$output" | grep -F 'phase: parser' >/dev/null && \
	printf '%s\n' "$$output" | grep -F 'help: Provide a valid expression' >/dev/null
	@set +e; \
	output=`$(BOOTSTRAP_TARGET_BIN) lex \
		$(TEST_DIR)/diagnostic_lexer_invalid.vit \
		--quiet --no-color 2>&1`; \
	rc=$$?; \
	test "$$rc" -eq 1 && \
	printf '%s\n' "$$output" | grep -F 'E0102' >/dev/null && \
	printf '%s\n' "$$output" | grep -F 'phase: lexer' >/dev/null && \
	printf '%s\n' "$$output" | grep -F 'help: Close the string' >/dev/null
	@set +e; \
	output=`$(BOOTSTRAP_TARGET_BIN) --not-a-valid-option 2>&1`; \
	status=$$?; \
	test "$$status" -eq 2 && \
	printf '%s\n' "$$output" | grep -F 'E0011' >/dev/null && \
	printf '%s\n' "$$output" | grep -F 'phase: driver' >/dev/null && \
	printf '%s\n' "$$output" | grep -F 'help: Correct the option named' >/dev/null

# -----------------------------------------------------------------------------
# Diagnostic fuzz test
# -----------------------------------------------------------------------------

DIAGNOSTIC_FUZZ_TEST_BIN := $(BIN_DIR)/diagnostic_fuzz_test

.PHONY: test-diagnostic-fuzz
test-diagnostic-fuzz: $(TARGET_BIN)
	@mkdir -p $(BIN_DIR)
	@printf '[TEST-CC] diagnostic_fuzz_test\n'
	$(CC) \
		$(CPPFLAGS) \
		$(TEST_CFLAGS) \
		$(TEST_DIR)/diagnostic_fuzz_test.c \
		$(LIB_OBJECTS) \
		$(LDFLAGS) \
		$(LDLIBS) \
		-o $(DIAGNOSTIC_FUZZ_TEST_BIN)
	$(DIAGNOSTIC_FUZZ_TEST_BIN)

# Contextual help through the real CLI and machine-readable consumers.
.PHONY: test-diagnostic-help
test-diagnostic-help: $(TARGET_BIN) $(BOOTSTRAP_TARGET_BIN)
	$(PYTHON) $(TEST_DIR)/diagnostic_help_test.py $(TARGET_BIN) $(BOOTSTRAP_TARGET_BIN)

.PHONY: test-diagnostic-parity
test-diagnostic-parity: $(TARGET_BIN) $(BOOTSTRAP_TARGET_BIN)
	$(PYTHON) $(TEST_DIR)/diagnostic_parity_test.py $(TARGET_BIN) $(BOOTSTRAP_TARGET_BIN)

.PHONY: test-native-bootstrap-parity
NATIVE_BOOTSTRAP_PARITY_TEST_BIN := $(BIN_DIR)/native_bootstrap_parity_test

$(NATIVE_BOOTSTRAP_PARITY_TEST_BIN): $(TEST_DIR)/native_parser_parity_test.c $(NATIVE_LINK_OBJECTS) $(NATIVE_API_OBJECT) $(NATIVE_FRONTEND_LIBRARY) $(BOOTSTRAP_ARCHIVE) Makefile
	@mkdir -p $(BIN_DIR)
	@printf '[TEST-CC] native_parser_parity_test\n'
	$(CC) $(CPPFLAGS) $(TEST_CFLAGS) -DVITTE_NATIVE_FRONTEND_LINKED \
		$(TEST_DIR)/native_parser_parity_test.c \
		$(filter-out $(MAIN_OBJECT) $(NATIVE_MAIN_OBJECT),$(NATIVE_LINK_OBJECTS)) \
		$(NATIVE_API_OBJECT) $(NATIVE_FRONTEND_LIBRARY) $(BOOTSTRAP_ARCHIVE) $(LDFLAGS) $(LDLIBS) -o $@

test-native-bootstrap-parity: $(TARGET_BIN) $(BOOTSTRAP_TARGET_BIN) $(NATIVE_BOOTSTRAP_PARITY_TEST_BIN)
	$(PYTHON) $(TEST_DIR)/native_bootstrap_parity_test.py \
		$(TARGET_BIN) $(BOOTSTRAP_TARGET_BIN) $(NATIVE_BOOTSTRAP_PARITY_TEST_BIN)

.PHONY: test-native-frontend-subset
test-native-frontend-subset: $(TARGET_BIN)
	$(PYTHON) $(TEST_DIR)/native_frontend_subset_test.py $(TARGET_BIN)

# Targeted bootstrap import and diagnostic regressions.
.PHONY: test-bootstrap-regressions
test-bootstrap-regressions: $(BOOTSTRAP_TARGET_BIN)
	$(PYTHON) $(TEST_DIR)/bootstrap_regression_test.py $(BOOTSTRAP_TARGET_BIN)

# Check the seed directly without first building the native frontend.
.PHONY: bootstrap-check
bootstrap-check: $(BOOTSTRAP_TARGET_BIN)
	$(BOOTSTRAP_TARGET_BIN) --version
	$(PYTHON) $(TEST_DIR)/bootstrap_regression_test.py $(BOOTSTRAP_TARGET_BIN)
	$(BOOTSTRAP_TARGET_BIN) run examples/hello.vit --quiet --no-color
	@printf '\nBootstrap seed checks passed.\n'

# -----------------------------------------------------------------------------
# Golden diagnostic tests
# -----------------------------------------------------------------------------

.PHONY: test-diagnostic-golden
test-diagnostic-golden: $(BOOTSTRAP_TARGET_BIN)
	@mkdir -p $(TMP_DIR)
	@set +e; \
	$(BOOTSTRAP_TARGET_BIN) compile \
		$(TEST_DIR)/golden/unknown_symbol.input \
		--stop-after-sema --quiet --no-color \
		>$(TMP_DIR)/diagnostic_unknown_symbol.stdout \
		2>$(TMP_DIR)/diagnostic_unknown_symbol.stderr; \
	rc=$$?; \
	set -e; \
	test "$$rc" -eq 1 && \
	awk 'NF { last = NR } { lines[NR] = $$0 } END { for (i = 1; i <= last; i++) print lines[i] }' \
		$(TMP_DIR)/diagnostic_unknown_symbol.stderr \
		>$(TMP_DIR)/diagnostic_unknown_symbol.normalized && \
	diff -u $(TEST_DIR)/golden/diagnostic_unknown_symbol.stderr \
		$(TMP_DIR)/diagnostic_unknown_symbol.normalized
	@set +e; \
	$(BOOTSTRAP_TARGET_BIN) compile \
		$(TEST_DIR)/sema_calls/local_param_mismatch.vit \
		--stop-after-sema --quiet --no-color \
		>$(TMP_DIR)/diagnostic_type_mismatch.stdout \
		2>$(TMP_DIR)/diagnostic_type_mismatch.stderr; \
	rc=$$?; \
	set -e; \
	test "$$rc" -eq 1 && \
	awk 'NF { last = NR } { lines[NR] = $$0 } END { for (i = 1; i <= last; i++) print lines[i] }' \
		$(TMP_DIR)/diagnostic_type_mismatch.stderr \
		>$(TMP_DIR)/diagnostic_type_mismatch.normalized && \
	diff -u $(TEST_DIR)/golden/diagnostic_type_mismatch.stderr \
		$(TMP_DIR)/diagnostic_type_mismatch.normalized
	@set +e; \
	$(BOOTSTRAP_TARGET_BIN) lex \
		$(TEST_DIR)/diagnostic_lexer_invalid.vit \
		--quiet --no-color \
		>$(TMP_DIR)/diagnostic_lexer_invalid.stdout \
		2>$(TMP_DIR)/diagnostic_lexer_invalid.stderr; \
	rc=$$?; \
	set -e; \
	test "$$rc" -eq 1 && \
	awk 'NF { last = NR } { lines[NR] = $$0 } END { for (i = 1; i <= last; i++) print lines[i] }' \
		$(TMP_DIR)/diagnostic_lexer_invalid.stderr \
		>$(TMP_DIR)/diagnostic_lexer_invalid.normalized && \
	diff -u $(TEST_DIR)/golden/diagnostic_lexer_invalid.stderr \
		$(TMP_DIR)/diagnostic_lexer_invalid.normalized

# -----------------------------------------------------------------------------
# Structured diagnostic formats
# -----------------------------------------------------------------------------

.PHONY: test-diagnostic-formats
test-diagnostic-formats: $(BOOTSTRAP_TARGET_BIN)
	@set +e; \
	output=`$(BOOTSTRAP_TARGET_BIN) check \
		$(TEST_DIR)/diagnostic_parser_invalid.vit \
		--diagnostic=json 2>&1`; \
	rc=$$?; \
	test "$$rc" -eq 1 && \
	printf '%s\n' "$$output" | grep -F '"schema": "vitte-diagnostics"' >/dev/null && \
	printf '%s\n' "$$output" | grep -F '"code": "E0203"' >/dev/null && \
	printf '%s\n' "$$output" | grep -F '"kind": "help"' >/dev/null && \
	test "$$(printf '%s\n' "$$output" | tail -n 1)" = '}'
	@set +e; \
	output=`$(BOOTSTRAP_TARGET_BIN) compile \
		$(TEST_DIR)/diagnostic_sema_multiple_invalid.vit \
		--stop-after-sema --quiet --no-color \
		--diagnostic=json 2>&1`; \
	rc=$$?; \
	test "$$rc" -eq 1 && \
	printf '%s\n' "$$output" | grep -F '"stored": 2' >/dev/null && \
	test "$$(printf '%s\n' "$$output" | grep -Fc '"code": "E0602"')" -eq 2 && \
	printf '%s\n' "$$output" | grep -F '"kind": "help"' >/dev/null && \
	test "$$(printf '%s\n' "$$output" | tail -n 1)" = '}'
	@set +e; \
	output=`$(BOOTSTRAP_TARGET_BIN) check \
		$(TEST_DIR)/diagnostic_parser_invalid.vit \
		--diagnostic=sarif 2>&1`; \
	rc=$$?; \
	test "$$rc" -eq 1 && \
	printf '%s\n' "$$output" | grep -F '"version": "2.1.0"' >/dev/null && \
	printf '%s\n' "$$output" | grep -F '"ruleId": "E0203"' >/dev/null && \
	test "$$(printf '%s\n' "$$output" | tail -n 1)" = '}'
	@set +e; \
	output=`$(BOOTSTRAP_TARGET_BIN) compile \
		$(TEST_DIR)/diagnostic_sema_multiple_invalid.vit \
		--stop-after-sema --quiet --no-color \
		--diagnostic=sarif 2>&1`; \
	rc=$$?; \
	test "$$rc" -eq 1 && \
	test "$$(printf '%s\n' "$$output" | grep -Fc '"ruleId": "E0602"')" -eq 2 && \
	test "$$(printf '%s\n' "$$output" | tail -n 1)" = '}'
	@set +e; \
	output=`$(BOOTSTRAP_TARGET_BIN) --diagnostic=json --not-a-valid-option 2>&1`; \
	status=$$?; \
	test "$$status" -eq 2 && \
	printf '%s\n' "$$output" | grep -F '"code": "E0011"' >/dev/null && \
	printf '%s\n' "$$output" | grep -F '"schema": "vitte-diagnostics"' >/dev/null && \
	! printf '%s\n' "$$output" | grep -F 'Usage:' >/dev/null && \
	test "$$(printf '%s\n' "$$output" | tail -n 1)" = '}'
	@set +e; \
	output=`$(BOOTSTRAP_TARGET_BIN) check \
		$(TEST_DIR)/diagnostic_missing_source.vit \
		--diagnostic=json 2>&1`; \
	rc=$$?; \
	test "$$rc" -eq 1 && \
	printf '%s\n' "$$output" | grep -F '"schema": "vitte-diagnostics"' >/dev/null && \
	printf '%s\n' "$$output" | grep -F '"code": "E0005"' >/dev/null && \
	test "$$(printf '%s\n' "$$output" | tail -n 1)" = '}'

# -----------------------------------------------------------------------------
# Source map / generated C
# -----------------------------------------------------------------------------

SOURCE_MAP_INPUT ?= $(TEST_DIR)/assignment_ok.vit
SOURCE_MAP_C     := $(BUILD)/source-map.c

.PHONY: test-source-map
test-source-map: $(TARGET_BIN)
	@printf '[SKIP] source-map generation requires the disconnected code-generation pipeline\n'

# -----------------------------------------------------------------------------
# Corpus
# -----------------------------------------------------------------------------

.PHONY: test-corpus
test-corpus: $(TARGET_BIN) $(BOOTSTRAP_TARGET_BIN)
	$(PYTHON) \
		$(TEST_DIR)/run_corpus.py \
		--compiler $(BOOTSTRAP_TARGET_BIN) \
		--exclude src/tests/expression_literals_parse.vit \
		--exclude src/tests/grammar_alignment/ref_param_type_marker_ok.vit
	$(TARGET_BIN) check $(TEST_DIR)/expression_literals_parse.vit --quiet --no-color
	$(TARGET_BIN) check $(TEST_DIR)/grammar_alignment/ref_param_type_marker_ok.vit --quiet --no-color

.PHONY: test-unavailable
test-unavailable: $(TARGET_BIN)
	$(PYTHON) \
		$(TEST_DIR)/run_corpus.py \
		--compiler $(TARGET_BIN) \
		--include-unavailable

# -----------------------------------------------------------------------------
# Grammar alignment (direct lexer/parser API; independent of the CLI driver)
# -----------------------------------------------------------------------------

GRAMMAR_TEST_BIN := $(BIN_DIR)/grammar_parser_test
GRAMMAR_FIXTURES := \
	$(TEST_DIR)/grammar_alignment/let_mut_ok.vit \
	$(TEST_DIR)/grammar_alignment/ref_param_type_marker_ok.vit \
	$(TEST_DIR)/expression_literals_parse.vit \
	$(TEST_DIR)/form_parse.vit \
	$(TEST_DIR)/pick_parse.vit \
	$(TEST_DIR)/nested_generic_type_arguments_parse.vit

$(GRAMMAR_TEST_BIN): $(TEST_DIR)/grammar_parser_test.c $(LIB_OBJECTS)
	@mkdir -p $(BIN_DIR)
	@printf '[TEST-CC] grammar_parser_test\n'
	$(CC) \
		$(CPPFLAGS) \
		$(TEST_CFLAGS) \
		$(TEST_DIR)/grammar_parser_test.c \
		$(LIB_OBJECTS) \
		$(LDFLAGS) \
		$(LDLIBS) \
		-o $@

.PHONY: test-grammar
test-grammar: check-ebnf-frontend-sync $(GRAMMAR_TEST_BIN)
	$(GRAMMAR_TEST_BIN) $(GRAMMAR_FIXTURES)

# -----------------------------------------------------------------------------
# Lexer token contract and frontend fuzzing
# -----------------------------------------------------------------------------

FRONTEND_TOKEN_TEST_BIN := $(BIN_DIR)/frontend_token_test
FRONTEND_FUZZ_TEST_BIN := $(BIN_DIR)/frontend_fuzz_test
CONFIG_FUZZ_TEST_BIN := $(BIN_DIR)/config_fuzz_test
ROBUSTNESS_TEST_BIN := $(BIN_DIR)/robustness_test

$(FRONTEND_TOKEN_TEST_BIN): $(TEST_DIR)/frontend_token_test.c $(LIB_OBJECTS) $(SRC_ROOT)/unicode/unicode_xid_table.h
	@mkdir -p $(BIN_DIR)
	@printf '[TEST-CC] frontend_token_test\n'
	$(CC) \
		$(CPPFLAGS) \
		$(TEST_CFLAGS) \
		$(TEST_DIR)/frontend_token_test.c \
		$(LIB_OBJECTS) \
		$(LDFLAGS) \
		$(LDLIBS) \
		-o $@

$(FRONTEND_FUZZ_TEST_BIN): $(TEST_DIR)/frontend_fuzz_test.c $(LIB_OBJECTS)
	@mkdir -p $(BIN_DIR)
	@printf '[TEST-CC] frontend_fuzz_test\n'
	$(CC) \
		$(CPPFLAGS) \
		$(TEST_CFLAGS) \
		$(TEST_DIR)/frontend_fuzz_test.c \
		$(LIB_OBJECTS) \
		$(LDFLAGS) \
		$(LDLIBS) \
		-o $@

$(CONFIG_FUZZ_TEST_BIN): \
	$(TEST_DIR)/config_fuzz_test.c \
	$(OBJ_DIR)/config/config.o \
	$(OBJ_DIR)/filesystem/filesystem.o
	@mkdir -p $(BIN_DIR)
	@printf '[TEST-CC] config_fuzz_test\n'
	$(CC) $(CPPFLAGS) $(TEST_CFLAGS) \
		$(TEST_DIR)/config_fuzz_test.c \
		$(OBJ_DIR)/config/config.o $(OBJ_DIR)/filesystem/filesystem.o \
		$(LDFLAGS) $(LDLIBS) -o $@

$(ROBUSTNESS_TEST_BIN): $(TEST_DIR)/robustness_test.c $(LIB_OBJECTS)
	@mkdir -p $(BIN_DIR)
	@printf '[TEST-CC] robustness_test\n'
	$(CC) $(CPPFLAGS) $(TEST_CFLAGS) \
		$(TEST_DIR)/robustness_test.c \
		$(LIB_OBJECTS) $(LDFLAGS) $(LDLIBS) -o $@

.PHONY: test-frontend-tokens
test-frontend-tokens: $(FRONTEND_TOKEN_TEST_BIN)
	$(FRONTEND_TOKEN_TEST_BIN)

.PHONY: test-frontend-fuzz
test-frontend-fuzz: $(FRONTEND_FUZZ_TEST_BIN)
	$(FRONTEND_FUZZ_TEST_BIN)

.PHONY: test-config-fuzz
test-config-fuzz: $(CONFIG_FUZZ_TEST_BIN)
	$(CONFIG_FUZZ_TEST_BIN)

.PHONY: test-robustness
test-robustness: $(ROBUSTNESS_TEST_BIN)
	$(ROBUSTNESS_TEST_BIN)

FUZZ_SECONDS ?= 60
.PHONY: fuzz-continuous
fuzz-continuous: $(FRONTEND_FUZZ_TEST_BIN) $(BIN_DIR)/import_fuzz_test $(CONFIG_FUZZ_TEST_BIN)
	$(PYTHON) scripts/run-bootstrap-fuzz.py \
		--seconds "$(FUZZ_SECONDS)" \
		--iterations "$${VITTE_FUZZ_ITERATIONS:-1000}" \
		$(FRONTEND_FUZZ_TEST_BIN) $(IMPORT_FUZZ_TEST_BIN) $(CONFIG_FUZZ_TEST_BIN)

.PHONY: check-ebnf-frontend-sync
check-ebnf-frontend-sync:
	$(PYTHON) scripts/check-ebnf-frontend-sync.py

# -----------------------------------------------------------------------------
# Semantic tests
# -----------------------------------------------------------------------------

.PHONY: test-sema
test-sema: $(TARGET_BIN)
	$(TARGET_BIN) compile \
		$(TEST_DIR)/sema_calls/local_param_ok.vit \
		--stop-after-sema \
		--quiet

	$(TARGET_BIN) compile \
		$(TEST_DIR)/contracts_ok.vit \
		--stop-after-sema \
		--quiet

	$(TARGET_BIN) compile \
		$(TEST_DIR)/sema_calls/import_param_ok.vit \
		--stop-after-sema \
		--quiet

	$(TARGET_BIN) compile \
		$(TEST_DIR)/import_visibility/module_ok.vit \
		--stop-after-sema \
		--quiet

	$(TARGET_BIN) compile \
		$(TEST_DIR)/grammar_alignment/ref_param_type_marker_ok.vit \
		--stop-after-sema \
		--quiet

	$(TARGET_BIN) compile \
		$(TEST_DIR)/import_collision/modules_main.vit \
		--stop-after-sema \
		--quiet

	$(TARGET_BIN) compile \
		$(TEST_DIR)/sema_calls/u64_literal_and_recursion_ok.vit \
		--stop-after-sema \
		--quiet

	$(TARGET_BIN) compile \
		$(TEST_DIR)/sema_calls/u8_form_members_and_string_pointer_ok.vit \
		--stop-after-sema \
		--quiet

	$(TARGET_BIN) compile \
		$(TEST_DIR)/sema_operators/logical_ok.vit \
		--stop-after-sema \
		--quiet

	@set +e; \
	output=`$(TARGET_BIN) compile \
		$(TEST_DIR)/import_visibility/glob_private.vit \
		--stop-after-sema \
		--quiet --no-color 2>&1`; \
	rc=$$?; \
	set -e; \
	test "$$rc" -eq 1 && \
	printf '%s\n' "$$output" | grep -F 'E0404' >/dev/null && \
	! printf '%s\n' "$$output" | grep -F 'E0504' >/dev/null

	@set +e; \
	output=`$(TARGET_BIN) compile \
		$(TEST_DIR)/sema_calls/local_param_mismatch.vit \
		--stop-after-sema \
		--quiet --no-color 2>&1`; \
	rc=$$?; \
	set -e; \
	test "$$rc" -eq 1 && \
	printf '%s\n' "$$output" | grep -F 'E0602' >/dev/null && \
	printf '%s\n' "$$output" | grep -F 'expected i64, found string' >/dev/null && \
	printf '%s\n' "$$output" | grep -F '"help":"Argument 1 does not match parameter' >/dev/null

	@set +e; \
	output=`$(TARGET_BIN) compile \
		$(TEST_DIR)/sema_calls/signed_variable_to_u64_mismatch.vit \
		--stop-after-sema \
		--quiet --no-color 2>&1`; \
	rc=$$?; \
	set -e; \
	test "$$rc" -eq 1 && \
	printf '%s\n' "$$output" | grep -F 'E0602' >/dev/null && \
	printf '%s\n' "$$output" | grep -F '"help":"Argument 1 does not match parameter' >/dev/null

# -----------------------------------------------------------------------------
# Import/export tests
# -----------------------------------------------------------------------------

IMPORT_GRAPH_TEST_BIN := $(BIN_DIR)/import_graph_test

$(IMPORT_GRAPH_TEST_BIN): \
	$(TEST_DIR)/import_graph_test.c \
	$(SRC_ROOT)/import/import.c \
	$(SRC_ROOT)/import/import.h
	@mkdir -p $(BIN_DIR)
	@printf '[TEST-CC] import_graph_test\n'
	$(CC) \
		$(CPPFLAGS) \
		$(TEST_CFLAGS) \
		$(TEST_DIR)/import_graph_test.c \
		$(SRC_ROOT)/import/import.c \
		$(LDFLAGS) \
		$(LDLIBS) \
		-o $@

.PHONY: test-imports
test-imports: $(IMPORT_GRAPH_TEST_BIN)
	$(IMPORT_GRAPH_TEST_BIN)

IMPORT_FUZZ_TEST_BIN := $(BIN_DIR)/import_fuzz_test

$(IMPORT_FUZZ_TEST_BIN): \
	$(TEST_DIR)/import_fuzz_test.c \
	$(SRC_ROOT)/import/import.c \
	$(SRC_ROOT)/import/import.h
	@mkdir -p $(BIN_DIR)
	@printf '[TEST-CC] import_fuzz_test\n'
	$(CC) \
		$(CPPFLAGS) \
		$(TEST_CFLAGS) \
		$(TEST_DIR)/import_fuzz_test.c \
		$(SRC_ROOT)/import/import.c \
		$(LDFLAGS) \
		$(LDLIBS) \
		-o $@

.PHONY: test-import-fuzz
test-import-fuzz: $(IMPORT_FUZZ_TEST_BIN)
	$(IMPORT_FUZZ_TEST_BIN)

CONCURRENT_TEST_BIN := $(BIN_DIR)/concurrent_compile_test

$(CONCURRENT_TEST_BIN): \
	$(TEST_DIR)/concurrent_compile_test.c \
	$(LIB_OBJECTS)
	@mkdir -p $(BIN_DIR)
	@printf '[TEST-CC] concurrent_compile_test\n'
	$(CC) \
		$(CPPFLAGS) \
		$(TEST_CFLAGS) \
		-pthread \
		$(TEST_DIR)/concurrent_compile_test.c \
		$(LIB_OBJECTS) \
		$(LDFLAGS) \
		-pthread \
		$(LDLIBS) \
		-o $@

.PHONY: test-concurrent
test-concurrent: $(CONCURRENT_TEST_BIN) $(TARGET_BIN)
	$(CONCURRENT_TEST_BIN) \
		examples/hello.vit \
		$(TEST_DIR)/sema_calls/local_param_ok.vit

# -----------------------------------------------------------------------------
# Large-source compiler stress tests
# -----------------------------------------------------------------------------

SCALE_COMPILER ?= $(TARGET_BIN)
SCALE_TIMEOUT ?= 180
# Keep debug stress inputs large enough to cross scanner boundaries without
# making unoptimized scans exceed the per-process CI timeout. Release gates
# retain the full 48 MiB workload.
SCALE_SOURCE_MIB ?= $(if $(filter release,$(MODE)),48,2)
SCALE_DECLARATIONS ?= 10000

.PHONY: test-scale
test-scale: $(SCALE_COMPILER)
	VITTE_BIN="$(SCALE_COMPILER)" \
		VITTE_SCALE_TIMEOUT="$(SCALE_TIMEOUT)" \
		VITTE_SCALE_SOURCE_MIB="$(SCALE_SOURCE_MIB)" \
		VITTE_SCALE_DECLARATIONS="$(SCALE_DECLARATIONS)" \
		$(PYTHON) $(TEST_DIR)/compiler_scale_test.py

# -----------------------------------------------------------------------------
# Frontend expression statements
# -----------------------------------------------------------------------------

.PHONY: test-frontend
test-frontend: $(TARGET_BIN)
	$(TARGET_BIN) check \
		$(TEST_DIR)/frontend_expr_stmt/expr_stmt_ok.vit \
		--quiet

# -----------------------------------------------------------------------------
# Runtime tests
# -----------------------------------------------------------------------------

RUNTIME_CANDIDATES := \
	$(TEST_DIR)/aggregate_runtime.vit \
	$(TEST_DIR)/contracts_ok.vit \
	$(TEST_DIR)/generic_trait_method_dispatch_runtime.vit \
	$(TEST_DIR)/generic_pick_runtime.vit \
	$(TEST_DIR)/ir_declaration_runtime.vit \
	$(TEST_DIR)/native_fixed_array_runtime.vit \
	$(TEST_DIR)/llvm_array_runtime.vit \
	$(TEST_DIR)/llvm_imports_runtime.vit \
	$(TEST_DIR)/llvm_integer_width_runtime.vit \
	$(TEST_DIR)/llvm_pick_codegen_runtime.vit \
	$(TEST_DIR)/import_collision/modules_main.vit \
	$(TEST_DIR)/mir_loop_targets_runtime.vit \
	$(TEST_DIR)/streaming_summary_runtime.vit \
	$(TEST_DIR)/string_intrinsics_runtime.vit
# Keep this list empty when every runtime fixture is executable.  Individual
# temporary gaps may still be listed here while a backend is being ported.
RUNTIME_KNOWN_GAPS :=
RUNTIME_EXECUTABLE_CANDIDATES := \
	$(filter-out $(RUNTIME_KNOWN_GAPS),$(RUNTIME_CANDIDATES))
RUNTIME_TESTS := $(wildcard $(RUNTIME_EXECUTABLE_CANDIDATES))
RUNTIME_MISSING := $(filter-out $(RUNTIME_TESTS),$(RUNTIME_EXECUTABLE_CANDIDATES))

.PHONY: test-runtime
test-runtime: $(TARGET_BIN)
	@set -e; \
	for file in $(RUNTIME_MISSING); do \
		printf '[SKIP] %s (fixture absent from this checkout)\n' "$$file"; \
	done; \
	for file in $(RUNTIME_KNOWN_GAPS); do \
		if test -f "$$file"; then \
			printf '[KNOWN-GAP] %s (required frontend/compiler support unavailable)\n' "$$file"; \
		else \
			printf '[SKIP] %s (fixture absent from this checkout)\n' "$$file"; \
		fi; \
	done; \
	for file in $(RUNTIME_TESTS); do \
		printf '[RUNTIME] %s\n' "$$file"; \
		$(TARGET_BIN) run "$$file"; \
	done

# Qualified imports must preserve same-named declarations through semantic
# analysis, C generation, and execution in both shipped compiler paths.
.PHONY: test-import-collisions
test-import-collisions: $(TARGET_BIN) $(BOOTSTRAP_TARGET_BIN)
	@set -e; \
	for compiler in "$(TARGET_BIN)" "$(BOOTSTRAP_TARGET_BIN)"; do \
		for fixture in modules_main symbol_alias_main; do \
			printf '[IMPORT-COLLISION] semantic and runtime: %s (%s)\n' "$$compiler" "$$fixture"; \
			"$$compiler" compile $(TEST_DIR)/import_collision/"$$fixture".vit --stop-after-sema --quiet; \
			"$$compiler" run $(TEST_DIR)/import_collision/"$$fixture".vit --quiet --no-color; \
		done; \
	done

.PHONY: test-declaration-features
test-declaration-features: $(TARGET_BIN) $(BOOTSTRAP_TARGET_BIN)
	@set -e; \
	for compiler in "$(TARGET_BIN)" "$(BOOTSTRAP_TARGET_BIN)"; do \
		printf '[DECLARATIONS] values, aliases, and opaque pointers: %s\n' "$$compiler"; \
		"$$compiler" run $(TEST_DIR)/declaration_features_runtime.vit --quiet --no-color; \
		printf '[DECLARATIONS] immutable static rejection: %s\n' "$$compiler"; \
		if output=$$("$$compiler" compile $(TEST_DIR)/static_immutable_write_rejected.vit --stop-after-sema --quiet --no-color 2>&1); then \
			printf 'ERROR: immutable static write unexpectedly compiled\n'; exit 1; \
		fi; \
		case "$$output" in *E0513*) ;; *) printf '%s\n' "$$output"; printf 'ERROR: expected E0513 for immutable static write\n'; exit 1 ;; esac; \
	done

# HIR/IR lowering and C17 backend runtime smoke test. Keep the test on the
# production Vitte path; the former standalone C harness is no longer tracked.
.PHONY: test-ir-backend
test-ir-backend: $(TARGET_BIN)
	$(TARGET_BIN) run $(TEST_DIR)/ir_declaration_runtime.vit

.PHONY: test-mir-control-flow
test-mir-control-flow: $(TARGET_BIN)
	$(TARGET_BIN) run $(TEST_DIR)/mir_loop_targets_runtime.vit

# Fast compiler-focused gate for diagnostics, semantics, lowering/backend, and
# end-to-end driver behavior. Broader stress, fuzz, and sanitizer suites remain
# part of `make test` and `make test-sanitizers`.
.PHONY: test-build-invalidation
test-build-invalidation: $(BOOTSTRAP_TARGET_BIN)
	VITTE_BIN="$(BOOTSTRAP_TARGET_BIN)" $(PYTHON) $(TEST_DIR)/incremental_build_test.py

.PHONY: test-packaging-matrix
test-packaging-matrix:
	$(PYTHON) scripts/test-packaging-matrix.py

# Blocking release criteria: every diagnostic is owned and every native
# frontend action/fallback path has a checked witness in the compiler gate.
.PHONY: compiler-release-criteria
compiler-release-criteria:
	$(PYTHON) scripts/check-release-criteria.py

.PHONY: compiler-power-gate
compiler-power-gate: \
	test-cli-driver \
	test-diagnostic \
	test-diagnostic-golden \
	test-diagnostic-formats \
	test-diagnostic-help \
	test-diagnostic-parity \
	test-bootstrap-regressions \
	test-grammar \
	test-sema \
	test-import-collisions \
	test-declaration-features \
	test-imports \
	test-native-frontend-subset \
	test-native-bootstrap-parity \
	test-ir-backend \
	test-mir-control-flow \
	test-build-invalidation \
	test-packaging-matrix \
	compiler-release-criteria
	@printf '\nCompiler power gate passed.\n'

# Maximum fast local gate: the focused semantic/backend gate, complete
# deterministic compiler suite, and runtime fixtures. Run sequentially because
# these targets share build outputs.
.PHONY: compiler-max-gate-fast
compiler-max-gate-fast:
	$(MAKE) compiler-power-gate
	$(MAKE) test
	$(MAKE) test-runtime
	@printf '\nMaximum fast compiler gate passed.\n'

# Full local reliability gate: strict debug/release verification, runtime
# fixtures, continuous fuzzing, and the sanitizer regression suite.
.PHONY: compiler-gate
compiler-gate: compiler-release-criteria
	$(MAKE) test-build-invalidation test-packaging-matrix
	$(MAKE) verify
	$(MAKE) test-runtime
	$(MAKE) FUZZ_SECONDS=60 fuzz-continuous
	$(MAKE) test-sanitizers
	@printf '\nFull local compiler gate passed.\n'

# Each verified stamp records a completed stage. A failed stage gets no stamp,
# so rerunning its target resumes there while preserving successful earlier
# stages. Source, test, and build-profile inputs invalidate affected stamps.
$(SELFHOST_STAGE0_STAMP): $(TARGET_BIN) $(BOOTSTRAP_TARGET_BIN) \
		$(NATIVE_BOOTSTRAP_PARITY_TEST_BIN) $(NATIVE_FRONTEND_SOURCES) \
		$(NATIVE_MODULE_SOURCES) $(SELFHOST_TEST_INPUTS) \
		VERSION $(SELFHOST_STEP_RUNNER) Makefile
	@mkdir -p "$(SELFHOST_DIR)"
	@printf '\n[SELFHOST] stage0 frontend corpus and parity\n'
	$(SELFHOST_STEP_RUNNER) --stage 0 --step "frontend corpus and parity" $(call SELFHOST_STEP_EVIDENCE,0) \
		--compiler "$(TARGET_BIN)" --compiler "$(BOOTSTRAP_TARGET_BIN)" \
		--reproduce "$(MAKE) MODE=$(MODE) bootstrap-stage1" -- \
		$(MAKE) test-grammar test-native-frontend-subset test-native-bootstrap-parity
	$(SELFHOST_STEP_RUNNER) --stage 0 --step "record stage0 verification" $(call SELFHOST_STEP_EVIDENCE,0) \
		--compiler "$(TARGET_BIN)" --artifact "$@" \
		--reproduce "$(MAKE) MODE=$(MODE) bootstrap-stage1" -- touch "$@"
	@printf '[SELFHOST] stage0 verification passed\n'

$(SELFHOST_STAGE1_C): $(SELFHOST_STAGE0_STAMP) $(TARGET_BIN) \
		$(NATIVE_FRONTEND_SOURCES) $(NATIVE_MODULE_SOURCES) Makefile
	@mkdir -p "$(SELFHOST_DIR)"
	@rm -f "$@.tmp"
	$(SELFHOST_STEP_RUNNER) --stage 1 --step "generate stage1 API C" $(call SELFHOST_STEP_EVIDENCE,1) \
		--compiler "$(TARGET_BIN)" --generated "$@.tmp" \
		--generated "$(SELFHOST_STAGE1_C)" \
		--reproduce "$(MAKE) MODE=$(MODE) bootstrap-stage1" -- \
		VITTE_MODULE_PATH="$(VITTE_MODULE_PATH)$(PATH_LIST_SEPARATOR)$(VITTE_ROOT)" $(TARGET_BIN) \
		compile "$(NATIVE_FRONTEND_ENTRY)" --emit-c --quiet --no-color \
		-o "$@.tmp"
	$(SELFHOST_STEP_RUNNER) --stage 1 --step "publish stage1 API C" $(call SELFHOST_STEP_EVIDENCE,1) \
		--compiler "$(TARGET_BIN)" --generated "$@.tmp" \
		--generated "$(SELFHOST_STAGE1_C)" \
		--reproduce "$(MAKE) MODE=$(MODE) bootstrap-stage1" -- mv "$@.tmp" "$@"

$(SELFHOST_STAGE1_BIN): $(SELFHOST_STAGE1_C) $(BUILD_CONFIG_STAMP) \
		$(SELFHOST_C_INPUTS) Makefile
	$(SELFHOST_STEP_RUNNER) --stage 1 --step "build stage1 compiler" $(call SELFHOST_STEP_EVIDENCE,1) \
		--compiler "$(CC)" --compiler "$(TARGET_BIN)" \
		--generated "$(SELFHOST_STAGE1_C)" --artifact "$(SELFHOST_STAGE1_BIN)" \
		--reproduce "$(MAKE) MODE=$(MODE) bootstrap-stage1" -- \
	$(MAKE) BUILD="$(SELFHOST_STAGE1_BUILD)" \
		NATIVE_FRONTEND_COMPILER="$(abspath $(TARGET_BIN))" \
		NATIVE_FRONTEND_C="$(abspath $(SELFHOST_STAGE1_C))" all
	@touch "$@"

$(SELFHOST_STAGE1_STAMP): $(SELFHOST_STAGE0_STAMP) $(SELFHOST_STAGE1_BIN) \
		$(SELFHOST_TEST_INPUTS) Makefile
	@printf '\n[SELFHOST] stage1 frontend corpus and parity\n'
	$(SELFHOST_STEP_RUNNER) --stage 1 --step "frontend corpus and parity" $(call SELFHOST_STEP_EVIDENCE,1) \
		--compiler "$(SELFHOST_STAGE1_BIN)" --generated "$(SELFHOST_STAGE1_C)" \
		--reproduce "$(MAKE) MODE=$(MODE) bootstrap-stage1" -- \
	$(MAKE) BUILD="$(SELFHOST_STAGE1_BUILD)" \
		NATIVE_FRONTEND_COMPILER="$(abspath $(TARGET_BIN))" \
		NATIVE_FRONTEND_C="$(abspath $(SELFHOST_STAGE1_C))" \
		test-grammar test-native-frontend-subset test-native-bootstrap-parity
	$(SELFHOST_STEP_RUNNER) --stage 1 --step "record stage1 verification" $(call SELFHOST_STEP_EVIDENCE,1) \
		--compiler "$(SELFHOST_STAGE1_BIN)" --generated "$(SELFHOST_STAGE1_C)" \
		--artifact "$@" --reproduce "$(MAKE) MODE=$(MODE) bootstrap-stage1" -- touch "$@"
	@printf '[SELFHOST] stage1 verification passed\n'

$(SELFHOST_STAGE2_C): $(SELFHOST_STAGE1_STAMP) $(SELFHOST_STAGE1_BIN) \
		$(NATIVE_FRONTEND_SOURCES) $(NATIVE_MODULE_SOURCES) Makefile
	@mkdir -p "$(SELFHOST_DIR)"
	@rm -f "$@.tmp"
	$(SELFHOST_STEP_RUNNER) --stage 2 --step "generate stage2 API C" $(call SELFHOST_STEP_EVIDENCE,2) \
		--compiler "$(SELFHOST_STAGE1_BIN)" --generated "$@.tmp" \
		--generated "$(SELFHOST_STAGE2_C)" \
		--reproduce "$(MAKE) MODE=$(MODE) bootstrap-stage2" -- \
		VITTE_MODULE_PATH="$(VITTE_MODULE_PATH)$(PATH_LIST_SEPARATOR)$(VITTE_ROOT)" $(SELFHOST_STAGE1_BIN) \
		compile "$(NATIVE_FRONTEND_ENTRY)" --emit-c --quiet --no-color \
		-o "$@.tmp"
	$(SELFHOST_STEP_RUNNER) --stage 2 --step "publish stage2 API C" $(call SELFHOST_STEP_EVIDENCE,2) \
		--compiler "$(SELFHOST_STAGE1_BIN)" --generated "$@.tmp" \
		--generated "$(SELFHOST_STAGE2_C)" \
		--reproduce "$(MAKE) MODE=$(MODE) bootstrap-stage2" -- mv "$@.tmp" "$@"

$(SELFHOST_STAGE2_BIN): $(SELFHOST_STAGE2_C) $(BUILD_CONFIG_STAMP) \
		$(SELFHOST_C_INPUTS) Makefile
	$(SELFHOST_STEP_RUNNER) --stage 2 --step "build stage2 compiler" $(call SELFHOST_STEP_EVIDENCE,2) \
		--compiler "$(CC)" --compiler "$(SELFHOST_STAGE1_BIN)" \
		--generated "$(SELFHOST_STAGE2_C)" --artifact "$(SELFHOST_STAGE2_BIN)" \
		--reproduce "$(MAKE) MODE=$(MODE) bootstrap-stage2" -- \
	$(MAKE) BUILD="$(SELFHOST_STAGE2_BUILD)" \
		NATIVE_FRONTEND_COMPILER="$(abspath $(SELFHOST_STAGE1_BIN))" \
		NATIVE_FRONTEND_C="$(abspath $(SELFHOST_STAGE2_C))" all
	@touch "$@"

$(SELFHOST_STAGE2_STAMP): $(SELFHOST_STAGE1_STAMP) $(SELFHOST_STAGE2_BIN) \
		$(SELFHOST_TEST_INPUTS) Makefile
	$(SELFHOST_STEP_RUNNER) --stage 2 --step "compare generated stage1 and stage2 API C" $(call SELFHOST_STEP_EVIDENCE,2) \
		--compiler "$(TARGET_BIN)" --compiler "$(SELFHOST_STAGE1_BIN)" \
		--generated "$(SELFHOST_STAGE1_C)" --generated "$(SELFHOST_STAGE2_C)" \
		--reproduce "$(MAKE) MODE=$(MODE) bootstrap-stage2" -- \
	cmp -s "$(SELFHOST_STAGE1_C)" "$(SELFHOST_STAGE2_C)"
	@printf '\n[SELFHOST] stage0 and stage1 API output match\n'
	@printf '[SELFHOST] stage2 frontend corpus and parity\n'
	$(SELFHOST_STEP_RUNNER) --stage 2 --step "frontend corpus and parity" $(call SELFHOST_STEP_EVIDENCE,2) \
		--compiler "$(SELFHOST_STAGE2_BIN)" --generated "$(SELFHOST_STAGE2_C)" \
		--reproduce "$(MAKE) MODE=$(MODE) bootstrap-stage2" -- \
	$(MAKE) BUILD="$(SELFHOST_STAGE2_BUILD)" \
		NATIVE_FRONTEND_COMPILER="$(abspath $(SELFHOST_STAGE1_BIN))" \
		NATIVE_FRONTEND_C="$(abspath $(SELFHOST_STAGE2_C))" \
		test-grammar test-native-frontend-subset test-native-bootstrap-parity
	$(SELFHOST_STEP_RUNNER) --stage 2 --step "record stage2 verification" $(call SELFHOST_STEP_EVIDENCE,2) \
		--compiler "$(SELFHOST_STAGE2_BIN)" --generated "$(SELFHOST_STAGE2_C)" \
		--artifact "$@" --reproduce "$(MAKE) MODE=$(MODE) bootstrap-stage2" -- touch "$@"
	@printf '[SELFHOST] stage2 verification passed\n'

$(SELFHOST_STAGE3_C): $(SELFHOST_STAGE2_STAMP) $(SELFHOST_STAGE2_BIN) \
		$(NATIVE_FRONTEND_SOURCES) $(NATIVE_MODULE_SOURCES) Makefile
	@mkdir -p "$(SELFHOST_DIR)"
	@rm -f "$@.tmp"
	$(SELFHOST_STEP_RUNNER) --stage 3 --step "generate stage3 API C" $(call SELFHOST_STEP_EVIDENCE,3) \
		--compiler "$(SELFHOST_STAGE2_BIN)" --generated "$@.tmp" \
		--generated "$(SELFHOST_STAGE3_C)" \
		--reproduce "$(MAKE) MODE=$(MODE) bootstrap-stage3" -- \
		VITTE_MODULE_PATH="$(VITTE_MODULE_PATH)$(PATH_LIST_SEPARATOR)$(VITTE_ROOT)" $(SELFHOST_STAGE2_BIN) \
		compile "$(NATIVE_FRONTEND_ENTRY)" --emit-c --quiet --no-color \
		-o "$@.tmp"
	$(SELFHOST_STEP_RUNNER) --stage 3 --step "publish stage3 API C" $(call SELFHOST_STEP_EVIDENCE,3) \
		--compiler "$(SELFHOST_STAGE2_BIN)" --generated "$@.tmp" \
		--generated "$(SELFHOST_STAGE3_C)" \
		--reproduce "$(MAKE) MODE=$(MODE) bootstrap-stage3" -- mv "$@.tmp" "$@"

$(SELFHOST_STAGE3_BIN): $(SELFHOST_STAGE3_C) $(BUILD_CONFIG_STAMP) \
		$(SELFHOST_C_INPUTS) Makefile
	$(SELFHOST_STEP_RUNNER) --stage 3 --step "build stage3 compiler" $(call SELFHOST_STEP_EVIDENCE,3) \
		--compiler "$(CC)" --compiler "$(SELFHOST_STAGE2_BIN)" \
		--generated "$(SELFHOST_STAGE3_C)" --artifact "$(SELFHOST_STAGE3_BIN)" \
		--reproduce "$(MAKE) MODE=$(MODE) bootstrap-stage3" -- \
	$(MAKE) BUILD="$(SELFHOST_STAGE3_BUILD)" \
		NATIVE_FRONTEND_COMPILER="$(abspath $(SELFHOST_STAGE2_BIN))" \
		NATIVE_FRONTEND_C="$(abspath $(SELFHOST_STAGE3_C))" all
	@touch "$@"

$(SELFHOST_STAGE3_STAMP): $(SELFHOST_STAGE2_STAMP) $(SELFHOST_STAGE3_BIN) \
		$(SELFHOST_TEST_INPUTS) Makefile
	$(SELFHOST_STEP_RUNNER) --stage 3 --step "compare generated stage2 and stage3 API C" $(call SELFHOST_STEP_EVIDENCE,3) \
		--compiler "$(SELFHOST_STAGE1_BIN)" --compiler "$(SELFHOST_STAGE2_BIN)" \
		--generated "$(SELFHOST_STAGE2_C)" --generated "$(SELFHOST_STAGE3_C)" \
		--reproduce "$(MAKE) MODE=$(MODE) bootstrap-stage3" -- \
	cmp -s "$(SELFHOST_STAGE2_C)" "$(SELFHOST_STAGE3_C)"
	@printf '\n[SELFHOST] stage1 and stage2 API output match\n'
	@printf '[SELFHOST] stage3 frontend corpus and parity\n'
	$(SELFHOST_STEP_RUNNER) --stage 3 --step "frontend corpus and parity" $(call SELFHOST_STEP_EVIDENCE,3) \
		--compiler "$(SELFHOST_STAGE3_BIN)" --generated "$(SELFHOST_STAGE3_C)" \
		--reproduce "$(MAKE) MODE=$(MODE) bootstrap-stage3" -- \
	$(MAKE) BUILD="$(SELFHOST_STAGE3_BUILD)" \
		NATIVE_FRONTEND_COMPILER="$(abspath $(SELFHOST_STAGE2_BIN))" \
		NATIVE_FRONTEND_C="$(abspath $(SELFHOST_STAGE3_C))" \
		test-grammar test-native-frontend-subset test-native-bootstrap-parity
	$(SELFHOST_STEP_RUNNER) --stage 3 --step "record stage3 verification" $(call SELFHOST_STEP_EVIDENCE,3) \
		--compiler "$(SELFHOST_STAGE3_BIN)" --generated "$(SELFHOST_STAGE3_C)" \
		--artifact "$@" --reproduce "$(MAKE) MODE=$(MODE) bootstrap-stage3" -- touch "$@"
	@printf '[SELFHOST] stage3 verification passed\n'

.PHONY: bootstrap-stage1 bootstrap-stage2 bootstrap-stage3
bootstrap-stage1: $(SELFHOST_STAGE1_STAMP)
	@printf '\nBootstrap stage 1 is built and verified.\n'

bootstrap-stage2: $(SELFHOST_STAGE2_STAMP)
	@printf '\nBootstrap stage 2 is built and verified.\n'

bootstrap-stage3: $(SELFHOST_STAGE3_STAMP)
	@printf '\nBootstrap stage 3 is built and verified.\n'

# Rebuild the native frontend through stage3; already verified stages are reused.
.PHONY: compiler-selfhost-gate
compiler-selfhost-gate: $(SELFHOST_STAGE3_STAMP)
	@printf '\nNative compiler self-host gate passed through stage3.\n'

# User-facing name for the complete bootstrap/self-host cycle.
.PHONY: bootstrap-selfhost
bootstrap-selfhost: compiler-selfhost-gate

# -----------------------------------------------------------------------------
# Complete test suite
# -----------------------------------------------------------------------------

.PHONY: test
test: \
	test-cli-driver \
	test-source \
	test-diagnostic \
	test-diagnostic-fuzz \
	test-diagnostic-golden \
	test-diagnostic-formats \
	test-diagnostic-help \
	test-diagnostic-parity \
	test-bootstrap-regressions \
	test-source-map \
	test-grammar \
	test-frontend-tokens \
	test-frontend-fuzz \
	test-config-fuzz \
	test-robustness \
	test-sema \
	test-import-collisions \
	test-native-frontend-subset \
	test-imports \
	test-import-fuzz \
	test-concurrent \
	test-scale \
	test-frontend \
	test-corpus
	@printf '\nVitte test suite completed successfully.\n'

# -----------------------------------------------------------------------------
# Sanitizers
# -----------------------------------------------------------------------------

.PHONY: test-sanitizers
test-sanitizers:
	$(MAKE) clean
	$(MAKE) MODE=asan -j4 all
	$(MAKE) MODE=asan \
		test-source \
		test-import-fuzz \
		test-concurrent \
		test-diagnostic \
		test-diagnostic-fuzz \
		test-frontend-tokens \
		test-frontend-fuzz \
		test-config-fuzz \
		test-robustness \
		test-diagnostic-golden
	@printf '\nASan/UBSan tests completed successfully.\n'

# -----------------------------------------------------------------------------
# Strict release verification
# -----------------------------------------------------------------------------

.PHONY: verify
verify:
	$(MAKE) clean
	$(MAKE) MODE=debug all
	$(MAKE) check
	$(MAKE) test
	$(MAKE) clean
	$(MAKE) MODE=release all
	$(MAKE) version
	$(MAKE) cli-help
	@printf '\nVitte verification completed successfully.\n'

# -----------------------------------------------------------------------------
# Build information
# -----------------------------------------------------------------------------

.PHONY: info
info:
	@printf 'Project:        %s\n' "$(PROJECT)"
	@printf 'Target:         %s\n' "$(TARGET_BIN)"
	@printf 'Compiler:       %s\n' "$(CC)"
	@printf 'Mode:           %s\n' "$(MODE)"
	@printf 'Host OS:        %s\n' "$(UNAME_S)"
	@printf 'Vitte root:     %s\n' "$(VITTE_ROOT)"
	@printf 'Source root:    %s\n' "$(SRC_ROOT)"
	@printf 'Runtime root:   %s\n' "$(RUNTIME_ROOT)"
	@printf 'Test root:      %s\n' "$(TEST_DIR)"
	@printf 'Build root:     %s\n' "$(BUILD)"
	@printf 'C sources:      %s\n' "$(words $(SOURCES))"
	@printf 'Objects:        %s\n' "$(words $(OBJECTS))"
	@printf 'Test .vit:      %s\n' \
		"$(shell find $(TEST_DIR) -type f -name '*.vit' | wc -l | tr -d ' ')"

# -----------------------------------------------------------------------------
# Binary information
# -----------------------------------------------------------------------------

.PHONY: binary-info
binary-info: $(TARGET_BIN)
	@printf 'Binary:\n'
	@ls -lh $(TARGET_BIN)
	@printf '\nFile information:\n'
	@file $(TARGET_BIN)
	@printf '\nVersion:\n'
	@$(TARGET_BIN) --version

# -----------------------------------------------------------------------------
# Installation
# -----------------------------------------------------------------------------

PREFIX ?= /usr/local
BINDIR ?= $(PREFIX)/bin

.PHONY: install
install: $(TARGET_BIN)
	install -d $(DESTDIR)$(BINDIR)
	install -m 755 \
		$(TARGET_BIN) \
		$(DESTDIR)$(BINDIR)/$(TARGET)
	$(MAKE) -C modules/json install \
		PREFIX="$(PREFIX)" \
		DESTDIR="$(DESTDIR)"

.PHONY: uninstall
uninstall:
	rm -f $(DESTDIR)$(BINDIR)/$(TARGET)
	$(MAKE) -C modules/json uninstall \
		PREFIX="$(PREFIX)" \
		DESTDIR="$(DESTDIR)"

# -----------------------------------------------------------------------------
# Cleaning
# -----------------------------------------------------------------------------

.PHONY: clean
clean:
	rm -rf $(BUILD)

.PHONY: clean-target
clean-target:
	rm -rf target

.PHONY: distclean
distclean: clean clean-target

# =============================================================================
# macOS builds
# =============================================================================

MACOS_CC ?= clang

MACOS_ARM64_DIR    := target/macos-arm64
MACOS_X86_64_DIR   := target/macos-x86_64
MACOS_UNIVERSAL_DIR := target/universal

MACOS_ARM64_BIN    := $(MACOS_ARM64_DIR)/vitte
MACOS_X86_64_BIN   := $(MACOS_X86_64_DIR)/vitte
MACOS_UNIVERSAL_BIN := $(MACOS_UNIVERSAL_DIR)/vitte

# Historical Mac OS X 10.4 / iMac 2006 profile.  The compiler and SDK are
# intentionally overridable because modern Xcode installations no longer ship
# an i386-capable toolchain or the 10.4 SDK.
MACOS2006_DIR := target/macos2006-i386
MACOS2006_BIN := $(MACOS2006_DIR)/vitte
MACOS2006_CC ?= gcc-4.0
MACOS2006_SDK ?= MacOSX10.4u.sdk
MACOS2006_DEPLOYMENT_TARGET ?= 10.4

ifneq ($(strip $(VITTE_MACOS_LEGACY_CC)),)
MACOS2006_CC := $(VITTE_MACOS_LEGACY_CC)
endif
ifneq ($(strip $(VITTE_MACOS_LEGACY_SDK)),)
MACOS2006_SDK := $(VITTE_MACOS_LEGACY_SDK)
endif
ifneq ($(strip $(MACOSX_DEPLOYMENT_TARGET)),)
MACOS2006_DEPLOYMENT_TARGET := $(MACOSX_DEPLOYMENT_TARGET)
endif
ifneq ($(strip $(VITTE_MACOS_LEGACY_OUT)),)
MACOS2006_BIN := $(VITTE_MACOS_LEGACY_OUT)
endif

MACOS_COMMON_FLAGS := \
	$(CPPFLAGS) \
	$(CFLAGS_COMMON) \
	-O2 \
	-DNDEBUG

.PHONY: macos-arm64-bin
macos-arm64-bin:
	@printf 'Building Vitte for macOS arm64...\n'
	@mkdir -p $(MACOS_ARM64_DIR)
	$(MACOS_CC) \
		$(MACOS_COMMON_FLAGS) \
		-arch arm64 \
		-mmacosx-version-min=11.0 \
		$(SOURCES) \
		$(LDLIBS) \
		-o $(MACOS_ARM64_BIN)
	@printf 'Architectures: '
	@lipo -archs $(MACOS_ARM64_BIN)
	@printf 'Built: %s\n' "$(MACOS_ARM64_BIN)"

.PHONY: macos-x86_64-bin
macos-x86_64-bin:
	@printf 'Building Vitte for macOS x86_64...\n'
	@mkdir -p $(MACOS_X86_64_DIR)
	$(MACOS_CC) \
		$(MACOS_COMMON_FLAGS) \
		-arch x86_64 \
		-mmacosx-version-min=10.13 \
		$(SOURCES) \
		$(LDLIBS) \
		-o $(MACOS_X86_64_BIN)
	@printf 'Architectures: '
	@lipo -archs $(MACOS_X86_64_BIN)
	@printf 'Built: %s\n' "$(MACOS_X86_64_BIN)"

.PHONY: macos-universal-bin
macos-universal-bin: macos-arm64-bin macos-x86_64-bin
	@printf 'Creating universal Vitte binary...\n'
	@mkdir -p $(MACOS_UNIVERSAL_DIR)
	lipo -create \
		$(MACOS_ARM64_BIN) \
		$(MACOS_X86_64_BIN) \
		-output $(MACOS_UNIVERSAL_BIN)
	@printf 'Architectures: '
	@lipo -archs $(MACOS_UNIVERSAL_BIN)
	@printf 'Binary: '
	@file $(MACOS_UNIVERSAL_BIN)
	@printf 'Built: %s\n' "$(MACOS_UNIVERSAL_BIN)"

.PHONY: macos2006-i386-bin
macos2006-i386-bin:
	@printf 'Building Vitte for Mac OS X 2006 i386...\n'
	@mkdir -p $(dir $(MACOS2006_BIN))
	$(MACOS2006_CC) \
		$(MACOS_COMMON_FLAGS) \
		-arch i386 \
		-isysroot $(MACOS2006_SDK) \
		-mmacosx-version-min=$(MACOS2006_DEPLOYMENT_TARGET) \
		$(SOURCES) \
		$(LDLIBS) \
		-o $(MACOS2006_BIN)
	@printf 'Architectures: '
	@lipo -archs $(MACOS2006_BIN)
	@printf 'Built: %s\n' "$(MACOS2006_BIN)"

.PHONY: macos-verify
macos-verify: macos-arm64-bin
	@printf '\nVerifying macOS arm64 binary...\n'
	@file $(MACOS_ARM64_BIN)
	@lipo -archs $(MACOS_ARM64_BIN)
	@$(MACOS_ARM64_BIN) --version
	@$(MACOS_ARM64_BIN) --help >/dev/null
	@printf 'macOS arm64 binary verified.\n'

# =============================================================================
# Help
# =============================================================================

.PHONY: docs-site docs-site-check
docs-site:
	python3 scripts/generate_site_audits.py --write

docs-site-check:
	python3 scripts/generate_site_audits.py --check

.PHONY: help
help:
	@printf '%s\n' \
		'Vitte build system' \
		'' \
		'Build:' \
		'  make                         Build debug compiler' \
		'  make bootstrap               Build only the C seed compiler' \
		'  make bootstrap-archive       Archive the historical C seed modules' \
		'  make debug                   Build debug compiler' \
		'  make release                 Build optimized compiler' \
		'  make asan                    Build with ASan + UBSan' \
		'  make ubsan                   Build with UBSan' \
		'' \
		'Compiler:' \
		'  make run                     Run compiler' \
		'  make version                 Run vitte --version' \
		'  make cli-help                Run vitte --help' \
		'  make compile FILE=x.vit      Run vitte build' \
		'  make vitte-check FILE=x.vit  Run vitte check' \
		'  make emit-c FILE=x.vit       Generate C17 source' \
		'  make native-frontend         Compile the native frontend archive' \
		'                               (also linked by the default build)' \
		'  make bootstrap-check         Verify seed startup, imports and diagnostics' \
		'  make bootstrap-stage1        Build and verify through stage 1' \
		'  make bootstrap-stage2        Build and verify through stage 2' \
		'  make bootstrap-stage3        Build and verify through stage 3' \
		'                               (successful earlier stages are reused)' \
		'  make MODE=release bootstrap-selfhost' \
		'                               Verify the complete stages 0 through 3' \
		'' \
		'Verification:' \
		'  make docs-site               Regenerate static site audits and metadata' \
		'  make docs-site-check         Check generated site audits and metadata' \
		'  make check                   Syntax-check every C source' \
		'  make test                    Run complete test suite' \
		'  make test-corpus             Run corpus tests' \
		'  make test-grammar            Run grammar tests' \
		'  make test-sema               Run semantic tests' \
		'  make test-imports            Run import/export tests' \
		'  make test-import-fuzz        Run import graph fuzzing tests' \
		'  make test-config-fuzz        Fuzz configuration adapters' \
		'  make test-robustness         Check allocation, size and path limits' \
		'  make fuzz-continuous         Repeat lexer/parser/import/config fuzzing' \
		'  make test-concurrent        Run repeated concurrent compilation tests' \
		'  make test-scale              Compile large Vitte source stress cases' \
		'  make test-runtime            Build runtime test programs' \
		'  make test-diagnostic-parity  Compare diagnostic output formats and explain codes' \
		'  make test-build-invalidation Check incremental rebuild dependencies' \
		'  make test-packaging-matrix     Check cross-platform package targets' \
		'  make compiler-release-criteria Require diagnostic and compiler-path ownership' \
		'  make compiler-power-gate       Run the focused compiler reliability gate' \
		'  make test-sanitizers         Run sanitizer tests' \
		'  make verify                  Debug + tests + release verification' \
		'' \
		'macOS:' \
		'  make macos-arm64-bin         Build native Apple Silicon binary' \
		'  make macos-x86_64-bin        Build Intel macOS binary' \
		'  make macos-universal-bin     Build universal arm64+x86_64 binary' \
		'  make macos-verify            Verify Apple Silicon binary' \
		'' \
		'Utility:' \
		'  make info                    Show build information' \
		'  make binary-info             Inspect generated compiler' \
		'  make install                 Install Vitte' \
		'  make uninstall               Uninstall Vitte' \
		'  make clean                   Remove build/' \
		'  make clean-target            Remove target/' \
		'  make distclean               Remove all generated build files'
