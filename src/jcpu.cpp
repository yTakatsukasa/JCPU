#include <cassert>
#include "jcpu.h"
#include "jcpu_internal.h"
#include "jcpu_openrisc.h"
#include "jcpu_riscv.h"

namespace jcpu{


jcpu::jcpu() : ext_ifs(JCPU_NULLPTR){}

void jcpu::set_ext_interface(jcpu_ext_if *ifs){
    assert(!ext_ifs);
    ext_ifs = ifs;
}


jcpu * jcpu::create(const char*arch_, const char *model){
    const std::string arch(arch_);
    if(arch == "openrisc"){
        return new openrisc::openrisc(model);
    }
    else if(arch == "riscv"){
        return new riscv::riscv(model);
    }
    else{
        jcpu_assert(!"Not supported architecture");
    }
}

void jcpu::initialize(){
    llvm::InitializeNativeTarget();
    llvm::InitializeNativeTargetAsmPrinter();
    //llvm::InitializeAllTargetMCs();
}

} //end of namespace jcpu
