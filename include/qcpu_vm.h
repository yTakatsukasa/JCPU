#ifndef QCPU_VM_H
#define QCPU_VM_H

#include <stdint.h>
#include <vector>
#include <utility>
#include "qcpu_internal.h"


namespace qcpu{
namespace vm{

class qcpu_vm_if{
    public:
    virtual void dump_regs()const = 0;
    virtual ~qcpu_vm_if(){}
};


} //end of namespace vm
} //end of namespace qcpu
#endif
