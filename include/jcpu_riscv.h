#ifndef JCPU_RISCV_H
#define JCPU_RISCV_H

#include <stdint.h>
#include "jcpu.h"
#include "jcpu_internal.h"
#include "jcpu_vm.h"

namespace jcpu{
namespace riscv{

class riscv_vm;

class riscv : public jcpu{
    riscv_vm *vm;
    public:
    explicit riscv(const char *);
    ~riscv();
    virtual void interrupt(int, bool) JCPU_OVERRIDE;
    virtual void reset(bool)JCPU_OVERRIDE;
    virtual void run(run_option_e)JCPU_OVERRIDE;
    virtual uint64_t get_total_insn_count()const JCPU_OVERRIDE;
};

} //end of namespace riscv
} //end of namespace jcpu



#endif
