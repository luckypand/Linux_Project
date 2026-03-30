CXX := g++
CXXFLAGS := -Wall -Wextra -O2 -std=c++11 -include stdio.h -include unistd.h -include stdlib.h

SRC := test/main.cpp
TARGET := main

.PHONY: all run clean

all: run

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) $< -o $@

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(TARGET)

# Pid_CTR:test/main.cpp
# 	g++ -o Pid_CTR test/main.cpp

# .PHONY:clean
# clean:
# 	@echo "清理工程"
# 	@rm -rf Pid_CTR
# 	@echo "清理完成"