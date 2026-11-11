# Movie Server - build instructions
# Requires: gcc (MinGW) or MSVC

CC = gcc
CFLAGS = -O2 -Wall -Wextra -D_CRT_SECURE_NO_WARNINGS
TARGET = movie_server
SRC = movie_server.c

.PHONY: all clean run

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC) -lws2_32

run: all
	./$(TARGET)

clean:
	rm -f $(TARGET)