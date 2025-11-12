CC = gcc
CFLAGS = -Wall -g
LIBS = -lparser
TARGET = msh

all: $(TARGET)

$(TARGET): msh.c libparser_64.a parser.h
	$(CC) $(CFLAGS) msh.c libparser_64.a -o $(TARGET) -no-pie

clean:
	rm -f $(TARGET)
