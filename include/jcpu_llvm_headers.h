#ifndef JCPU_LLVM_HEADERS_H
#define JCPU_LLVM_HEADERS_H

#include <llvm/Config/llvm-config.h>

#if LLVM_VERSION_MAJOR != 3
#error "Only LLVM 3.x is supported"
#endif

#if LLVM_VERSION_MINOR <= 2 //3.0, 3.1, 3.2
#include <llvm/LLVMContext.h>
#include <llvm/Instructions.h> //LoadInst
#include <llvm/IRBuilder.h>
#include <llvm/Module.h>
#elif LLVM_VERSION_MINOR == 3 //3.3
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Instructions.h> //LoadInst
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Module.h>
#else
#error "Not supported LLVM version"
#endif


#include <llvm/ExecutionEngine/JIT.h> 
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/PrettyStackTrace.h>
#include <llvm/Support/Signals.h>
#include <llvm/Support/Debug.h> //EnableDebugBuffering
#include <llvm/Analysis/Verifier.h> //verifyModule

#include <llvm/ExecutionEngine/JIT.h> 
#include <llvm/PassManager.h> //PassManager
#include <llvm/Support/raw_ostream.h> //outs()
#include <llvm/Assembly/PrintModulePass.h> //PrintModulePass



#endif
