CC = gcc
CFLAGS = -Wall -g
LIBS = -lparser
TARGET = myshell

all: $(TARGET)

$(TARGET): myshell.c libparser_64.a parser.h
	$(CC) $(CFLAGS) myshell.c libparser_64.a -o $(TARGET) -no-pie

clean:
	rm -f $(TARGET)
