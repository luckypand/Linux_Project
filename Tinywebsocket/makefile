CXX := g++
PROJECT_DIR := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
EXE_DIR := $(PROJECT_DIR)/exe

WARN_FLAGS := -Wall -Wextra
STD_FLAGS := -std=c++11
HEADER_FLAGS := -I./inc
INCLUDE_FLAGS := -include stdio.h -include unistd.h -include stdlib.h
THREAD_FLAGS := -pthread

RELEASE_FLAGS := $(WARN_FLAGS) $(STD_FLAGS) $(HEADER_FLAGS) -O2 $(INCLUDE_FLAGS) $(THREAD_FLAGS)
DEBUG_FLAGS := $(WARN_FLAGS) $(STD_FLAGS) $(HEADER_FLAGS) -O0 -g3 -ggdb $(INCLUDE_FLAGS) $(THREAD_FLAGS)

TEST_SRCS := $(wildcard test/*.cpp test/*.cc)
SRC_SRCS := $(wildcard src/*.cpp src/*.cc)

TARGETS_CPP := $(patsubst test/%.cpp,%,$(filter %.cpp,$(TEST_SRCS)))
TARGETS_CC := $(patsubst test/%.cc,%,$(filter %.cc,$(TEST_SRCS)))
TARGETS := $(TARGETS_CPP) $(TARGETS_CC)
DEBUG_TARGETS := $(addsuffix _debug,$(TARGETS))
TARGET_EXES := $(addprefix $(EXE_DIR)/,$(TARGETS))
DEBUG_EXES := $(addprefix $(EXE_DIR)/,$(DEBUG_TARGETS))
LEGACY_EXES := $(TARGETS) $(DEBUG_TARGETS)

BUILD ?= release

ifeq ($(BUILD),debug)
OUT := $(DEBUG_EXES)
OUT_FLAGS := $(DEBUG_FLAGS)
else
OUT := $(TARGET_EXES)
OUT_FLAGS := $(RELEASE_FLAGS)
endif

.PHONY: all release debug run run-release run-debug flamegraph clean help

all: $(OUT)

release: $(TARGET_EXES)

debug: $(DEBUG_EXES)


$(EXE_DIR):
	mkdir -p $@

$(EXE_DIR)/%: test/%.cpp | $(EXE_DIR)
	$(CXX) $(RELEASE_FLAGS) $< $(SRC_SRCS) -o $@

$(EXE_DIR)/%: test/%.cc | $(EXE_DIR)
	$(CXX) $(RELEASE_FLAGS) $< $(SRC_SRCS) -o $@

$(EXE_DIR)/%_debug: test/%.cpp | $(EXE_DIR)
	$(CXX) $(DEBUG_FLAGS) $< $(SRC_SRCS) -o $@

$(EXE_DIR)/%_debug: test/%.cc | $(EXE_DIR)
	$(CXX) $(DEBUG_FLAGS) $< $(SRC_SRCS) -o $@

run: all
	@for bin in $(OUT); do "$$bin"; done

run-release: release
	@for bin in $(TARGET_EXES); do "$$bin"; done

run-debug: debug
	@for bin in $(DEBUG_EXES); do "$$bin"; done

flamegraph:
	@[ -n "$(BIN)" ] || { echo "Usage: make flamegraph BIN=Test_log"; exit 1; }
	@$(PROJECT_DIR)/gen_flamegraph.sh "$(EXE_DIR)/$(BIN)"

help:
	@echo "Usage:"
	@echo "  make release      # build release binaries in exe/: $(TARGETS)"
	@echo "  make debug        # build debug binaries in exe/: $(DEBUG_TARGETS)"
	@echo "  make BUILD=release"
	@echo "  make BUILD=debug"
	@echo "  make run          # run binaries from exe/ by BUILD selection"
	@echo "  make run-release"
	@echo "  make run-debug"
	@echo "  make flamegraph BIN=Test_log  # output svg/Test_log.svg"

clean:
	rm -f $(TARGET_EXES) $(DEBUG_EXES) $(LEGACY_EXES)
	rm -f $(EXE_DIR)/*
	-rmdir $(EXE_DIR) 2>/dev/null || true

# Pid_CTR:test/main.cpp
# 	g++ -o Pid_CTR test/main.cpp

# .PHONY:clean
# clean:
# 	@echo "清理工程"
# 	@rm -rf Pid_CTR
# 	@echo "清理完成"