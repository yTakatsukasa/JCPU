SRC_DIRS			:= ../src $(wildcard ../src/target-*)
INCLUDE_DIRS		:= ../include

include Makefile.common

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
