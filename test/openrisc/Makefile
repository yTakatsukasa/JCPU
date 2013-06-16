V					?= 0
DEBUG				?= 1
SRC_DIRS			:= . ../../src ../../src/target-openrisc
EXECOPT 			:= 
INCLUDE_DIRS		:=  ../../include
USE_GCC				:= 1
ifeq ($(USE_GCC),0)
CC					:= gcc
CXX					:= g++
CPP					:= cpp
else
CC					:= clang 
CXX					:= clang++
CPP					:= clang -E
endif
CPPFLAGS			:= $(addprefix -I,$(INCLUDE_DIRS)) $(shell llvm-config --cppflags)
ifeq ($(DEBUG),1)
DEBUGGER_CMD		:= gdb --args
CXXFLAGS			:= $(filter-out -O%,$(shell llvm-config --cxxflags)) -g3 -O0 -Wall -std=c++11
else
DEBUGGER_CMD		:=
CXXFLAGS			:= $(filter-out -O%,$(shell llvm-config --cxxflags)) -g3 -O3 -Wall -std=c++11
endif
CFLAGS				:= $(CXXFLAGS)
LIB_DIRS			:=
LIBS				:= dl
LDFLAGS             := $(addprefix -L,$(LIB_DIRS)) $(addprefix -l,$(LIBS)) $(shell llvm-config --ldflags --libs) -Wl,-rpath=$(dir $(shell which clang))/../lib64
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

.%.d:%.cpp
	@echo Generating dependency list of $< $(SHOW_MSG)
	@echo -n "$@ ." > $@
	$(SHOW_CMD_LINE) $(CPP) $(CPPFLAGS) $< -MM >> $@ ||  rm -f $@ || /bin/false

.%.d:%.c
	@echo Generating dependency list of $< $(SHOW_MSG)
	@echo -n "$@ ." > $@
	$(SHOW_CMD_LINE) $(CPP) $(CPPFLAGS) $< -MM >> $@ ||  rm -f $@ || /bin/false

clean:
	rm -f .*.[do] *.x

-include $(addsuffix .d,$(basename $(OBJS)))
