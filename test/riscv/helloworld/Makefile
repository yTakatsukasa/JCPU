TOOL_CHAIN_PATH	:= /opt/riscv/bin/
ARCH			:= riscv64-unknown-elf
CC				:= ${TOOL_CHAIN_PATH}/${ARCH}-gcc
CXX				:= ${TOOL_CHAIN_PATH}/${ARCH}-g++
LD				:= ${TOOL_CHAIN_PATH}/${ARCH}-gcc
OBJDUMP			:= ${TOOL_CHAIN_PATH}/${ARCH}-objdump
OBJCOPY			:= ${TOOL_CHAIN_PATH}/${ARCH}-objcopy
CFLAGS			:= -g -m64
CPPFLAGS		:=
LDFLAGS			:= -m64 -nostdlib -static -T simple_link.lnk

SRC_DIRS		:= ./
SRCS				:= $(foreach dir,$(SRC_DIRS),$(wildcard $(dir)/*.cpp $(dir)/*.c $(dir)/*.S))
OBJS				:= $(addprefix .,$(addsuffix .o,$(basename $(notdir $(SRCS)))))

.PHONY:clean all

vpath %.cpp $(SRC_DIRS)
vpath %.c $(SRC_DIRS)

ifeq ($V,1)
SHOW_CMD_LINE   := 
SHOW_MSG        := > /dev/null
else
SHOW_CMD_LINE   := @
SHOW_MSG        :=
endif

run.x:$(OBJS)

%.x:
	@echo Linking $@ $(SHOW_MSG)
	$(SHOW_CMD_LINE) $(LD)	-o $@ $(filter %.o,$^) $(LDFLAGS)

.%.o:%.cpp
	@echo Compiling $< $(SHOW_MSG)
	$(SHOW_CMD_LINE) $(CXX)	$(CPPFLAGS) $(CXXFLAGS) -c -o $@ $<

.%.o:%.c
	@echo Compiling $< $(SHOW_MSG)
	$(SHOW_CMD_LINE) $(CC)	$(CPPFLAGS) $(CFLAGS) -c -o $@ $<

.%.o:%.S
	@echo Compiling $< $(SHOW_MSG)
	$(SHOW_CMD_LINE) $(CC)	$(CPPFLAGS) $(CFLAGS) -c -o $@ $<
clean:
	rm -f .*.[do] *.x

-include $(wildcard .*.d)
