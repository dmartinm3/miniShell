CC      = gcc
CFLAGS  = -std=c11 -Wall -Wextra -Werror -O2 -g -D_POSIX_C_SOURCE=200809L
LDFLAGS = -no-pie
TARGET  = myshell

all: $(TARGET)

$(TARGET): myshell.c libparser_64.a parser.h
	$(CC) $(CFLAGS) myshell.c libparser_64.a $(LDFLAGS) -o $(TARGET)

clean:
	rm -f $(TARGET)