
SRC_DIR	:= ./cpp
IR_DIR	:= ./ir
RESULT_DIR	:= ./gen_cpp
CLANG		:= clang

$(IR_DIR)/%.s:$(SRC_DIR)/%.cpp 
	+make ${IR_DIR}
	$(CLANG) -emit-llvm -c -Wall -S -O2 $< -o $@ -D__STDC_LIMIT_MACROS -D__STDC_CONSTANT_MACROS -std=c++11

$(IR_DIR)/%.s:$(SRC_DIR)/%.c
	+make ${IR_DIR}
	$(CLANG) -emit-llvm -c -Wall -S -O2 $< -o $@

$(RESULT_DIR)/%.cpp:$(IR_DIR)/%.s
	+make ${RESULT_DIR}
	llc -march=cpp $^ -o $@

.PHONY:all clean
all:
	+make $(addsuffix .cpp,$(addprefix $(RESULT_DIR)/,$(notdir $(basename $(wildcard $(SRC_DIR)/*.c*)))))
clean:
	rm -f $(RESULT_DIR)/* $(IR_DIR)/*

${RESULT_DIR}:
	mkdir -p $@

${IR_DIR}:
	mkdir -p $@

