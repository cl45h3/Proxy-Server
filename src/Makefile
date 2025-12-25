CC = gcc
CFLAGS = -Wall -g -I../include
LIBS = -lws2_32
TARGET = proxy_server.exe

# source files
SRC = main.c 

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC) $(LIBS)
	@echo "Build successful! Run with: .\\$(TARGET)"

clean:
	del $(TARGET)