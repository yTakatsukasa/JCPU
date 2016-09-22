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
#elif 3 <= LLVM_VERSION_MINOR && LLVM_VERSION_MINOR <= 8 //3.3, 3.4, 3.5, 3.6, 3.7, 3.8
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Instructions.h> //LoadInst
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Module.h>
#else
#error "Not supported LLVM version"
#endif

#if LLVM_VERSION_MINOR <= 5
#include <llvm/ExecutionEngine/JIT.h> 
#else //>= 3.6
#include <llvm/ExecutionEngine/MCJIT.h> 
#endif
#if LLVM_VERSION_MINOR <= 6
#include <llvm/PassManager.h>          //PassManager
#else
#include <llvm/IR/LegacyPassManager.h> //PassManager
#endif
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/PrettyStackTrace.h>
#include <llvm/Support/Signals.h>
#include <llvm/Support/Debug.h> //EnableDebugBuffering
#include <llvm/Support/raw_ostream.h> //outs()

#if LLVM_VERSION_MINOR <= 4
#include <llvm/Analysis/Verifier.h> //verifyModule
#include <llvm/Assembly/PrintModulePass.h> //PrintModulePass
#else
#include <llvm/IR/Verifier.h> //verifyModule
#include <llvm/IR/IRPrintingPasses.h> //PrintModulePass
#endif

#define JCPU_LLVM_VERSION_LE(major, minor) ((LLVM_VERSION_MAJOR == major && LLVM_VERSION_MINOR <= minor) || LLVM_VERSION_MAJOR < major)
#define JCPU_LLVM_VERSION_LT(major, minor) ((LLVM_VERSION_MAJOR == major && LLVM_VERSION_MINOR  < minor) || LLVM_VERSION_MAJOR < major)
#define JCPU_LLVM_VERSION_GE(major, minor) ((LLVM_VERSION_MAJOR == major && LLVM_VERSION_MINOR >= minor) || LLVM_VERSION_MAJOR > major)
#define JCPU_LLVM_VERSION_GT(major, minor) ((LLVM_VERSION_MAJOR == major && LLVM_VERSION_MINOR  > minor) || LLVM_VERSION_MAJOR > major)



#endif
