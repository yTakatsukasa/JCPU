#include <vector>
#include <map>
#include <iostream>
#include <iomanip>


#include "jcpu_vm.h"


namespace jcpu{
namespace vm{

ir_builder_wrapper::ir_builder_wrapper(jcpu_vm_if &vmif, llvm::LLVMContext &c) : 
    vm(vmif),
    builder(new llvm::IRBuilder<>(c))
{
}

void ir_builder_wrapper::set_pc_str(std::string &str)const{
    char buf[19];
    ::snprintf(buf, sizeof(buf), "@%lX_", static_cast<unsigned long>(vm.get_cur_disas_virt_pc()));
    str += buf;
}

llvm::Type * ir_builder_wrapper::getInt8Ty()const{
    return builder->getInt8Ty();
}

llvm::Type * ir_builder_wrapper::getInt16Ty()const{
    return builder->getInt16Ty();
}

llvm::Type * ir_builder_wrapper::getInt32Ty()const{
    return builder->getInt32Ty();
}

llvm::Type * ir_builder_wrapper::getInt64Ty()const{
    return builder->getInt64Ty();
}

void ir_builder_wrapper::SetInsertPoint(llvm::BasicBlock *bb)const{
    builder->SetInsertPoint(bb);
}

llvm::ReturnInst *ir_builder_wrapper::CreateRet(llvm::Value *v)const{
    return builder->CreateRet(v);
}

llvm::CallInst *ir_builder_wrapper::CreateCall(llvm::Function *func, const char *nm)const{
    std::string str(nm);
    set_pc_str(str);
    return builder->CreateCall(func, str.c_str());
}

llvm::CallInst *ir_builder_wrapper::CreateCall(llvm::Function *func, llvm::Value *arg, const char *nm)const{
    std::string str(nm);
    set_pc_str(str);
    return builder->CreateCall(func, arg, str.c_str());
}

llvm::CallInst *ir_builder_wrapper::CreateCall2(llvm::Function *func, llvm::Value *arg0, llvm::Value *arg1, const char *nm)const{
    std::string str(nm);
    set_pc_str(str);
    return builder->CreateCall2(func, arg0, arg1, str.c_str());
}

llvm::CallInst *ir_builder_wrapper::CreateCall3(llvm::Function *func, llvm::Value *arg0, llvm::Value *arg1, llvm::Value *arg2, const char *nm)const{
    std::string str(nm);
    set_pc_str(str);
    return builder->CreateCall3(func, arg0, arg1, arg2, str.c_str());
}

llvm::Value * ir_builder_wrapper::CreateZExt(llvm::Value *v, llvm::Type *t, const char *nm)const{
    std::string str(nm);
    set_pc_str(str);
    return builder->CreateZExt(v, t, str.c_str());
}

llvm::Value * ir_builder_wrapper::CreateSExt(llvm::Value *v, llvm::Type *t, const char *nm)const{
    std::string str(nm);
    set_pc_str(str);
    return builder->CreateSExt(v, t, str.c_str());
}

llvm::Value * ir_builder_wrapper::CreateTrunc(llvm::Value *v, llvm::Type *t, const char *nm)const{
    std::string str(nm);
    set_pc_str(str);
    return builder->CreateTrunc(v, t, str.c_str());
}

#define BUILDER_CREATE2(func_name) \
    llvm::Value * ir_builder_wrapper::Create##func_name (llvm::Value *arg0, llvm::Value *arg1, const char *nm)const{ \
        std::string str(nm); \
        set_pc_str(str); \
        return builder->Create##func_name(arg0, arg1, str.c_str()); \
    }


BUILDER_CREATE2(Add)
BUILDER_CREATE2(Sub)
BUILDER_CREATE2(Mul)
BUILDER_CREATE2(Or)
BUILDER_CREATE2(And)
BUILDER_CREATE2(Xor)
BUILDER_CREATE2(Shl)
BUILDER_CREATE2(LShr)
BUILDER_CREATE2(AShr)
BUILDER_CREATE2(ICmpEQ)
BUILDER_CREATE2(ICmpNE)
BUILDER_CREATE2(ICmpULE)
BUILDER_CREATE2(ICmpULT)
BUILDER_CREATE2(ICmpUGE)
BUILDER_CREATE2(ICmpUGT)
BUILDER_CREATE2(ICmpSLE)
BUILDER_CREATE2(ICmpSLT)
BUILDER_CREATE2(ICmpSGE)
BUILDER_CREATE2(ICmpSGT)

#define BUILDER_CREATE2I(func_name) \
    llvm::Value * ir_builder_wrapper::Create##func_name (llvm::Value *v, unsigned int i, const char *nm)const{ \
        std::string str(nm); \
        set_pc_str(str); \
        return builder->Create##func_name(v, i, str.c_str()); \
    }

BUILDER_CREATE2I(Shl)
BUILDER_CREATE2I(LShr)
BUILDER_CREATE2I(AShr)
BUILDER_CREATE2I(And)


} //end of namespace vm
} //end of namespace jcpu
