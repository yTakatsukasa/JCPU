#ifndef JCPU_VM_H
#define JCPU_VM_H

#include <stdint.h>
#include <vector>
#include <utility>
#include <stack>

#include "jcpu_llvm_headers.h"
#include "jcpu.h"
#include "jcpu_internal.h"
#include "gdbserver.h"
namespace llvm{class Function;}

namespace jcpu{
namespace vm{

class jcpu_vm_if{
    public:
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

template<typename ARCH>
class jcpu_vm_base : public ::jcpu::vm::jcpu_vm_if, public ::jcpu::gdb::gdb_target_if{
    protected:
    jcpu_ext_if &ext_ifs;
    llvm::LLVMContext *context;
    typedef typename ARCH::reg_e reg_e;
    typedef typename ARCH::sr_flag_e sr_flag_e;
    typedef typename ARCH::virt_addr_t virt_addr_t;
    typedef typename ARCH::phys_addr_t phys_addr_t;
    llvm::IRBuilder<> *builder;
    llvm::Module *mod;
    llvm::ExecutionEngine *ee;
    llvm::Function *cur_func;
    llvm::BasicBlock *cur_bb;
    typedef typename ARCH::target_ulong target_ulong;
    llvm::Type *get_reg_type()const{
        if(ARCH::reg_bit_width == 32)
            return builder->getInt32Ty();
        else if(ARCH::reg_bit_width == 64)
            return builder->getInt64Ty();
        else
            jcpu_assert(!"Not supported yet");
    }
    llvm::ConstantInt *reg_index(reg_e r)const{
        return llvm::ConstantInt::get(*context, llvm::APInt(16, r));
    }
    llvm::CallInst *gen_get_reg(llvm::Value *reg, const char *mn = "")const{
        return builder->CreateCall(mod->getFunction("get_reg"), reg, mn);
    }
    llvm::CallInst *gen_get_reg(reg_e r, const char *mn = "")const{
        return gen_get_reg(reg_index(r), mn);
    }
    llvm::CallInst *gen_set_reg(llvm::Value *reg, llvm::Value *val, const char *mn = "")const{
        return builder->CreateCall2(mod->getFunction("set_reg"), reg, val, mn);
    }
    llvm::CallInst *gen_set_reg(reg_e r, llvm::Value *val, const char *mn = "")const{
        return gen_set_reg(reg_index(r), val, mn);
    }
    llvm::ConstantInt * gen_const(target_ulong val)const{
        return llvm::ConstantInt::get(*context, llvm::APInt(ARCH::reg_bit_width, val));
    }
    llvm::ConstantInt * gen_get_pc()const{
        return gen_const(processing_pc.top().first);
    }
    llvm::Value *gen_cond_code(llvm::Value *cond, llvm::Value *t, llvm::Value *f, const char *mn = "")const{//cond must be 1 or 0
        using namespace llvm;
        Value *const f_mask = builder->CreateSub(builder->CreateZExt(cond, get_reg_type(), mn), gen_const(1), mn);
        Value *const t_mask = builder->CreateXor(f_mask, gen_const(~static_cast<target_ulong>(0)), mn);
        return builder->CreateOr(builder->CreateAnd(t, t_mask, mn), builder->CreateAnd(f, f_mask, mn), mn); // (t & ~t_mask) | (f & f_mask)
    }
    llvm::CallInst * gen_sw(llvm::Value *addr, llvm::Value *len, llvm::Value *val, const char *mn = "")const{
        return builder->CreateCall3(mod->getFunction("helper_mem_write"), addr, len, val, mn);
    }
    llvm::CallInst * gen_lw(llvm::Value *addr, llvm::Value *len, const char *mn = "")const{
        return builder->CreateCall2(mod->getFunction("helper_mem_read"), addr, len, mn);
    }

    target_ulong (*get_reg_func)(uint16_t);
    void (*set_reg_func)(uint16_t, target_ulong);
    bb_manager<ARCH> bb_man;
    bp_manager<ARCH> bp_man;
    std::stack<std::pair<virt_addr_t, phys_addr_t> > processing_pc;
    int mem_region;
    uint64_t total_icount;

    //gdb_target_if
    virtual unsigned int get_reg_width()const JCPU_OVERRIDE{
        return sizeof(target_ulong) * 8;
    }
    //virtual void get_reg_value(std::vector<uint64_t> &)const JCPU_OVERRIDE;
    //virtual void set_reg_value(unsigned int, uint64_t)JCPU_OVERRIDE;
    virtual run_state_e run_continue(bool is_step) JCPU_OVERRIDE{
        if(is_step){
            return step_exec();
        }
        else{
            return run();
        }
    }
    virtual uint64_t read_mem_dbg(uint64_t virt_addr, unsigned int len) JCPU_OVERRIDE{
        return ext_ifs.mem_read_dbg(virt_addr, len);
    }
    virtual void write_mem_dbg(uint64_t virt_addr, unsigned int len, uint64_t val) JCPU_OVERRIDE{
        ext_ifs.mem_write_dbg(virt_addr, len, val);
    }
    virtual void set_unset_break_point(bool set, uint64_t virt_addr) JCPU_OVERRIDE{
        const virt_addr_t pc_v(virt_addr);
        if(set){
            bp_man.add(pc_v);
            const phys_addr_t pc_p = code_v2p(pc_v);
            bb_man.invalidate(pc_p, pc_p + static_cast<phys_addr_t>(4));
        }
        else
            bp_man.remove(pc_v);
    }
    virtual bool disas_insn(virt_addr_t, int *) = 0;
    void start_func(phys_addr_t);
    llvm::Function * end_func();
    const basic_block<ARCH> *disas(virt_addr_t, int, const break_point<ARCH> *);
    virtual run_state_e run() = 0;
    virtual run_state_e step_exec() = 0;
    phys_addr_t code_v2p(virt_addr_t pc){return static_cast<phys_addr_t>(pc);} //FIXME implement MMU

    explicit jcpu_vm_base(jcpu_ext_if &ifs) : ext_ifs(ifs), cur_func(JCPU_NULLPTR), cur_bb(JCPU_NULLPTR)
    {

        context = &llvm::getGlobalContext();
        builder = new llvm::IRBuilder<>(*context);
        mod = new llvm::Module("openrisc module", *context);
        llvm::EngineBuilder ebuilder(mod);
        ebuilder.setUseMCJIT(true);
        ebuilder.setEngineKind(llvm::EngineKind::JIT);
        ee = ebuilder.create();
        mem_region = 0;
        total_icount = 0;
    }
    public:
    void dump_ir()const{
        llvm::PassManager pm;
        pm.add(createPrintModulePass(&llvm::outs()));
        pm.run(*mod);
    }
    uint64_t get_total_insn_count()const{return total_icount;}
};


} //end of namespace vm
} //end of namespace jcpu
#endif
