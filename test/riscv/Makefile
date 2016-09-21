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

.%.o:%.cpp elfio
	@echo Compiling $< $(SHOW_MSG)
	$(SHOW_CMD_LINE) $(CXX)	$(CPPFLAGS) $(CXXFLAGS) -c -o $@ $<

.%.o:%.c elfio
	@echo Compiling $< $(SHOW_MSG)
	$(SHOW_CMD_LINE) $(CC)	$(CPPFLAGS) $(CFLAGS) -c -o $@ $<

elfio.zip:
	wget https://github.com/serge1/ELFIO/archive/master.zip -O elfio.zip

elfio:elfio.zip
	@if [ ! -d ELFIO-master -a ! -d elfio ]; then unzip elfio.zip; fi
	@if [ ! -d elfio ]; then mv ELFIO-master/elfio ./; fi
	@if [ -d ELFIO-master ]; then rm -rf ELFIO-master; fi


clean:
	rm -f .*.[do] *.x
	rm -rf elfio

-include $(wildcard .*.d)
