# Simple Makefile for Lab 4 Scheduler
#make        -compile, generate ./group3
#make clean  -remove ./group3

CC = gcc
CFLAGS = -Wall -Wextra -std=c11

TARGET = group3_scheduler
SRCS = group3_scheduler.c queue.c
HEADERS = queue.h

all: $(TARGET)

$(TARGET): $(SRCS) $(HEADERS)
	$(CC) $(CFLAGS) $(SRCS) -o $(TARGET)

clean:
	rm -f $(TARGET)