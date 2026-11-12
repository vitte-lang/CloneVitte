# ============================================================================
# Vitte JSON Module
# ============================================================================
#
# Build, validation and test orchestration for the standalone json package.
#
# The JSON implementation itself is written in Vitte.
#
# Usage:
#
#   make
#   make check
#   make test
#   make test-vitte
#   make install PREFIX=/usr/local
#   make uninstall PREFIX=/usr/local
#   make verify
#   make clean
#
# Override the compiler if necessary:
#
#   make VITTE=/path/to/vitte
#
# ============================================================================

SHELL := /bin/sh

.DEFAULT_GOAL := check

# ----------------------------------------------------------------------------
# Module metadata
# ----------------------------------------------------------------------------

MODULE_NAME    := json
MODULE_VERSION := 0.1.1

ROOT_DIR := .
BUILD_DIR := $(ROOT_DIR)/build
TMP_DIR   := $(BUILD_DIR)/tmp
LOG_DIR   := $(BUILD_DIR)/logs
INSTALL_TEST_ROOT := $(abspath $(BUILD_DIR)/installed-test-root)
INSTALL_TEST_PREFIX := /opt/vitte
INSTALL_TEST_MODULE_ROOT := $(INSTALL_TEST_ROOT)$(INSTALL_TEST_PREFIX)/share/vitte/modules

# ----------------------------------------------------------------------------
# Vitte compiler
# ----------------------------------------------------------------------------

VITTE ?= $(if $(wildcard ../../build/bin/vitte),../../build/bin/vitte,$(shell command -v vitte 2>/dev/null))

VITTE_FLAGS ?=

VITTE_CHECK_FLAGS ?=
VITTE_TEST_FLAGS  ?=

# ----------------------------------------------------------------------------
# Commands
# ----------------------------------------------------------------------------

RM    ?= rm -f
RMRF  ?= rm -rf
MKDIR ?= mkdir -p
FIND  ?= find
SORT  ?= sort
GREP  ?= grep
SED   ?= sed
PRINTF ?= printf

# ----------------------------------------------------------------------------
# Sources
# ----------------------------------------------------------------------------

CORE_SOURCES := \
	core/array.vit \
	core/number.vit \
	core/object.vit \
	core/value.vit

ENCODE_SOURCES := \
	encode/encoder.vit

ERROR_SOURCES := \
	error/error.vit \
	error/position.vit

LEXER_SOURCES := \
	lexer/escape.vit \
	lexer/lexer.vit \
	lexer/token.vit

PARSER_SOURCES := \
	parser/parser.vit

# The unexported parser/array.vit, parser/object.vit, parser/value.vit, and
# encode/escape.vit drafts are retained in the checkout but not shipped.

RUNTIME_SOURCES := \
	runtime/numeric.vit

LIBRARY_SOURCES := \
	json.vit \
	$(CORE_SOURCES) \
	$(ERROR_SOURCES) \
	$(RUNTIME_SOURCES) \
	$(LEXER_SOURCES) \
	$(PARSER_SOURCES) \
	$(ENCODE_SOURCES)

# ----------------------------------------------------------------------------
# Tests
# ----------------------------------------------------------------------------

TEST_ENCODE    := tests/encode.vit
TEST_INVALID   := tests/invalid.vit
TEST_PARSE     := tests/parse.vit
TEST_ROUNDTRIP := tests/roundtrip.vit
TEST_STRING_CONCAT := tests/string_concat.vit
TEST_UNICODE   := tests/unicode.vit

JSON_TEST_SOURCES := \
	$(TEST_ENCODE) \
	$(TEST_INVALID) \
	$(TEST_PARSE) \
	$(TEST_ROUNDTRIP) \
	$(TEST_STRING_CONCAT) \
	$(TEST_UNICODE) \
	tests/import_smoke.vit

RUNTIME_TEST_SOURCES := \
	runtime/test_numeric.vit

TEST_SOURCES := \
	$(RUNTIME_TEST_SOURCES) \
	$(JSON_TEST_SOURCES)

ALL_SOURCES := \
	$(LIBRARY_SOURCES) \
	$(TEST_SOURCES)

# ----------------------------------------------------------------------------
# Generated lists
# ----------------------------------------------------------------------------

DISCOVERED_VIT_SOURCES := $(shell \
	$(FIND) . \
		-type f \
		-name '*.vit' \
		-not -path './build/*' \
		-print 2>/dev/null | \
	$(SORT))

# ----------------------------------------------------------------------------
# Formatting
# ----------------------------------------------------------------------------

define heading
	@$(PRINTF) '\n==> %s\n' "$(1)"
endef

define success
	@$(PRINTF) '    OK  %s\n' "$(1)"
endef

define failure
	@$(PRINTF) '    FAIL %s\n' "$(1)"
endef

# ----------------------------------------------------------------------------
# Phony targets
# ----------------------------------------------------------------------------

.PHONY: \
	all \
	check \
	check-compiler \
	check-layout \
	check-sources \
	check-library \
	check-core \
	check-error \
	check-runtime \
	check-lexer \
	check-parser \
	check-encode \
	check-tests \
	test \
	test-unit \
	test-json \
	test-vitte \
	test-runtime \
	test-encode \
	test-invalid \
	test-parse \
	test-roundtrip \
	test-string-concat \
	test-unicode \
	verify \
	smoke \
	list \
	list-library \
	list-tests \
	info \
	stats \
	install \
	uninstall \
	clean \
	distclean \
	help

# ----------------------------------------------------------------------------
# Default
# ----------------------------------------------------------------------------

all: check

# ----------------------------------------------------------------------------
# Build directories
# ----------------------------------------------------------------------------

$(BUILD_DIR):
	@$(MKDIR) "$(BUILD_DIR)"

$(TMP_DIR): | $(BUILD_DIR)
	@$(MKDIR) "$(TMP_DIR)"

$(LOG_DIR): | $(BUILD_DIR)
	@$(MKDIR) "$(LOG_DIR)"

# ----------------------------------------------------------------------------
# Compiler availability
# ----------------------------------------------------------------------------

check-compiler:
	$(call heading,Checking Vitte compiler)
	@if [ ! -x "$(VITTE)" ]; then \
		$(PRINTF) 'Vitte compiler not found or not executable:\n'; \
		$(PRINTF) '  %s\n\n' "$(VITTE)"; \
		$(PRINTF) 'Override it with:\n'; \
		$(PRINTF) '  make VITTE=/path/to/vitte\n'; \
		exit 1; \
	fi
	$(call success,$(VITTE))

# ----------------------------------------------------------------------------
# Layout validation
# ----------------------------------------------------------------------------

check-layout:
	$(call heading,Checking module layout)
	@set -e; \
	for directory in \
		core \
		encode \
		error \
		lexer \
		parser \
		runtime \
		tests; \
	do \
		if [ ! -d "$$directory" ]; then \
			$(PRINTF) 'Missing directory: %s\n' "$$directory"; \
			exit 1; \
		fi; \
	done
	$(call success,module directories)

# ----------------------------------------------------------------------------
# Source existence
# ----------------------------------------------------------------------------

check-sources:
	$(call heading,Checking expected source files)
	@set -e; \
	for source in $(ALL_SOURCES); do \
		if [ ! -f "$$source" ]; then \
			$(PRINTF) 'Missing source: %s\n' "$$source"; \
			exit 1; \
		fi; \
	done
	$(call success,expected sources)

# ----------------------------------------------------------------------------
# Generic source checker
# ----------------------------------------------------------------------------

define run_check
	@set -e; \
	for source in $(1); do \
		$(PRINTF) '    CHECK %s\n' "$$source"; \
		"$(VITTE)" check $(VITTE_FLAGS) $(VITTE_CHECK_FLAGS) "$$source"; \
	done
endef

# ----------------------------------------------------------------------------
# Library checks
# ----------------------------------------------------------------------------

check-core: check-compiler check-sources
	$(call heading,Checking core)
	$(call run_check,$(CORE_SOURCES))

check-error: check-compiler check-sources
	$(call heading,Checking error subsystem)
	$(call run_check,$(ERROR_SOURCES))

check-runtime: check-compiler check-sources
	$(call heading,Checking runtime)
	$(call run_check,$(RUNTIME_SOURCES))

check-lexer: check-compiler check-sources
	$(call heading,Checking lexer)
	$(call run_check,$(LEXER_SOURCES))

check-parser: check-compiler check-sources
	$(call heading,Checking parser)
	$(call run_check,$(PARSER_SOURCES))

check-encode: check-compiler check-sources
	$(call heading,Checking encoder)
	$(call run_check,$(ENCODE_SOURCES))

check-library: \
	check-core \
	check-error \
	check-runtime \
	check-lexer \
	check-parser \
	check-encode
	$(call heading,Library validation complete)
	$(call success,$(MODULE_NAME))

# ----------------------------------------------------------------------------
# Test source checks
# ----------------------------------------------------------------------------

check-tests: check-compiler check-sources
	$(call heading,Checking test sources)
	$(call run_check,$(TEST_SOURCES))
	$(call success,test sources)

# ----------------------------------------------------------------------------
# Complete static validation
# ----------------------------------------------------------------------------

check: \
	check-layout \
	check-sources \
	check-compiler
	$(call heading,Checking package entry module)
	@"$(VITTE)" check $(VITTE_FLAGS) json.vit
	$(call heading,JSON module check complete)
	$(call success,$(MODULE_NAME) package layout)

# ----------------------------------------------------------------------------
# Generic test runner
# ----------------------------------------------------------------------------

define run_test
	@$(PRINTF) '    COMPILE TEST %s\n' "$(1)"
	@$(MKDIR) "$(BUILD_DIR)/tests"
	@"$(VITTE)" compile $(VITTE_FLAGS) $(VITTE_TEST_FLAGS) \
		"$(1)" \
		-o "$(BUILD_DIR)/tests/$(notdir $(basename $(1)))"
	@"$(BUILD_DIR)/tests/$(notdir $(basename $(1)))"
endef

# ----------------------------------------------------------------------------
# Runtime tests
# ----------------------------------------------------------------------------

test-runtime: check-compiler check-sources
	$(call heading,Running numeric runtime tests)
	$(call run_test,runtime/test_numeric.vit)
	$(call success,runtime tests)

# ----------------------------------------------------------------------------
# Consumer import smoke test
# ----------------------------------------------------------------------------

test-vitte: check-compiler check-sources
	$(call heading,Resolving an installed JSON module from a consumer)
	@$(MAKE) install \
		PREFIX="$(INSTALL_TEST_PREFIX)" \
		DESTDIR="$(INSTALL_TEST_ROOT)"
	@$(MKDIR) "$(INSTALL_TEST_ROOT)$(INSTALL_TEST_PREFIX)/bin"
	@install -m 755 "$(VITTE)" \
		"$(INSTALL_TEST_ROOT)$(INSTALL_TEST_PREFIX)/bin/vitte"
	@cp tests/import_smoke.vit "$(INSTALL_TEST_MODULE_ROOT)/consumer.vit"
	@"$(INSTALL_TEST_ROOT)$(INSTALL_TEST_PREFIX)/bin/vitte" \
		compile "$(INSTALL_TEST_MODULE_ROOT)/consumer.vit" \
		-o "$(INSTALL_TEST_MODULE_ROOT)/consumer"
	@"$(INSTALL_TEST_MODULE_ROOT)/consumer"
	$(call success,installed JSON parser and encoder consumer compiled and ran)

# ----------------------------------------------------------------------------
# Encoder tests
# ----------------------------------------------------------------------------

test-encode: check-compiler check-sources
	$(call heading,Running encoder tests)
	$(call run_test,$(TEST_ENCODE))
	$(call success,encode tests)

test-string-concat: check-compiler check-sources
	$(call heading,Running string concatenation test)
	$(call run_test,$(TEST_STRING_CONCAT))
	$(call success,string concatenation)

# ----------------------------------------------------------------------------
# Invalid JSON tests
# ----------------------------------------------------------------------------

test-invalid: check-compiler check-sources
	$(call heading,Running invalid JSON tests)
	$(call run_test,$(TEST_INVALID))
	$(call success,invalid JSON tests)

# ----------------------------------------------------------------------------
# Parser tests
# ----------------------------------------------------------------------------

test-parse: check-compiler check-sources
	$(call heading,Running parser tests)
	$(call run_test,$(TEST_PARSE))
	$(call success,parser tests)

# ----------------------------------------------------------------------------
# Round-trip tests
# ----------------------------------------------------------------------------

test-roundtrip: check-compiler check-sources
	$(call heading,Running round-trip tests)
	$(call run_test,$(TEST_ROUNDTRIP))
	$(call success,round-trip tests)

# ----------------------------------------------------------------------------
# Unicode tests
# ----------------------------------------------------------------------------

test-unicode: check-compiler check-sources
	$(call heading,Running Unicode tests)
	$(call run_test,$(TEST_UNICODE))
	$(call success,Unicode tests)

# ----------------------------------------------------------------------------
# Test groups
# ----------------------------------------------------------------------------

test-unit: test-vitte

test-json: test-vitte

test: test-vitte test-string-concat
	$(call heading,JSON module smoke test passed)
	$(call success,installed module import)

# ----------------------------------------------------------------------------
# Smoke test
# ----------------------------------------------------------------------------

smoke: check-compiler check-sources
	$(call heading,Running smoke tests)
	@"$(VITTE)" check $(VITTE_FLAGS) json.vit
	$(call success,smoke tests)

# ----------------------------------------------------------------------------
# Installation
# ----------------------------------------------------------------------------

PREFIX ?= /usr/local
MODULE_DIR ?= $(PREFIX)/share/vitte/modules/json

install:
	@$(MKDIR) "$(DESTDIR)$(MODULE_DIR)"
	@set -e; \
	for source in $(LIBRARY_SOURCES); do \
		$(MKDIR) "$(DESTDIR)$(MODULE_DIR)/$$(dirname "$$source")"; \
		cp "$$source" "$(DESTDIR)$(MODULE_DIR)/$$source"; \
	done
	@cp package.toml "$(DESTDIR)$(MODULE_DIR)/package.toml"

uninstall:
	@$(RMRF) "$(DESTDIR)$(MODULE_DIR)"

# ----------------------------------------------------------------------------
# Verification
# ----------------------------------------------------------------------------

verify: \
	check \
	test
	$(call heading,Verification complete)
	$(call success,$(MODULE_NAME) $(MODULE_VERSION))

# ----------------------------------------------------------------------------
# Source inventory
# ----------------------------------------------------------------------------

list:
	$(call heading,All discovered Vitte sources)
	@$(FIND) . \
		-type f \
		-name '*.vit' \
		-not -path './build/*' \
		-print | \
	$(SORT)

list-library:
	$(call heading,Library sources)
	@for source in $(LIBRARY_SOURCES); do \
		$(PRINTF) '%s\n' "$$source"; \
	done

list-tests:
	$(call heading,Test sources)
	@for source in $(TEST_SOURCES); do \
		$(PRINTF) '%s\n' "$$source"; \
	done

# ----------------------------------------------------------------------------
# Statistics
# ----------------------------------------------------------------------------

stats:
	$(call heading,JSON module statistics)
	@library_count=0; \
	test_count=0; \
	total_count=0; \
	for source in $(LIBRARY_SOURCES); do \
		if [ -f "$$source" ]; then \
			library_count=$$((library_count + 1)); \
		fi; \
	done; \
	for source in $(TEST_SOURCES); do \
		if [ -f "$$source" ]; then \
			test_count=$$((test_count + 1)); \
		fi; \
	done; \
	total_count=$$((library_count + test_count)); \
	$(PRINTF) 'Module:          %s\n' "$(MODULE_NAME)"; \
	$(PRINTF) 'Version:         %s\n' "$(MODULE_VERSION)"; \
	$(PRINTF) 'Library files:   %s\n' "$$library_count"; \
	$(PRINTF) 'Test files:      %s\n' "$$test_count"; \
	$(PRINTF) 'Tracked total:   %s\n' "$$total_count"; \
	$(PRINTF) 'Discovered .vit: %s\n' \
		"$$( $(FIND) . -type f -name '*.vit' -not -path './build/*' | wc -l | tr -d ' ' )"

# ----------------------------------------------------------------------------
# Information
# ----------------------------------------------------------------------------

info:
	$(call heading,Vitte JSON module)
	@$(PRINTF) 'Name:       %s\n' "$(MODULE_NAME)"
	@$(PRINTF) 'Version:    %s\n' "$(MODULE_VERSION)"
	@$(PRINTF) 'Compiler:   %s\n' "$(VITTE)"
	@$(PRINTF) 'Build dir:  %s\n' "$(BUILD_DIR)"
	@$(PRINTF) '\n'
	@$(PRINTF) 'Core sources:\n'
	@for source in $(CORE_SOURCES); do \
		$(PRINTF) '  %s\n' "$$source"; \
	done
	@$(PRINTF) '\n'
	@$(PRINTF) 'Runtime sources:\n'
	@for source in $(RUNTIME_SOURCES); do \
		$(PRINTF) '  %s\n' "$$source"; \
	done
	@$(PRINTF) '\n'
	@$(PRINTF) 'Tests:\n'
	@for source in $(TEST_SOURCES); do \
		$(PRINTF) '  %s\n' "$$source"; \
	done

# ----------------------------------------------------------------------------
# Cleanup
# ----------------------------------------------------------------------------

clean:
	$(call heading,Cleaning JSON module)
	@$(RMRF) "$(BUILD_DIR)"
	$(call success,clean)

distclean: clean
	@$(FIND) . \
		-type f \
		\( \
			-name '*.tmp' \
			-o -name '*.log' \
			-o -name '*.out' \
			-o -name '*.bak' \
			-o -name '*~' \
		\) \
		-delete 2>/dev/null || true

# ----------------------------------------------------------------------------
# Help
# ----------------------------------------------------------------------------

help:
	@$(PRINTF) '%s\n' \
		'Vitte JSON module' \
		'' \
		'Usage:' \
		'  make [target]' \
		'' \
		'Main targets:' \
		'  all              Alias for check' \
		'  check            Validate package layout and entry module' \
		'  test             Alias for the installed-module import smoke test' \
		'  test-vitte       Resolve a JSON import from a staged installation' \
		'  verify           Run check + installed-module smoke test' \
		'  smoke            Check the package entry module' \
		'  install          Install JSON sources under PREFIX' \
		'  uninstall        Remove JSON sources under PREFIX' \
		'' \
		'Inspection:' \
		'  list             List every discovered .vit source' \
		'  list-library     List library sources' \
		'  list-tests       List test sources' \
		'  stats            Show module statistics' \
		'  info             Show module information' \
		'' \
		'Cleanup:' \
		'  clean            Remove build artifacts' \
		'  distclean        Remove build artifacts and temporary files' \
		'' \
		'Variables:' \
		'  VITTE=...        Path to the Vitte compiler' \
		'  VITTE_FLAGS=...  Additional compiler flags'
