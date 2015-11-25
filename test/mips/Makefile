SRC_DIRS		:= ./
INCLUDE_DIRS	:= ../../include
LIB_DIRS		:= ../../build
LIBS			:= pthread dl jcpu ncurses z 
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
