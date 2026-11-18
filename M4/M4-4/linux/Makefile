# M4-4 Linux 上位机
# 用法：make        编译
#       make run    以默认参数运行（需要先改 DEV）
#       make clean  清理

CC      ?= gcc
CFLAGS  ?= -O2 -Wall -Wextra
TARGET   = upper
SRC      = upper.c

DEV      ?= /dev/ttyUSB0
BAUD     ?= 115200

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $@ $<

run: $(TARGET)
	./$(TARGET) -d $(DEV) -b $(BAUD)

clean:
	rm -f $(TARGET)

.PHONY: all run clean
