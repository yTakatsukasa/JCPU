V					?= 0
DEBUG				?= 1
SRC_DIRS			:= . ../../src $(wildcard ../../src/target-*)
EXECOPT 			:= 
INCLUDE_DIRS		:=  ../../include .
USE_GCC				:= 1
LLVM_CONFIG			?= /opt/llvm/3.3-debug/bin/llvm-config
ifeq ($(USE_GCC),1)
CC					:= gcc
CXX					:= g++
CPP					:= cpp
else
CC					:= clang 
CXX					:= clang++
CPP					:= clang -E
endif
CPPFLAGS			:= $(addprefix -I,$(INCLUDE_DIRS)) $(shell $(LLVM_CONFIG) --cppflags)
ifeq ($(DEBUG),1)
DEBUGGER_CMD		:= gdb --args
CXXFLAGS			:= -MD -g -O0 -Wall -fexceptions -std=c++14
else
DEBUGGER_CMD		:=
CXXFLAGS			:= -MD -g -O3 -Wall -fexceptions -std=c++14
endif
CFLAGS				:= $(CXXFLAGS)
LIB_DIRS			:= ../../build
LIBS				:= dl
LDFLAGS             := $(addprefix -L,$(LIB_DIRS)) $(addprefix -l,$(LIBS)) $(shell $(LLVM_CONFIG) --ldflags --libs) -Wl,-rpath=$(dir $(shell which clang))/../lib64 -lpthread -lz -lncurses
SRCS				:= $(foreach dir,$(SRC_DIRS),$(wildcard $(dir)/*.cpp $(dir)/*.c))
OBJS				:= $(addprefix .,$(addsuffix .o,$(basename $(notdir $(SRCS)))))

ifeq ($V,1)
SHOW_CMD_LINE   := 
SHOW_MSG        := > /dev/null
else
SHOW_CMD_LINE   := @
SHOW_MSG        :=
endif
.PHONY:clean runall

vpath %.cpp $(SRC_DIRS)
vpath %.c $(SRC_DIRS)

run.x:$(OBJS)

%.x:
	@echo Linking $@ $(SHOW_MSG)
	$(SHOW_CMD_LINE) $(CXX)	-o $@ $(filter %.o,$^) $(LDFLAGS)

.%.o:%.cpp
	@echo Compiling $< $(SHOW_MSG)
	$(SHOW_CMD_LINE) $(CXX)	$(CPPFLAGS) $(CXXFLAGS) -c -o $@ $<

.%.o:%.c
	@echo Compiling $< $(SHOW_MSG)
	$(SHOW_CMD_LINE) $(CC)	$(CPPFLAGS) $(CFLAGS) -c -o $@ $<

clean:
	rm -f .*.[do] *.x

-include $(wildcard .*.d)