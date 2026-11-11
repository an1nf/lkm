CC      = gcc
CFLAGS  = -O2 -Wall -Wextra -std=c11 -Iinclude -fopenmp
LDFLAGS = -lm -fopenmp

SRC_DIR = src
OBJ_DIR = build
BIN     = lkm

SRCS = $(SRC_DIR)/vm.c $(SRC_DIR)/levin.c $(SRC_DIR)/metabiology.c \
       $(SRC_DIR)/selfref.c $(SRC_DIR)/rng.c $(SRC_DIR)/main.c
OBJS = $(SRCS:$(SRC_DIR)/%.c=$(OBJ_DIR)/%.o)

.PHONY: all clean run test

all: $(BIN)

$(BIN): $(OBJS)
	$(CC) $(OBJS) -o $@ $(LDFLAGS)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c | $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

run: $(BIN)
	./$(BIN)

clean:
	rm -rf $(OBJ_DIR) $(BIN) tests/test_selfref

tests/test_selfref: tests/test_selfref.c $(OBJ_DIR)/vm.o $(OBJ_DIR)/selfref.o $(OBJ_DIR)/levin.o
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

test: tests/test_selfref
	./tests/test_selfref
