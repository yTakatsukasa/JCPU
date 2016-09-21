SRC_DIRS			:= . ../../src $(wildcard ../../src/target-*)
INCLUDE_DIRS		:=  ../../include .
LLVM_CONFIG			?= /opt/llvm/3.3-debug/bin/llvm-config
LIB_DIRS			:= ../../build
LIBS				:= dl

include ../../build/Makefile.common

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
