#ifndef JCPU_CORTEXM0_H
#define JCPU_CORTEXM0_H

#include <stdint.h>
#include "jcpu.h"
#include "jcpu_internal.h"
#include "jcpu_vm.h"

namespace jcpu{
namespace cortexm0{

class cortexm0_vm;

class cortexm0 : public jcpu{
    cortexm0_vm *vm;
    public:
    explicit cortexm0(const char *);
    ~cortexm0();
    virtual void interrupt(int, bool) JCPU_OVERRIDE;
    virtual void reset(bool)JCPU_OVERRIDE;
    virtual void run(run_option_e)JCPU_OVERRIDE;
    virtual uint64_t get_total_insn_count()const JCPU_OVERRIDE;
};



} //end of namespace cortexm0
} //end of namespace jcpu



#endif
