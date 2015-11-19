#ifndef JCPU_VM_H
#define JCPU_VM_H

#include <stdint.h>
#include <vector>
#include <utility>
#include <stack>
#include <sstream>

#include "jcpu_llvm_headers.h"
#include "jcpu.h"
#include "jcpu_internal.h"
#include "gdbserver.h"

//#define JCPU_VM_DEBUG 1

namespace jcpu{
namespace vm{

enum jcpu_vm_arch_e{
    JCPU_ARCH_OPENRISC,
    JCPU_ARCH_ARM
};

void make_set_get(llvm::Module *, llvm::GlobalVariable *, unsigned int);
void make_mem_access(llvm::Module *, unsigned int);
void make_debug_func(llvm::Module *, unsigned int);

template<typename ARCH>
class general_reg_cache{
    static const size_t num_regs =  ARCH::NUM_REGS;
    std::pair<llvm::Value *, bool> regs[num_regs];//latest_reg, is_dirty
    void clear(){
        for(size_t i = 0; i < num_regs; ++i){regs[i] = std::pair<llvm::Value *, bool>(JCPU_NULLPTR, false);}
    }
    public:
    general_reg_cache(){clear();}
    llvm::Value * set(typename ARCH::reg_e r, llvm::Value *v, bool dirty){
        jcpu_assert(static_cast<unsigned int>(r) < num_regs);
        regs[r].second |= dirty;
        regs[r].first = v;
        return v;
    }
    llvm::Value *get(typename ARCH::reg_e r)const{
        jcpu_assert(static_cast<unsigned int>(r) < num_regs);
        return regs[r].first;
    }
    template<typename F>
    void flush_and_clear(const F &f){
        for(size_t i = 0; i < num_regs; ++i){
            if(regs[i].second){//dirty
                f(static_cast<typename ARCH::reg_e>(i), regs[i].first);
            }
            regs[i] = std::pair<llvm::Value *, bool>(JCPU_NULLPTR, false);
        }
    }

};


class jcpu_vm_if{
    public:
    virtual uint64_t get_cur_disas_virt_pc()const = 0;
    virtual void dump_regs()const = 0;
    virtual ~jcpu_vm_if(){}
};

template<typename T, typename ARCH, int TAG>
class primitive_type_holder{
    T v;
    typedef primitive_type_holder<T, ARCH, TAG> this_type;
    public:
    explicit primitive_type_holder(T v) : v(v){}
    operator const T ()const{return v;}
    const this_type operator + (const this_type &other)const{
        return this_type(v + other.v);
    }
};

template<typename ARCH>
class basic_block{
    typedef typename ARCH::phys_addr_t phys_addr_t;
    typedef typename ARCH::target_ulong (*basic_block_func_t)();

    const phys_addr_t start_phys_addr, end_phys_addr;
    const llvm::Function *const func;
    basic_block_func_t const func_ptr;
    const unsigned int num_insn;
    basic_block();
    public:
    basic_block(phys_addr_t sp, phys_addr_t ep, llvm::Function *f, llvm::ExecutionEngine *ee, unsigned int insn) :
        start_phys_addr(sp), end_phys_addr(ep), func(f),
        func_ptr(reinterpret_cast<basic_block_func_t>(ee->getPointerToFunction(f))),
        num_insn(insn)
    {}
    typename ARCH::virt_addr_t exec()const{return static_cast<typename ARCH::virt_addr_t>((*func_ptr)());}
    unsigned int get_icount()const{return num_insn;}
    phys_addr_t get_start_addr()const{return start_phys_addr;}
    phys_addr_t get_end_addr()const{return end_phys_addr;}
};

template<typename ARCH>
class bb_manager{
    typedef typename ARCH::phys_addr_t phys_addr_t;
    typedef basic_block<ARCH> bb_type;
    std::map<phys_addr_t, bb_type *> bb_by_start;
    std::multimap<phys_addr_t, bb_type *> bb_by_end;
    public:
    void add(bb_type *bb){
        typename std::map<phys_addr_t, bb_type *>::const_iterator i = bb_by_start.find(bb->get_start_addr());
        if(i != bb_by_start.end()){abort();}
        bb_by_start[bb->get_start_addr()] = bb;
        bb_by_end.insert(std::make_pair(bb->get_end_addr(), bb));
    }
    bool exists_by_start_addr(phys_addr_t p)const{
        typename std::map<phys_addr_t, bb_type *>::const_iterator i = bb_by_start.find(p);
        return i != bb_by_start.end();
    }
    const bb_type * find_by_start_addr(phys_addr_t p)const{
        typename std::map<phys_addr_t, bb_type *>::const_iterator i = bb_by_start.find(p);
        return i->second;
    }
    int exists_by_end_addr(phys_addr_t p)const{
        return bb_by_end.count(p);
    }
    void invalidate(phys_addr_t from, phys_addr_t to){
        //FIXME invalide bb only in range
        bb_by_start.clear();
        bb_by_end.clear();
    }
};

template<typename ARCH>
class break_point{
    typedef typename ARCH::virt_addr_t virt_addr_t;
    virt_addr_t pc;
    public:
    explicit break_point(virt_addr_t pc) : pc(pc){}
    virt_addr_t get_pc()const{return pc;}
};

template<typename ARCH>
class bp_manager{
    typedef typename ARCH::virt_addr_t virt_addr_t;
    typedef break_point<ARCH> bp_type;
    std::map<virt_addr_t, bp_type *> bps_by_addr;
    public:
    void add(virt_addr_t pc){
        bp_type *const bp = new bp_type(pc);
        bps_by_addr.insert(std::make_pair(pc, bp));
    }
    bool exists(virt_addr_t pc)const{
        return bps_by_addr.find(pc) != bps_by_addr.end();
    }
    void remove(virt_addr_t pc){
        typename std::map<virt_addr_t, bp_type *>::iterator it = bps_by_addr.find(pc);
        jcpu_assert(it != bps_by_addr.end());
        delete it->second;
        bps_by_addr.erase(it);
    }
    const bp_type *find_nearest(virt_addr_t pc)const{
        const typename std::map<virt_addr_t, bp_type *>::const_iterator it = bps_by_addr.lower_bound(pc);
        return it == bps_by_addr.end() ? JCPU_NULLPTR : it->second;
    }
};

class ir_builder_wrapper{
    jcpu_vm_if &vm;
    llvm::IRBuilder<> *const builder;
    void set_pc_str(std::string &)const;
    public:
    ir_builder_wrapper(jcpu_vm_if &, llvm::LLVMContext &);
    llvm::Type *getInt8Ty()const;
    llvm::Type *getInt16Ty()const;
    llvm::Type *getInt32Ty()const;
    llvm::Type *getInt64Ty()const;
    void SetInsertPoint(llvm::BasicBlock *)const;
    llvm::ReturnInst *CreateRet(llvm::Value *)const;
    llvm::CallInst *CreateCall(llvm::Function *, const char * = "")const;
    llvm::CallInst *CreateCall(llvm::Function *, llvm::Value *, const char * = "")const;
    llvm::CallInst *CreateCall2(llvm::Function *, llvm::Value *, llvm::Value *, const char * = "")const;
    llvm::CallInst *CreateCall3(llvm::Function *, llvm::Value *, llvm::Value *, llvm::Value *, const char * = "")const;
    llvm::Value *CreateSelect(llvm::Value *, llvm::Value *, llvm::Value *, const char *)const;
    llvm::Value *CreateZExt(llvm::Value *, llvm::Type *, const char * = "")const;
    llvm::Value *CreateSExt(llvm::Value *, llvm::Type *, const char * = "")const;
    llvm::Value *CreateTrunc(llvm::Value *, llvm::Type *, const char * = "")const;
    llvm::Value *CreateAdd(llvm::Value *, llvm::Value *, const char * = "")const;
    llvm::Value *CreateSub(llvm::Value *, llvm::Value *, const char * = "")const;
    llvm::Value *CreateMul(llvm::Value *, llvm::Value *, const char * = "")const;
    llvm::Value *CreateSDiv(llvm::Value *, llvm::Value *, const char * = "")const;
    llvm::Value *CreateUDiv(llvm::Value *, llvm::Value *, const char * = "")const;
    llvm::Value *CreateOr(llvm::Value *, llvm::Value *, const char * = "")const;
    llvm::Value *CreateAnd(llvm::Value *, llvm::Value *, const char * = "")const;
    llvm::Value *CreateXor(llvm::Value *, llvm::Value *, const char * = "")const;
    llvm::Value *CreateShl(llvm::Value *, llvm::Value *, const char * = "")const;
    llvm::Value *CreateLShr(llvm::Value *, llvm::Value *, const char * = "")const;
    llvm::Value *CreateAShr(llvm::Value *, llvm::Value *, const char * = "")const;
    llvm::Value *CreateICmpEQ(llvm::Value *, llvm::Value *, const char * = "")const;
    llvm::Value *CreateICmpNE(llvm::Value *, llvm::Value *, const char * = "")const;
    llvm::Value *CreateICmpULE(llvm::Value *, llvm::Value *, const char * = "")const;
    llvm::Value *CreateICmpULT(llvm::Value *, llvm::Value *, const char * = "")const;
    llvm::Value *CreateICmpUGE(llvm::Value *, llvm::Value *, const char * = "")const;
    llvm::Value *CreateICmpUGT(llvm::Value *, llvm::Value *, const char * = "")const;
    llvm::Value *CreateICmpSLE(llvm::Value *, llvm::Value *, const char * = "")const;
    llvm::Value *CreateICmpSLT(llvm::Value *, llvm::Value *, const char * = "")const;
    llvm::Value *CreateICmpSGE(llvm::Value *, llvm::Value *, const char * = "")const;
    llvm::Value *CreateICmpSGT(llvm::Value *, llvm::Value *, const char * = "")const;
    llvm::Value *CreateShl(llvm::Value *, unsigned int, const char * = "")const;
    llvm::Value *CreateLShr(llvm::Value *, unsigned int, const char * = "")const;
    llvm::Value *CreateAShr(llvm::Value *, unsigned int, const char * = "")const;
    llvm::Value *CreateAnd(llvm::Value *, unsigned int, const char * = "")const;

};

template<typename ARCH>
class jcpu_vm_base : public ::jcpu::vm::jcpu_vm_if, public ::jcpu::gdb::gdb_target_if{
    llvm::ConstantInt *reg_index(typename ARCH::reg_e r)const;
    llvm::CallInst *gen_get_reg(llvm::Value *reg, const char *mn = "")const;
    llvm::CallInst *gen_set_reg(llvm::Value *reg, llvm::Value *val)const;
    struct set_reg_functor;
    protected:
    typedef typename ARCH::reg_e reg_e;
    typedef typename ARCH::sr_flag_e sr_flag_e;
    typedef typename ARCH::virt_addr_t virt_addr_t;
    typedef typename ARCH::phys_addr_t phys_addr_t;
    typedef typename ARCH::target_ulong target_ulong;
    jcpu_ext_if &ext_ifs;
    llvm::LLVMContext *context;
    ir_builder_wrapper *builder;
    llvm::Module *mod;
    llvm::ExecutionEngine *ee;
    llvm::Function *cur_func;
    llvm::BasicBlock *cur_bb;
    mutable general_reg_cache<ARCH> reg_cache;
    target_ulong (*get_reg_func)(uint16_t);
    void (*set_reg_func)(uint16_t, target_ulong);
    bb_manager<ARCH> bb_man;
    bp_manager<ARCH> bp_man;
    std::stack<std::pair<virt_addr_t, phys_addr_t> > processing_pc;
    int mem_region;
    uint64_t total_icount;

    llvm::Type *get_reg_type()const;
    llvm::Value *gen_get_reg(reg_e r, const char *nm = "")const;
    void gen_set_reg(reg_e r, llvm::Value *val)const;
    llvm::ConstantInt * gen_const(target_ulong val)const;
    llvm::ConstantInt * gen_get_pc()const;
    llvm::Value *gen_cond_code(llvm::Value *cond, llvm::Value *t, llvm::Value *f, const char *mn = "")const;//cond must be 1 or 0
    llvm::CallInst * gen_sw(llvm::Value *addr, unsigned int, llvm::Value *val)const;
    llvm::Value * gen_lw(llvm::Value *addr, unsigned int, const char *mn = "")const;

    //gdb_target_if
    virtual unsigned int get_reg_width()const JCPU_OVERRIDE;
    virtual run_state_e run_continue(bool is_step) JCPU_OVERRIDE;
    virtual uint64_t read_mem_dbg(uint64_t virt_addr, unsigned int len) JCPU_OVERRIDE;
    virtual void write_mem_dbg(uint64_t virt_addr, unsigned int len, uint64_t val) JCPU_OVERRIDE;
    virtual void set_unset_break_point(bool set, uint64_t virt_addr) JCPU_OVERRIDE;
    virtual void start_func(phys_addr_t) = 0;
    llvm::Function *end_func();
    virtual run_state_e run() = 0;
    virtual run_state_e step_exec() = 0;
    phys_addr_t code_v2p(virt_addr_t pc){return static_cast<phys_addr_t>(pc);} //FIXME implement MMU
    explicit jcpu_vm_base(jcpu_ext_if &);
    public:
    void dump_ir()const;
    uint64_t get_total_insn_count()const{return total_icount;}
    virtual uint64_t get_cur_disas_virt_pc()const JCPU_OVERRIDE{return processing_pc.empty() ? -1 : processing_pc.top().first;}
};

template<typename ARCH>
llvm::Type *jcpu_vm_base<ARCH>::get_reg_type()const{
    if(ARCH::reg_bit_width == 32)
        return builder->getInt32Ty();
    else if(ARCH::reg_bit_width == 64)
        return builder->getInt64Ty();
    else
        jcpu_assert(!"Not supported yet");
}

template<typename ARCH>
llvm::ConstantInt *jcpu_vm_base<ARCH>::reg_index(reg_e r)const{
    return llvm::ConstantInt::get(*context, llvm::APInt(16, r));
}

template<typename ARCH>
llvm::CallInst *jcpu_vm_base<ARCH>::gen_get_reg(llvm::Value *reg, const char *mn)const{
    return builder->CreateCall(mod->getFunction("get_reg"), reg, mn);
}

template<typename ARCH>
llvm::CallInst *jcpu_vm_base<ARCH>::gen_set_reg(llvm::Value *reg, llvm::Value *val)const{
    return builder->CreateCall2(mod->getFunction("set_reg"), builder->CreateTrunc(reg, builder->getInt16Ty()), val);
}

template<typename ARCH>
struct jcpu_vm_base<ARCH>::set_reg_functor{
    jcpu_vm_base<ARCH> *const vm_base;
    explicit set_reg_functor(jcpu_vm_base<ARCH> *v) : vm_base(v){}
    void operator () (typename ARCH::reg_e r, llvm::Value *v)const{
        vm_base->gen_set_reg(vm_base->reg_index(r), v);
    }
};

template<typename ARCH>
llvm::Value *jcpu_vm_base<ARCH>::gen_get_reg(reg_e r, const char *nm)const{
    llvm::Value *const latest = reg_cache.get(r);
    if(latest){
        return latest;
    }
    else{
        std::stringstream ss;
        ss << nm << "_reg" << r;
        return reg_cache.set(r, gen_get_reg(reg_index(r), ss.str().c_str()), false);
    }
}

template<typename ARCH>
void jcpu_vm_base<ARCH>::gen_set_reg(reg_e r, llvm::Value *val)const{
    reg_cache.set(r, val, true); 
}

template<typename ARCH>
llvm::ConstantInt * jcpu_vm_base<ARCH>::gen_const(target_ulong val)const{
    return llvm::ConstantInt::get(*context, llvm::APInt(ARCH::reg_bit_width, val));
}

template<typename ARCH>
llvm::ConstantInt * jcpu_vm_base<ARCH>::gen_get_pc()const{
    return gen_const(processing_pc.top().first);
}

template<typename ARCH>
llvm::Value *jcpu_vm_base<ARCH>::gen_cond_code(llvm::Value *cond, llvm::Value *t, llvm::Value *f, const char *mn)const{//cond must be 1 or 0
    using namespace llvm;
    Value *const f_mask = builder->CreateSub(builder->CreateZExt(cond, get_reg_type(), mn), gen_const(1), mn);
    Value *const t_mask = builder->CreateXor(f_mask, gen_const(~static_cast<target_ulong>(0)), mn);
    return builder->CreateOr(builder->CreateAnd(t, t_mask, mn), builder->CreateAnd(f, f_mask, mn), mn); // (t & ~t_mask) | (f & f_mask)
}

template<typename ARCH>
llvm::CallInst * jcpu_vm_base<ARCH>::gen_sw(llvm::Value *addr, unsigned int len, llvm::Value *val)const{
    jcpu_assert(len == 1 || len == 2 || len == 4 || len == 8);
    llvm::Value *const len_llvm = gen_const(len);
    return builder->CreateCall3(
            mod->getFunction("helper_mem_write"),
            builder->CreateZExt(addr, builder->getInt64Ty()), 
            len_llvm,
            builder->CreateZExt(val, builder->getInt64Ty())
            );
}

template<typename ARCH>
llvm::Value * jcpu_vm_base<ARCH>::gen_lw(llvm::Value *addr, unsigned int len, const char *mn)const{
    jcpu_assert(len == 1 || len == 2 || len == 4 || len == 8);
    llvm::Value *const len_llvm = gen_const(len);
    llvm::CallInst *const cinst = builder->CreateCall2(mod->getFunction("helper_mem_read"),
            builder->CreateZExt(addr, builder->getInt64Ty()),
            len_llvm, mn);
    switch(len){
        case 1: return builder->CreateTrunc(cinst, builder->getInt8Ty());
        case 2: return builder->CreateTrunc(cinst, builder->getInt16Ty());
        case 4: return builder->CreateTrunc(cinst, builder->getInt32Ty());
        case 8: return builder->CreateTrunc(cinst, builder->getInt64Ty());
        default: jcpu_assert(!"Never comes here");
    }
    return NULL;
}


    //gdb_target_if
template<typename ARCH>
unsigned int jcpu_vm_base<ARCH>::get_reg_width()const {
    return sizeof(target_ulong) * 8;
}

template<typename ARCH>
::jcpu::gdb::gdb_target_if::run_state_e jcpu_vm_base<ARCH>::run_continue(bool is_step) {
    if(is_step){
        return step_exec();
    }
    else{
        return run();
    }
}

template<typename ARCH>
uint64_t jcpu_vm_base<ARCH>::read_mem_dbg(uint64_t virt_addr, unsigned int len) {
    return ext_ifs.mem_read_dbg(virt_addr, len);
}

template<typename ARCH>
void jcpu_vm_base<ARCH>::write_mem_dbg(uint64_t virt_addr, unsigned int len, uint64_t val) {
    ext_ifs.mem_write_dbg(virt_addr, len, val);
}

template<typename ARCH>
void jcpu_vm_base<ARCH>::set_unset_break_point(bool set, uint64_t virt_addr) {
    const virt_addr_t pc_v(virt_addr);
    if(set){
        bp_man.add(pc_v);
        const phys_addr_t pc_p = code_v2p(pc_v);
        bb_man.invalidate(pc_p, pc_p + static_cast<phys_addr_t>(4));
    }
    else
        bp_man.remove(pc_v);
}

template<typename ARCH>
llvm::Function *jcpu_vm_base<ARCH>::end_func(){
    reg_cache.flush_and_clear(set_reg_functor(this));
    llvm::Value *const pc = gen_get_reg(reg_index(ARCH::REG_PNEXT_PC), "epilogue");
    gen_set_reg(gen_const(ARCH::REG_PC), pc);
#if defined(JCPU_VM_DEBUG) && JCPU_VM_DEBUG > 1
    builder->CreateCall(mod->getFunction("jcpu_vm_dump_regs"));
#endif
    builder->CreateRet(pc);
    llvm::Function *const ret = cur_func;
    cur_func = JCPU_NULLPTR;
    cur_bb = JCPU_NULLPTR;
    return ret;
}

template<typename ARCH>
jcpu_vm_base<ARCH>::jcpu_vm_base(jcpu_ext_if &ifs) : ext_ifs(ifs), cur_func(JCPU_NULLPTR), cur_bb(JCPU_NULLPTR)
{

    context = &llvm::getGlobalContext();
    builder = new ir_builder_wrapper(*this, *context);
    mod = new llvm::Module("openrisc module", *context);
    llvm::EngineBuilder ebuilder(mod);
    ebuilder.setUseMCJIT(true);
    ebuilder.setEngineKind(llvm::EngineKind::JIT);
    ee = ebuilder.create();
    mem_region = 0;
    total_icount = 0;
}

template<typename ARCH>
void jcpu_vm_base<ARCH>::dump_ir()const{
    llvm::PassManager pm;
#if (LLVM_VERSION_MAJOR == 3 && LLVM_VERSION_MINOR >= 5) || LLVM_VERSION_MAJOR >= 4
    pm.add(createPrintModulePass( llvm::outs()));
#else // version <= 3.4
    pm.add(createPrintModulePass(&llvm::outs()));
#endif
    pm.run(*mod);
}


} //end of namespace vm
} //end of namespace jcpu
#endif
