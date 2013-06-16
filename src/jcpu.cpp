#include <cassert>
#include "jcpu.h"
#include "jcpu_internal.h"
#include "jcpu_openrisc.h"

namespace jcpu{


jcpu::jcpu() : ext_ifs(JCPU_NULLPTR){}

void jcpu::set_ext_interface(jcpu_ext_if *ifs){
    assert(!ext_ifs);
    ext_ifs = ifs;
}


jcpu * jcpu::create(const char*arch, const char *model){
    return new openrisc::openrisc(model);
}


} //end of namespace jcpu
