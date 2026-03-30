CXX := g++

WARN_FLAGS := -Wall -Wextra
STD_FLAGS := -std=c++11
HEADER_FLAGS := -I./inc
INCLUDE_FLAGS := -include stdio.h -include unistd.h -include stdlib.h

RELEASE_FLAGS := $(WARN_FLAGS) $(STD_FLAGS) $(HEADER_FLAGS) -O2 $(INCLUDE_FLAGS)
DEBUG_FLAGS := $(WARN_FLAGS) $(STD_FLAGS) $(HEADER_FLAGS) -O0 -g3 -ggdb $(INCLUDE_FLAGS)

SRC := test/main.cpp
TARGET := main
DEBUG_TARGET := main_debug

BUILD ?= release

ifeq ($(BUILD),debug)
OUT := $(DEBUG_TARGET)
OUT_FLAGS := $(DEBUG_FLAGS)
else
OUT := $(TARGET)
OUT_FLAGS := $(RELEASE_FLAGS)
endif

.PHONY: all release debug run run-release run-debug clean help

all: $(OUT)

release: $(TARGET)

debug: $(DEBUG_TARGET)

$(TARGET): $(SRC)
	$(CXX) $(RELEASE_FLAGS) $< -o $@

$(DEBUG_TARGET): $(SRC)
	$(CXX) $(DEBUG_FLAGS) $< -o $@

run: all
	./$(OUT)

run-release: release
	./$(TARGET)

run-debug: debug
	./$(DEBUG_TARGET)

help:
	@echo "Usage:"
	@echo "  make release      # build release binary: $(TARGET)"
	@echo "  make debug        # build debug binary: $(DEBUG_TARGET)"
	@echo "  make BUILD=release"
	@echo "  make BUILD=debug"
	@echo "  make run          # run by BUILD selection (default release)"
	@echo "  make run-release"
	@echo "  make run-debug"

clean:
	rm -f $(TARGET) $(DEBUG_TARGET)

# Pid_CTR:test/main.cpp
# 	g++ -o Pid_CTR test/main.cpp

# .PHONY:clean
# clean:
# 	@echo "清理工程"
# 	@rm -rf Pid_CTR
# 	@echo "清理完成"