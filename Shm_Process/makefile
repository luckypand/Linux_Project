CXX := g++

WARN_FLAGS := -Wall -Wextra
STD_FLAGS := -std=c++11
HEADER_FLAGS := -I./inc
INCLUDE_FLAGS := -include stdio.h -include unistd.h -include stdlib.h

RELEASE_FLAGS := $(WARN_FLAGS) $(STD_FLAGS) $(HEADER_FLAGS) -O2 $(INCLUDE_FLAGS)
DEBUG_FLAGS := $(WARN_FLAGS) $(STD_FLAGS) $(HEADER_FLAGS) -O0 -g3 -ggdb $(INCLUDE_FLAGS)

TEST_SRCS := $(wildcard test/*.cpp test/*.cc)

TARGETS_CPP := $(patsubst test/%.cpp,%,$(filter %.cpp,$(TEST_SRCS)))
TARGETS_CC := $(patsubst test/%.cc,%,$(filter %.cc,$(TEST_SRCS)))
TARGETS := $(TARGETS_CPP) $(TARGETS_CC)
DEBUG_TARGETS := $(addsuffix _debug,$(TARGETS))

BUILD ?= release

ifeq ($(BUILD),debug)
OUT := $(DEBUG_TARGETS)
OUT_FLAGS := $(DEBUG_FLAGS)
else
OUT := $(TARGETS)
OUT_FLAGS := $(RELEASE_FLAGS)
endif

.PHONY: all release debug run run-release run-debug clean help

all: $(OUT)

release: $(TARGETS)

debug: $(DEBUG_TARGETS)


$(TARGETS_CPP): %: test/%.cpp
	$(CXX) $(RELEASE_FLAGS) $< -o $@

$(TARGETS_CC): %: test/%.cc
	$(CXX) $(RELEASE_FLAGS) $< -o $@

$(addsuffix _debug,$(TARGETS_CPP)): %_debug: test/%.cpp
	$(CXX) $(DEBUG_FLAGS) $< -o $@

$(addsuffix _debug,$(TARGETS_CC)): %_debug: test/%.cc
	$(CXX) $(DEBUG_FLAGS) $< -o $@

run: all
	@for bin in $(OUT); do ./$$bin; done

run-release: release
	@for bin in $(TARGETS); do ./$$bin; done

run-debug: debug
	@for bin in $(DEBUG_TARGETS); do ./$$bin; done

help:
	@echo "Usage:"
	@echo "  make release      # build release binaries: $(TARGETS)"
	@echo "  make debug        # build debug binaries: $(DEBUG_TARGETS)"
	@echo "  make BUILD=release"
	@echo "  make BUILD=debug"
	@echo "  make run          # run by BUILD selection (default release, runs all)"
	@echo "  make run-release"
	@echo "  make run-debug"

clean:
	rm -f $(TARGETS) $(DEBUG_TARGETS)

# Pid_CTR:test/main.cpp
# 	g++ -o Pid_CTR test/main.cpp

# .PHONY:clean
# clean:
# 	@echo "清理工程"
# 	@rm -rf Pid_CTR
# 	@echo "清理完成"