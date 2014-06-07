TOOL_CHAIN_PATH	:= /usr/bin
CC	:= $(TOOL_CHAIN_PATH)/armv6j-hardfloat-linux-gnueabi-gcc
LD	:= $(TOOL_CHAIN_PATH)/armv6j-hardfloat-linux-gnueabi-ld
OBJDUMP	:= $(TOOL_CHAIN_PATH)/armv6j-hardfloat-linux-gnueabi-objdump
OBJCOPY := $(TOOL_CHAIN_PATH)/armv6j-hardfloat-linux-gnueabi-objcopy
CFLAGS	:= -g -O2 -fno-builtin -mfloat-abi=soft -mthumb -mcpu=cortex-m0 -march=armv6-m -mcpu=cortex-m0
CPPFLAGS	:=
LDFLAGS	:= -static -nostdlib -Ttext=0x1000 -Tdata=0x2000 --section-start .rodata=0x3000
vpath %.c src
.PHONY:all clean
all:
	+make $(addsuffix .bin,$(basename $(notdir $(wildcard src/*.[c]))))

%.o:%.c
	$(CC) -c -o $@ $< $(CFLAGS) $(CPPFLAGS)

%.x:%.o
	$(LD) -o $@ $^ $(LDFLAGS)

%.ihex:%.x
	$(OBJCOPY) -O ihex $< $@

%.bin:%.x
	$(OBJCOPY) -O binary $< $@

%_swap.bin:%.bin
	./swap_endian.rb $< > $@


clean:
	rm -f *.o *.x *.?hex *.bin
