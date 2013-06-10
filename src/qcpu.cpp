#include <cassert>
#include "qcpu.h"
#include "qcpu_internal.h"
#include "openrisc.h"

namespace qcpu{


qcpu::qcpu() : ext_ifs(QCPU_NULLPTR){}

void qcpu::set_ext_interface(qcpu_ext_if *ifs){
    assert(!ext_ifs);
    ext_ifs = ifs;
}


qcpu * qcpu::create(const char*arch, const char *model){
    return new openrisc::openrisc(model);
}


} //end of namespace qcpu
