#ifndef JCPU_OPENRISC_H
#define JCPU_OPENRISC_H

#include <stdint.h>
#include "jcpu.h"
#include "jcpu_internal.h"
#include "jcpu_vm.h"

namespace jcpu{
namespace openrisc{

class openrisc_vm;

class openrisc : public jcpu{
    openrisc_vm *vm;
    public:
    explicit openrisc(const char *);
    ~openrisc();
    virtual void interrupt(int, bool) JCPU_OVERRIDE;
    virtual void reset(bool)JCPU_OVERRIDE;
    virtual void run(run_option_e)JCPU_OVERRIDE;
    virtual uint64_t get_total_insn_count()const JCPU_OVERRIDE;
};



} //end of namespace openrisc
} //end of namespace jcpu



#endif
