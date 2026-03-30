BIN = MuchFile.exe
SRC = $(wildcard *.c)
OBJ = $(SRC:.c=.o)

$(BIN):$(OBJ)
	@gcc -o $@ $^
	@echo "linking $^ to $@ ... done"
%.o:%.c
	@gcc -c $<
	@echo "compiling $< to $@ ... done"

.PHONY:test
test:
	@echo "Debug..."
	@echo $(SRC)
	@echo "Debug..."

.PHONY:clean
clean:
	rm -rf $(OBJ) $(BIN)