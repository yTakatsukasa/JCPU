#ifndef JCPU_ARM_H
#define JCPU_ARM_H

#include <stdint.h>
#include "jcpu.h"
#include "jcpu_internal.h"
#include "jcpu_vm.h"

namespace jcpu{
namespace arm{

class arm_vm;

class arm : public jcpu{
    arm_vm *vm;
    public:
    explicit arm(const char *);
    ~arm();
    virtual void interrupt(int, bool) JCPU_OVERRIDE;
    virtual void reset(bool)JCPU_OVERRIDE;
    virtual void run(run_option_e)JCPU_OVERRIDE;
    virtual uint64_t get_total_insn_count()const JCPU_OVERRIDE;
};



} //end of namespace arm
} //end of namespace jcpu



#endif
