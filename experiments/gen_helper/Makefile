
SRC_DIR	:= ./cpp
IR_DIR	:= ./ir
RESULT_DIR	:= ./gen_cpp
CLANG		:= clang

$(IR_DIR)/%.s:$(SRC_DIR)/%.cpp
	$(CLANG) -emit-llvm -c -Wall -S -O2 $< -o $@ -D__STDC_LIMIT_MACROS -D__STDC_CONSTANT_MACROS

$(IR_DIR)/%.s:$(SRC_DIR)/%.c
	$(CLANG) -emit-llvm -c -Wall -S -O2 $< -o $@

$(RESULT_DIR)/%.cpp:$(IR_DIR)/%.s
	llc -march=cpp $^ -o $@

.PHONY:all clean
all:
	+make $(addsuffix .cpp,$(addprefix $(RESULT_DIR)/,$(notdir $(basename $(wildcard $(SRC_DIR)/*.c*)))))
clean:
	rm -f $(RESULT_DIR)/* $(IR_DIR)/*
