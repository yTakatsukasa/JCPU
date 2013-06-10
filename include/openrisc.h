#ifndef QCPU_OPENRISC_H
#define QCPU_OPENRISC_H

#include <stdint.h>
#include "qcpu.h"
#include "qcpu_internal.h"
#include "qcpu_vm.h"

namespace qcpu{
namespace openrisc{

class openrisc_vm;

class openrisc : public qcpu{
    openrisc_vm *vm;
    public:
    explicit openrisc(const char *);
    ~openrisc();
    virtual void interrupt(int, bool) QCPU_OVERRIDE;
    virtual void reset(bool)QCPU_OVERRIDE;
    virtual void run(run_option_e)QCPU_OVERRIDE;
};



} //end of namespace openrisc
} //end of namespace qcpu



#endif
