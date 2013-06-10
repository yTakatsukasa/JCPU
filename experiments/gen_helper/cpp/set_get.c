unsigned int regs[3];

unsigned int get_reg(unsigned short idx){
    const unsigned int *const ptr = &regs[0] + idx;
    const unsigned int val = *ptr;
    return val;
}

void set_reg(unsigned short idx, unsigned int val){
    unsigned int *const ptr = &regs[0] + idx;
    *ptr = val;
    return ;
}

