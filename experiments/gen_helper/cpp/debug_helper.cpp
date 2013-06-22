#include "../../../include/jcpu_vm.h"

jcpu::vm::jcpu_vm_if *jcpu_vm_ptr;

extern "C"{
void set_jcpu_vm_ptr(jcpu::vm::jcpu_vm_if *ptr){
    jcpu_vm_ptr = ptr;
}

void jcpu_vm_dump_regs(){
    jcpu_vm_ptr->dump_regs();
}
}
