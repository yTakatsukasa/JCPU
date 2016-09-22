TOOL_CHAIN_PATH	:= /usr/bin/
CC	:= $(TOOL_CHAIN_PATH)or1k-elf-gcc
LD	:= $(TOOL_CHAIN_PATH)or1k-elf-ld
OBJDUMP	:= $(TOOL_CHAIN_PATH)/or1k-elf-objdump
OBJCOPY := $(TOOL_CHAIN_PATH)/or1k-elf-objcopy
CFLAGS	:= -g -O2 -fno-builtin 
CPPFLAGS	:= -Ilib
LDFLAGS	:= -T OR1K_startup.ld -static -nostdlib 
vpath %.S . src
vpath %.c src lib
.PHONY:all clean
all:
	+make $(addsuffix .x,$(basename $(notdir $(wildcard src/*.[c]))))

%.o:%.S
	$(CC) -c -o $@ $< $(CFLAGS) $(CPPFLAGS)

%.o:%.c
	$(CC) -c -o $@ $< $(CFLAGS) $(CPPFLAGS)

%.x:crt0.o %.o
	$(LD) -o $@ $^ $(LDFLAGS)

clean:
	rm -f *.o *.x *.?hex *.bin
