V					?= 0
DEBUG				?= 1
SRC_DIRS			:= ../src $(wildcard ../src/target-*)
EXECOPT 			:= 
INCLUDE_DIRS		:= ../include
USE_GCC				:= 1
LLVM_CONFIG			?= llvm-config
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
CXXFLAGS			:= -g -O0 -Wall -fexceptions -MD # -std=c++11
else
DEBUGGER_CMD		:=
CXXFLAGS			:= -g -O3 -Wall -fexceptions # -std=c++11
endif
CFLAGS				:= $(CXXFLAGS)
LIB_DIRS			:=
LIBS				:= dl
LDFLAGS             := $(addprefix -L,$(LIB_DIRS)) $(addprefix -l,$(LIBS)) $(shell $(LLVM_CONFIG) --ldflags --libs) -Wl,-rpath=$(dir $(shell which clang))/../lib64
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

libjcpu.a:$(OBJS)

%.a:
	@echo Archiving $@ $(SHOW_MSG)
	$(SHOW_CMD_LINE) ar cruv $@ $(filter %.o,$^)

.%.o:%.cpp
	@echo Compiling $< $(SHOW_MSG)
	$(SHOW_CMD_LINE) $(CXX)	$(CPPFLAGS) $(CXXFLAGS) -c -o $@ $<

.%.o:%.c
	@echo Compiling $< $(SHOW_MSG)
	$(SHOW_CMD_LINE) $(CC)	$(CPPFLAGS) $(CFLAGS) -c -o $@ $<

clean:
	rm -f .*.[do] *.x

-include $(wildcard .*.d)
