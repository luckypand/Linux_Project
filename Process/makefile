# 编译器和工具定义
CXX = g++
RM = rm -f

# 目录定义
INC_DIR = inc
SRC_DIR = src
TEST_DIR = test

# 目标可执行文件
TARGET = process

# 源文件
CORE_SRC = $(SRC_DIR)/processbar.cpp
TEST_SRC = $(TEST_DIR)/main.cpp

# 目标文件（在当前目录生成）
CORE_OBJ = processbar.o
TEST_OBJ = main.o

# 编译选项
CXXFLAGS = -Wall -g -I$(INC_DIR) -std=c++11

# 默认目标
all: $(TARGET)

# 链接可执行文件
$(TARGET): $(CORE_OBJ) $(TEST_OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $^
	@echo "构建完成: $@"

# 编译核心文件
$(CORE_OBJ): $(CORE_SRC) $(INC_DIR)/processbar.h
	$(CXX) $(CXXFLAGS) -c $< -o $@
	@echo "编译核心文件: $< -> $@"

# 编译测试文件
$(TEST_OBJ): $(TEST_SRC) $(INC_DIR)/processbar.h
	$(CXX) $(CXXFLAGS) -c $< -o $@
	@echo "编译测试文件: $< -> $@"

# 运行程序
.PHONY: run
run: $(TARGET)
	@echo "运行程序..."
	./$(TARGET)

# 清理
.PHONY: clean
clean:
	$(RM) $(TARGET) $(CORE_OBJ) $(TEST_OBJ)
	@echo "清理完成"

# 帮助
.PHONY: help
help:
	@echo "可用命令:"
	@echo "  make       - 构建程序"
	@echo "  make run   - 构建并运行程序"
	@echo "  make clean - 清理生成的文件"
	@echo "  make help  - 显示此帮助"

