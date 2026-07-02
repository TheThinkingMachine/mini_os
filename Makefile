CC      = gcc
CFLAGS  = -Wall -Wextra -std=c99 -Iinclude
LDFLAGS =

SRC     = src/main.c src/kernel.c src/vfs.c src/proc.c src/shell.c
OBJ     = $(SRC:.c=.o)
TARGET  = mini_linux

.PHONY: all clean run

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

src/%.o: src/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(OBJ) $(TARGET)
