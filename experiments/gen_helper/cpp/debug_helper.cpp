#include "../../include/qcpu_vm.h"

qcpu::vm::qcpu_vm_if *qcpu_vm_ptr;

extern "C"{
void set_qcpu_vm_ptr(qcpu::vm::qcpu_vm_if *ptr){
    qcpu_vm_ptr = ptr;
}

void qcpu_vm_dump_regs(){
    qcpu_vm_ptr->dump_regs();
}
}
