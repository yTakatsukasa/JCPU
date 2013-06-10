#include <cstdio>
#include <iostream>
#include <iomanip>
#include <stack>

#include <llvm/LLVMContext.h>
#include <llvm/IRBuilder.h> 
#include <llvm/Module.h>
#include <llvm/ExecutionEngine/JIT.h> 
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/PrettyStackTrace.h>
#include <llvm/Support/Signals.h>
#include <llvm/Support/Debug.h> //EnableDebugBuffering
#include <llvm/Analysis/Verifier.h> //verifyModule
#include <llvm/PassManager.h> //PassManager
#include <llvm/Assembly/PrintModulePass.h> //PrintModulePass
#include <llvm/Support/raw_ostream.h> //outs()
#include <llvm/Instructions.h> //LoadInst

#include "qcpu_vm.h"
#include "gdbserver.h"
#include "openrisc.h"

//#define QCPU_OPENRISC_DEBUG 1


namespace {
#define qcpu_or_disas_assert(cond) do{if(!(cond)){ \
    /*dump_ir();*/ \
    dump_regs(); \
    llvm::Function *const f = end_func(); \
    target_ulong (*const func)() = reinterpret_cast<target_ulong (*)()>(ee->getPointerToFunction(f)); \
    (*func)(); \
    dump_regs(); \
    std::cerr << "Failed " << #cond << " at " << __FILE__ << " " << std::dec << __LINE__ << " in " << __PRETTY_FUNCTION__ << std::endl; std::abort();}} while(false)

template<unsigned int bit, unsigned int width, typename T>
inline T bit_sub(T v){
    return (v >> bit) & ((T(1) << width) - 1);
}



void make_set_get(llvm::Module *, llvm::GlobalVariable *);
void make_mem_access(llvm::Module *);
void make_debug_func(llvm::Module *);


} //end of unnamed namespace

namespace qcpu{
namespace openrisc{

enum reg_e{
    REG_GR00, REG_GR01, REG_GR02, REG_GR03,
    REG_GR04, REG_GR05, REG_GR06, REG_GR07,
    REG_GR08, REG_GR09, REG_LR = REG_GR09, REG_GR10, REG_GR11,
    REG_GR12, REG_GR13, REG_GR14, REG_GR15,
    REG_GR16, REG_GR17, REG_GR18, REG_GR19,
    REG_GR20, REG_GR21, REG_GR22, REG_GR23,
    REG_GR24, REG_GR25, REG_GR26, REG_GR27,
    REG_GR28, REG_GR29, REG_GR30, REG_GR31,
    REG_PC, REG_SR, REG_CPUCFGR, REG_PNEXT_PC, NUM_REGS
};
enum sr_flag_e{
    SR_SM = 0, SR_TEE, SR_IEE, SR_DCE, SR_ICE, SR_DME, SR_IME, SR_LEE,
    SR_CE, SR_F, SR_CY, SR_OV, SR_OVE, SR_DSX, SR_EPH, SR_FO, 
    SR_SUMRA
};
enum cpucfgr_bit_e{
    CPUCFGR_ND = 10
};
typedef uint32_t target_ulong;
const static unsigned int reg_bit_width = 32;


template<typename T, int TAG>
class primitive_type_holder{
    T v;
    typedef primitive_type_holder<T, TAG> this_type;
    public:
    explicit primitive_type_holder(T v) : v(v){}
    operator const T ()const{return v;}
    const this_type operator + (const this_type &other)const{
        return this_type(v + other.v);
    }
};

typedef primitive_type_holder<target_ulong, 0> virt_addr_t;
typedef primitive_type_holder<target_ulong, 1> phys_addr_t;


class basic_block{
    typedef target_ulong (*basic_block_func_t)();

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
    virt_addr_t exec()const{return static_cast<virt_addr_t>((*func_ptr)());}
    unsigned int get_icount()const{return num_insn;}
    phys_addr_t get_start_addr()const{return start_phys_addr;}
    phys_addr_t get_end_addr()const{return end_phys_addr;}
};

class bb_manager{
    std::map<phys_addr_t, basic_block *> bb_by_start;
    std::multimap<phys_addr_t, basic_block *> bb_by_end;
    public:
    void add(basic_block *bb){
        std::map<phys_addr_t, basic_block *>::const_iterator i = bb_by_start.find(bb->get_start_addr());
        if(i != bb_by_start.end()){abort();}
        bb_by_start[bb->get_start_addr()] = bb;
        bb_by_end.insert(std::make_pair(bb->get_end_addr(), bb));
    }
    bool exists_by_start_addr(phys_addr_t p)const{
        std::map<phys_addr_t, basic_block *>::const_iterator i = bb_by_start.find(p);
        return i != bb_by_start.end();
    }
    const basic_block * find_by_start_addr(phys_addr_t p)const{
        std::map<phys_addr_t, basic_block *>::const_iterator i = bb_by_start.find(p);
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


class openrisc_vm : public ::qcpu::vm::qcpu_vm_if, public ::qcpu::gdb::gdb_target_if{
    qcpu_ext_if &ext_ifs;
    llvm::LLVMContext *context;
    llvm::IRBuilder<> *builder;
    llvm::Module *mod;
    llvm::ExecutionEngine *ee;
    llvm::Function *cur_func;
    llvm::BasicBlock *cur_bb;
    llvm::Type *get_reg_type()const{
        return builder->getInt32Ty();
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
        return llvm::ConstantInt::get(*context, llvm::APInt(reg_bit_width, val));
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
    llvm::Value *gen_set_sr(sr_flag_e flag, llvm::Value *val, const char *mn = "")const{//val must be 0 or 1
        using namespace llvm;
        Value *const sr = gen_get_reg(REG_SR, mn);
        Value *const drop_mask = gen_const(~(static_cast<target_ulong>(1) << flag));
        Value *const shifted_val = builder->CreateShl(builder->CreateZExt(val, get_reg_type()), flag, mn);
        Value *const new_sr = builder->CreateOr(builder->CreateAnd(sr, drop_mask, mn), shifted_val, mn);
        return gen_set_reg(REG_SR, new_sr, mn);
    }
    target_ulong (*get_reg_func)(uint16_t);
    void (*set_reg_func)(uint16_t, target_ulong);
    bb_manager bb_man;
    std::stack<std::pair<virt_addr_t, phys_addr_t> > processing_pc;
    int mem_region;

    //gdb_target_if
    virtual unsigned int get_reg_width()const QCPU_OVERRIDE;
    virtual void get_reg_value(std::vector<uint64_t> &)const QCPU_OVERRIDE;
    virtual void set_reg_value(unsigned int, uint64_t)QCPU_OVERRIDE;
    virtual void run_continue(bool) QCPU_OVERRIDE;
    virtual uint64_t read_mem_dbg(uint64_t, unsigned int) QCPU_OVERRIDE;
    virtual void write_mem_dbg(uint64_t, unsigned int, uint64_t) QCPU_OVERRIDE;
    virtual void set_unset_break_point(bool, unsigned int, uint64_t) QCPU_OVERRIDE;

    bool is_next_insn_use_flag(phys_addr_t);
    bool disas_insn(virt_addr_t);
    bool disas_arith(target_ulong);
    bool disas_logical(target_ulong);
    bool disas_compare_immediate(target_ulong);
    bool disas_compare(target_ulong);
    bool disas_others(target_ulong);
    void start_func(phys_addr_t);
    llvm::Function * end_func();
    const basic_block *disas(virt_addr_t, int);
    void step_exec();
    public:
    explicit openrisc_vm(qcpu_ext_if &);
    void run();
    void dump_ir()const;
    virtual void dump_regs()const QCPU_OVERRIDE;
};

openrisc_vm::openrisc_vm(qcpu_ext_if &ifs) : ext_ifs(ifs), cur_func(QCPU_NULLPTR), cur_bb(QCPU_NULLPTR)
{
    context = &llvm::getGlobalContext();
    builder = new llvm::IRBuilder<>(*context);
    mod = new llvm::Module("openrisc module", *context);
    llvm::EngineBuilder ebuilder(mod);
    ebuilder.setUseMCJIT(true);
    ebuilder.setEngineKind(llvm::EngineKind::JIT);
    ee = ebuilder.create();
    mem_region = 0;


    const unsigned int bit = sizeof(target_ulong) * 8;
    const unsigned int num_regs = NUM_REGS;
    llvm::ArrayType *const ATy = llvm::ArrayType::get(llvm::IntegerType::get(*context, bit), num_regs);
    std::vector<llvm::Constant*> Initializer;
    Initializer.reserve(num_regs);

    for(unsigned int i = 0; i < num_regs; ++i){
        const unsigned int reg_init_val = (i == REG_PC || i == REG_PNEXT_PC) ? 0x100 : 0;
        llvm::ConstantInt *const ivc = gen_const(reg_init_val);
        Initializer.push_back(ivc);
    }

    llvm::Constant *const init = llvm::ConstantArray::get(ATy, Initializer);
    llvm::GlobalVariable *const global_regs = new llvm::GlobalVariable(*mod, ATy, true, llvm::GlobalValue::CommonLinkage, init, "regs");
    global_regs->setAlignment(bit / 8);

    make_set_get(mod, global_regs);
    make_mem_access(mod);
    make_debug_func(mod);

    set_reg_func = reinterpret_cast<void (*)(uint16_t, target_ulong)>(ee->getPointerToFunction(mod->getFunction("set_reg")));
    get_reg_func = reinterpret_cast<target_ulong (*)(uint16_t)>(ee->getPointerToFunction(mod->getFunction("get_reg")));

    void (*const set_mem_access_if)(qcpu_ext_if *) = reinterpret_cast<void(*)(qcpu_ext_if*)>(ee->getPointerToFunction(mod->getFunction("set_mem_access_if")));
    set_mem_access_if(&ext_ifs);
    void (*const set_qcpu_vm_ptr)(qcpu_vm_if *) = reinterpret_cast<void(*)(qcpu_vm_if*)>(ee->getPointerToFunction(mod->getFunction("set_qcpu_vm_ptr")));
    set_qcpu_vm_ptr(this);


}


unsigned int openrisc_vm::get_reg_width()const{
    return sizeof(target_ulong) * 8;
}

void openrisc_vm::get_reg_value(std::vector<uint64_t> &regs)const{
    regs.clear();
    for(unsigned int i = 0; i < 32; ++i){
        regs.push_back(get_reg_func(i));
    }
    //regs.push_back(get_reg_func(REG_PC));
    regs.push_back(get_reg_func(REG_PNEXT_PC));
}
void openrisc_vm::set_reg_value(unsigned int reg_idx, uint64_t reg_val){
    qcpu_assert(reg_idx < 32 + 1);
    set_reg_func(reg_idx, reg_val);
}

void openrisc_vm::run_continue(bool is_step){
    if(is_step){
        step_exec();
    }
    else{
        run();
    }
}

uint64_t openrisc_vm::read_mem_dbg(uint64_t virt_addr, unsigned int len){
    return ext_ifs.mem_read_dbg(virt_addr, len);
}
void openrisc_vm::write_mem_dbg(uint64_t virt_addr, unsigned int len, uint64_t val){
    ext_ifs.mem_write_dbg(virt_addr, len, val);
}

void openrisc_vm::set_unset_break_point(bool set, unsigned int bid, uint64_t virt_addr){
    qcpu_assert(!"Breakpoint is not supported yet");
}

bool openrisc_vm::disas_insn(virt_addr_t pc_v){
    const phys_addr_t pc(pc_v);//FIXME translate via MMU
    struct push_and_pop_pc{
        openrisc_vm &vm;
        push_and_pop_pc(openrisc_vm &vm, virt_addr_t pc_v, phys_addr_t pc_p) : vm(vm){
            vm.processing_pc.push(std::make_pair(pc_v, pc_p));
        }
        ~push_and_pop_pc(){

            vm.gen_set_reg(REG_PC, vm.gen_const(vm.processing_pc.top().first + 4));
#if defined(QCPU_OPENRISC_DEBUG) && QCPU_OPENRISC_DEBUG > 1
            //vm.gen_set_reg(REG_PC, vm.gen_const(vm.processing_pc.top().second));
#endif
            vm.processing_pc.pop();
        }
    } push_and_pop_pc(*this, pc_v, pc);
    const target_ulong insn = ext_ifs.mem_read(pc, sizeof(target_ulong));
    const unsigned int kind = bit_sub<26, 6>(insn);
#if defined(QCPU_OPENRISC_DEBUG) && QCPU_OPENRISC_DEBUG > 0
    std::cout << std::hex << "pc:" << pc << " INSN:" << std::setw(8) << std::setfill('0') << insn << " kind:" << kind << std::endl;
#endif
#if defined(QCPU_OPENRISC_DEBUG) && QCPU_OPENRISC_DEBUG > 1
    builder->CreateCall(mod->getFunction("qcpu_vm_dump_regs"));
#endif
    switch(kind){
        case 0x08: //system
            qcpu_or_disas_assert(!"Not implemented yet");
            break;
        case 0x2E: //logical
            return disas_logical(insn);
        case 0x2F: //compare
            return disas_compare_immediate(insn);
        case 0x31: //media
            qcpu_or_disas_assert(!"Not implemented yet");
            break;
        case 0x32: //floating point
            qcpu_or_disas_assert(!"Not implemented yet");
            break;
        case 0x38: //arithmetric
            return disas_arith(insn);
            break;
        case 0x39: //compare
            return disas_compare(insn);
        default: //others
            return disas_others(insn);
    }

}

const basic_block *openrisc_vm::disas(virt_addr_t start_pc_, int max_insn){
    const phys_addr_t start_pc(start_pc_);
    start_func(start_pc);
    target_ulong pc;
    if(max_insn < 0){
        bool done = false;
        for(pc = start_pc; !done; pc += 4){
            done = disas_insn(virt_addr_t(pc));
        }
    }
    else{
        qcpu_assert(max_insn ==  1); //only step exec is supported
        const bool done = disas_insn(start_pc_);
        pc = start_pc + (done ? 8 : 4);
        if(!done) gen_set_reg(REG_PNEXT_PC, gen_const(pc));
    }
    std::cerr << "before end_func" << std::endl;
    llvm::Function *const f = end_func();
    basic_block *const bb = new basic_block(start_pc, phys_addr_t(pc - 4), f, ee, 1 + (pc - start_pc) / 4); //considering delay slot
    bb_man.add(bb);
    std::cerr << "BB is generated" << std::endl;
    return bb;
}

bool openrisc_vm::disas_arith(target_ulong insn){
    using namespace llvm;
    const target_ulong op = bit_sub<0, 4>(insn);
    const target_ulong op2 = bit_sub<8, 2>(insn);
    ConstantInt *const rD = ConstantInt::get(*context, APInt(5, bit_sub<21, 5>(insn)));
    ConstantInt *const rA = ConstantInt::get(*context, APInt(5, bit_sub<16, 5>(insn)));
    ConstantInt *const rB = ConstantInt::get(*context, APInt(5, bit_sub<11, 5>(insn)));
    if(op2 == 0){
        switch(op){
            case 0x00://l.add rD = aA + rB, SR[CY] = unsigned overflow(carry), SR[OV] = signed overflow
                gen_set_reg(rD, builder->CreateAdd(gen_get_reg(rA), gen_get_reg(rB), "l.add"));
                //FIXME overflow
                return false;
            case 0x02://l.sub rD = rA - rB, SR[CY] = unsigned overflow(carry), SR[OV] = signed overflow
                gen_set_reg(rD, builder->CreateSub(gen_get_reg(rA), gen_get_reg(rB), "l.sub"));
                //FIXME overflow
                return false;
            case 0x04: //l.or rD = rA | rB
                gen_set_reg(rD, builder->CreateOr(gen_get_reg(rA, "l.or_A"), gen_get_reg(rB, "l.or_B"), "l.or"), "l.or_D");
                return false;
            case 0x08: //l.srl rD = rA >> rB[4:0]
                {
                    Value *const rega = gen_get_reg(rA, "l.srl_A");
                    Value *const regb5 = builder->CreateTrunc(gen_get_reg(rB, "l.srl_B"), IntegerType::get(*context, 5), "l.srl_B5");
                    gen_set_reg(rD, builder->CreateLShr(rega, regb5, "l.srl"), "l.srl_D");
                }
                return false;
            default:
                qcpu_or_disas_assert(!"Not implemented yet");
                break;
        }
    }
    else if(op2 == 3){
        switch(op){
            case 0x06: //l.mul rD = rA * rB, SR[OV] = signed overflow
                gen_set_reg(rD, builder->CreateMul(gen_get_reg(rA, "l.mul_A"), gen_get_reg(rB, "lmul_B"), "l.mul"), "l.mul_D");
                //FIXME Overflow
                return false;

            default:
                qcpu_or_disas_assert(!"Not implemented yet");
                break;
        }
    }
    qcpu_or_disas_assert(!"Never comes here");
}

bool openrisc_vm::disas_logical(target_ulong insn){
    using namespace llvm;
    const target_ulong op = bit_sub<6, 2>(insn);
    ConstantInt *const rD = ConstantInt::get(*context, APInt(5, bit_sub<21, 5>(insn)));
    ConstantInt *const rA = ConstantInt::get(*context, APInt(5, bit_sub<16, 5>(insn)));
    //ConstantInt *const L_64 = ConstantInt::get(*context, APInt(6, bit_sub<0, 6>(insn)));
    ConstantInt *const L_32 = ConstantInt::get(*context, APInt(5, bit_sub<0, 5>(insn)));
    switch(op){
        case 0x0: //l.slli rD = rA << L
            gen_set_reg(rD, builder->CreateShl(gen_get_reg(rA, "l.slli"), L_32, "l.slli"), "l.slli");
            return false;
        case 0x1: //l.srli rD = rA >> L logical
            gen_set_reg(rD, builder->CreateLShr(gen_get_reg(rA, "l.srli"), L_32, "l.srli"), "l.srli");
            return false;
        case 0x2: //l.srai rD = rA >> L arith
            gen_set_reg(rD, builder->CreateAShr(gen_get_reg(rA, "l.srai"), L_32, "l.srai"), "l.srai");
            return false;
        default:
            qcpu_or_disas_assert(!"Not implemented yet");
            break;
    }
    qcpu_or_disas_assert(!"Never comes here");
}

bool openrisc_vm::disas_compare_immediate(target_ulong insn){
    using namespace llvm;
    const target_ulong op0 = bit_sub<21, 5>(insn);
    ConstantInt *const rA = ConstantInt::get(*context, APInt(5, bit_sub<16, 5>(insn)));
    ConstantInt *const I16 = ConstantInt::get(*context, APInt(16, bit_sub<0, 16>(insn)));
    Value *const I16s = builder->CreateSExt(I16, get_reg_type());

    switch(op0){
        case 0x00: //l.sfeqi SR[F] = rA == sext(I16)
            gen_set_sr(SR_F, builder->CreateICmpEQ(gen_get_reg(rA), I16s, "l.sfeqi"), "l.sfeqi");
            return false;
        case 0x01: //l.sfnei SR[F} = rA != sext(I16)
            gen_set_sr(SR_F, builder->CreateICmpNE(gen_get_reg(rA), I16s, "l.sfnwi"), "l.sfnei");
            return false;
        case 0x02: //l.sfgtui SR[F] = rA > sext(I16)
            gen_set_sr(SR_F, builder->CreateICmpUGT(gen_get_reg(rA), I16s, "l.sfgtui"), "l.sfgtui");
            return false;
        case 0x05: //l.sfleui SR[F} = rA <= sext(I16)
            gen_set_sr(SR_F, builder->CreateICmpULE(gen_get_reg(rA), I16s, "l.sfleui"), "l.sfleui");
            return false;
        case 0x0A: //l.sfgtsi SR[F] = rA > sext(I16)
            gen_set_sr(SR_F, builder->CreateICmpSGT(gen_get_reg(rA), I16s, "l.sfgtsi"), "l.sfgtsi");
            return false;
        case 0x0B: //l.sfgesi SR[F] = rA >= sext(I16)
            gen_set_sr(SR_F, builder->CreateICmpSGE(gen_get_reg(rA), I16s, "l.sfgesi"), "l.sfgesi");
            return false;
        case 0x0D: //l.sflesi SR[F] = rA <= sext(I16)
            gen_set_sr(SR_F, builder->CreateICmpSLE(gen_get_reg(rA, "l.sflesi_A"), I16s, "l.sflesi_I"), "l.sflesi");
            return false;
        default:
            qcpu_or_disas_assert(!"Not implemented yet");
            break;
    }
}


bool openrisc_vm::disas_compare(target_ulong insn){
    using namespace llvm;
    const target_ulong op0 = bit_sub<21, 5>(insn);
    ConstantInt *const rA = ConstantInt::get(*context, APInt(5, bit_sub<16, 5>(insn)));
    ConstantInt *const rB = ConstantInt::get(*context, APInt(5, bit_sub<11, 5>(insn)));

    switch(op0){
        case 0x00: //l.sfeq SR[F] = rA == rB
            gen_set_sr(SR_F, builder->CreateICmpEQ(gen_get_reg(rA, "l.sfeq"), gen_get_reg(rB, "l.sfeq"), "l.sfeq"), "l.sfeq");
            return false;
        case 0x01: //l.sfne SR[F] <= rA != rB
            gen_set_sr(SR_F, builder->CreateICmpNE(gen_get_reg(rA, "l.sfne"), gen_get_reg(rB, "l.sfne"), "l.sfne"), "l.sfne");
            return false;
        case 0x02: //l.sfgtu SR[F] <= rA > rB
            gen_set_sr(SR_F, builder->CreateICmpUGT(gen_get_reg(rA, "l.sfgtu_A"), gen_get_reg(rB, "l.sfgtu_B"), "l.sfgtu"), "l.sfgtu_SRF");
            return false;
        case 0x03: //l.sfgeu SR[F] <= rA >= rB
            gen_set_sr(SR_F, builder->CreateICmpUGE(gen_get_reg(rA, "l.sfgeu_A"), gen_get_reg(rB, "l.sfgeu_B"), "l.sfgeu"), "l.sfgeu_SRF");
            return false;
        case 0x05: //l.sfleu SR[F] <= rA <= rB
            gen_set_sr(SR_F, builder->CreateICmpULE(gen_get_reg(rA, "l.sfleu_A"), gen_get_reg(rB, "l.sfleu_B"), "l.sfleu"), "l.sfleu_SRF");
            return false;
        case 0x0B: //l.sfges SR[F] <= rA >= rB
            gen_set_sr(SR_F, builder->CreateICmpSGE(gen_get_reg(rA), gen_get_reg(rB)));
            return false;
        case 0x0C: //l.sflts SR[F] <= rA < rB
            gen_set_sr(SR_F, builder->CreateICmpSLT(gen_get_reg(rA, "l.sflts_A"), gen_get_reg(rB, "l.sflts_B"), "l.sflts"), "l.sflts_SRF");
            return false;
        case 0x0D: //l.sfles SR[F} <= rA <= rB
            gen_set_sr(SR_F, builder->CreateICmpSLE(gen_get_reg(rA), gen_get_reg(rB)));
            return false;
        default:
            qcpu_or_disas_assert(!"Not implemented yet");
            break;
    }
}

bool openrisc_vm::disas_others(target_ulong insn){
    using namespace llvm;
    const target_ulong op0 = bit_sub<26, 6>(insn);
    const target_ulong op1 = bit_sub<24, 2>(insn);
    ConstantInt *const rD = ConstantInt::get(*context, APInt(5, bit_sub<21, 5>(insn)));
    ConstantInt *const rA = ConstantInt::get(*context, APInt(5, bit_sub<16, 5>(insn)));
    ConstantInt *const rB = ConstantInt::get(*context, APInt(5, bit_sub<11, 5>(insn)));
    //const target_ulong lo6 = bit_sub<5, 6>(insn);
    //const target_ulong k5 = bit_sub<0, 5>(insn);
    ConstantInt *const lo16 = ConstantInt::get(*context, APInt(16, bit_sub<0, 16>(insn)));
    //const target_ulong i5 = bit_sub<21, 5>(insn);
    ConstantInt *const I11 = ConstantInt::get(*context, APInt(11, bit_sub<0, 11>(insn)));
    ConstantInt *const n26 = ConstantInt::get(*context, APInt(26, bit_sub<0, 26>(insn)));
    ConstantInt *const I = ConstantInt::get(*context, APInt(16, (bit_sub<21, 5>(insn) << 11) | bit_sub<0, 11>(insn)));
#if defined(QCPU_OPENRISC_DEBUG) && QCPU_OPENRISC_DEBUG > 0
    std::cerr << "op0:" << std::hex << op0 << " rA:" << bit_sub<21, 5>(insn) << " rB:" << bit_sub<11, 5>(insn) << " rD" << bit_sub<21, 5>(insn) << " lo16:" << bit_sub<0, 16>(insn) << std::endl;
#endif
    switch(op0){
        case 0x00://l.j PC = sext(n26) << 2 + PC
            {
                static const char *const mn = "l.j";
                ConstantInt *const pc = gen_get_pc();
                Value *const pc_offset = builder->CreateShl(builder->CreateSExt(n26, get_reg_type(), mn), 2, mn);
                gen_set_reg(REG_PNEXT_PC, builder->CreateAdd(pc, pc_offset, mn), mn);
                const bool ret = disas_insn(processing_pc.top().first + static_cast<virt_addr_t>(4)); //delay slot
                qcpu_or_disas_assert(!ret);
                return true;
            }
        case 0x01://l.jal
            {
                ConstantInt *const pc = gen_get_pc();
                Value *const pc_offset = builder->CreateShl(builder->CreateSExt(n26, get_reg_type()), 2);
                gen_set_reg(REG_PNEXT_PC, builder->CreateAdd(pc, pc_offset));
                Value *const nd_bit = builder->CreateAnd(builder->CreateLShr(gen_get_reg(REG_CPUCFGR), gen_const(CPUCFGR_ND)), gen_const(1));
                gen_set_reg(REG_LR, builder->CreateAdd(pc, gen_cond_code(nd_bit, gen_const(4), gen_const(8))));//check spr
                const bool ret = disas_insn(processing_pc.top().first + static_cast<virt_addr_t>(4)); //delay slot
                qcpu_or_disas_assert(!ret);
                return true;
            }
        case 0x03: //l.bnf
        case 0x04: //l.bf
            {
                const bool is_bnf = op0 == 0x03;
                const char *const mn = is_bnf ? "l.bnf" : "l.bf";
                const virt_addr_t &pc = processing_pc.top().first;
                Value *const flag = gen_get_reg(REG_SR);
                Value *const shifted_flag = builder->CreateAnd(builder->CreateLShr(flag, SR_F), 1, mn);
                Value *const pc_offset = builder->CreateShl(builder->CreateSExt(lo16, get_reg_type(), mn), 2, mn);
                Value *not_taken_pc = gen_const(pc + 8);
                Value *taken_pc = builder->CreateAdd(gen_const(pc), pc_offset, mn);
                if(is_bnf) std::swap(taken_pc, not_taken_pc);
                Value *const next_pc = gen_cond_code(shifted_flag, taken_pc, not_taken_pc);
                gen_set_reg(REG_PNEXT_PC, next_pc);
                gen_set_reg(REG_SR, builder->CreateAnd(flag, ~(static_cast<target_ulong>(1) << SR_F), mn));
                const bool ret = disas_insn(pc + static_cast<virt_addr_t>(4)); //delay slot
                qcpu_or_disas_assert(!ret);
            }
            return true;
        case 0x05:
            if(op1 == 1){//l.nop
                return false;
            }
            else{
                qcpu_or_disas_assert(!"Not implemented yet");
            }
            break;
        case 0x06:
            if(!bit_sub<16, 1>(insn)){//l.movhi rD = extz(lo16) << 16
                static const char *const mn = "l.movhi";
                gen_set_reg(rD, builder->CreateShl(builder->CreateZExt(lo16, get_reg_type(), mn), gen_const(16), mn), mn);
                return false;
            }
            else{
                qcpu_or_disas_assert(!"Not implemented yet");
            }
            break;
        case 0x11: //l.jr PC = rB
            {
                static const char *const mn = "l.jr";
                gen_set_reg(REG_PNEXT_PC, gen_get_reg(rB, mn), mn);
                const bool ret = disas_insn(processing_pc.top().first + static_cast<virt_addr_t>(4)); //delay slot
                qcpu_or_disas_assert(!ret);
                return true;
            }
            return true;
        case 0x21: //l.lwz rD = (rA + sext(I))
            {
                Value *const addr = builder->CreateAdd(gen_get_reg(rA), builder->CreateSExt(I11, get_reg_type()));
                Value *const dat = gen_lw(addr, gen_const(sizeof(target_ulong)));
                gen_set_reg(rD, dat);
            }
            return false;
        case 0x23: //l.lbz rD = zext(lb(sext(lo16) + rA))
            {
                static const char *const mn = "l.lbz";
                Value *const addr = builder->CreateAdd(gen_get_reg(rA, mn), builder->CreateSExt(lo16, get_reg_type(), mn), mn);
                Value *const dat = builder->CreateTrunc(gen_lw(addr, gen_const(1), mn), builder->getInt8Ty(), mn);
                gen_set_reg(rD, builder->CreateZExt(dat, get_reg_type(), mn), mn);
            }
            return false;
        case 0x24: //l.lbs rD = sext(lb(sext(lo16) + rA))
            {
                static const char *const mn = "l.lbs";
                Value *const addr = builder->CreateAdd(gen_get_reg(rA, mn), builder->CreateSExt(lo16, get_reg_type(), mn), mn);
                Value *const dat = builder->CreateTrunc(gen_lw(addr, gen_const(1), mn), builder->getInt8Ty(), mn);
                gen_set_reg(rD, builder->CreateSExt(dat, get_reg_type(), mn), mn);
            }
            return false;
        case 0x27: //l.addi  (set rD (add rA lo16))
            gen_set_reg(rD, builder->CreateAdd(gen_get_reg(rA), builder->CreateSExt(lo16, get_reg_type())));
            //FIXME CARRY, OVERFLOW
            return false;
        case 0x29: //l.andi rD = rA & extz(lo16)
            gen_set_reg(rD, builder->CreateAnd(gen_get_reg(rA, "l.andi"), builder->CreateZExt(lo16, get_reg_type(), "l.andi"), "l.andi"));
            return false;
        case 0x2A: //l.ori (set rD (or rA (and lo16 65535)))
            gen_set_reg(rD, builder->CreateOr(gen_get_reg(rA), builder->CreateZExt(lo16, get_reg_type())));
            return false;
        case 0x2C: //l.muli
            {
                //Value *const mul = builder->CreateMul(gen_get_reg(rA), builder->CreateSExt(lo16, builder->getInt64Ty()));
                Value *const mul = builder->CreateMul(gen_get_reg(rA), builder->CreateSExt(lo16, get_reg_type()));
                gen_set_reg(rD, mul);
                //FIXME Overflow ? 
            }
            return false;
        case 0x2D: //l.mfspr
            std::cerr << "l.mfspr is not implemented yet" << std::endl;
            return false;
        case 0x30: //l.mtspr
            std::cerr << "l.mtspr is not implemented yet" << std::endl;
            return false;
        case 0x35: //l.sw
            {
                static const char *const mn = "l.sw";
                Value *const EA = builder->CreateAdd(gen_get_reg(rA, mn), builder->CreateSExt(I, get_reg_type(), mn), mn);
                gen_sw(EA, gen_const(sizeof(target_ulong)), gen_get_reg(rB, mn), mn);
            }
            return false;
        case 0x36: //l.sb 
            {
                static const char *const mn = "l.sb";
                Value *const EA = builder->CreateAdd(gen_get_reg(rA, mn), builder->CreateSExt(I, get_reg_type(), mn), mn);
                gen_sw(EA, gen_const(1), gen_get_reg(rB, mn), mn);
            }
            return false;
 
        default:
            qcpu_or_disas_assert(!"Not implemented yet");
            break;
    }
}

void openrisc_vm::start_func(phys_addr_t pc_p){
    char func_name[17];
    std::snprintf(func_name, sizeof(func_name), "%16llx", static_cast<unsigned long long>(pc_p));
    std::vector<llvm::Type*> mainFuncTyArgs;
    llvm::FunctionType* const mainFuncTy = llvm::FunctionType::get(
            /* 戻り値の型 */ get_reg_type(),
            /* 関数の引数 */ mainFuncTyArgs,
            /* 可変引数か? */ false
            );
    llvm::Function *const func_main = llvm::Function::Create(
            /* 関数の型 */ mainFuncTy,
            /* Likage */ llvm::GlobalValue::ExternalLinkage,
            /* 関数名 */ func_name, mod
            );
    func_main->setCallingConv(llvm::CallingConv::C);
    llvm::BasicBlock* bb = llvm::BasicBlock::Create(mod->getContext(), "",func_main,0);
    builder->SetInsertPoint(bb);
    cur_func = func_main;
    cur_bb = bb;
    gen_set_reg(REG_PC, gen_get_reg(REG_PNEXT_PC, "prologue"), "prologue");
#if defined(QCPU_OPENRISC_DEBUG) && QCPU_OPENRISC_DEBUG > 1
    gen_set_reg(REG_PNEXT_PC, gen_const(0xFFFFFFFF), "prologue"); //poison value
#endif
}

llvm::Function * openrisc_vm::end_func(){
    llvm::Value *const pc = gen_get_reg(REG_PNEXT_PC, "epilogue");
    builder->CreateCall(mod->getFunction("qcpu_vm_dump_regs"));
    builder->CreateRet(pc);
    llvm::Function *const ret = cur_func;
    cur_func = QCPU_NULLPTR;
    cur_bb = QCPU_NULLPTR;

    return ret;
}

void openrisc_vm::run(){
    virt_addr_t pc(0x100);
    extern unsigned int total_icount;
    for(;;){
        const phys_addr_t pc_p(pc); //FIXME implement MMU
        const basic_block *const bb = bb_man.exists_by_start_addr(pc_p) ? bb_man.find_by_start_addr(pc_p) : disas(pc, -1);
        pc = bb->exec();
#if defined(QCPU_OPENRISC_DEBUG) && QCPU_OPENRISC_DEBUG > 1
        dump_regs();
#endif
        total_icount += bb->get_icount();
    }
}

void openrisc_vm::step_exec(){
    extern unsigned int total_icount;
    virt_addr_t pc(get_reg_func(REG_PC));
    const phys_addr_t pc_p(pc); //FIXME implement MMU
    bb_man.invalidate(pc_p, pc_p + phys_addr_t(4));
    const basic_block *const bb = bb_man.exists_by_start_addr(pc_p) ? bb_man.find_by_start_addr(pc_p) : disas(pc, 1);
    pc = bb->exec();
#if defined(QCPU_OPENRISC_DEBUG) && QCPU_OPENRISC_DEBUG > 1
    dump_regs();
#endif
    total_icount += bb->get_icount();
}

void openrisc_vm::dump_ir()const{
    llvm::PassManager pm;
    pm.add(createPrintModulePass(&llvm::outs()));
    pm.run(*mod);
}


void openrisc_vm::dump_regs()const{
    for(unsigned int i = 0; i < NUM_REGS; ++i){
        if(i < 32){
            std::cout << "reg[" << std::dec << std::setw(2) << std::setfill('0') << i << "]:";
        }
        else if(i == REG_PC){
            std::cout << "pc:";
        }
        else if(i == REG_SR){
            std::cout << "sr:";
        }
        else if(i == REG_PNEXT_PC){
            std::cout << "jump_to:";
        }
        else if(i == REG_CPUCFGR){
            std::cout << "cpucfgr";
        }
        else{assert(!"Unknown register");}
        std::cout << std::hex << std::setw(8) << std::setfill('0') << get_reg_func(i);
        if((i & 3) != 3) std::cout << "  ";
        else std::cout << '\n';
    }
    std::cout << std::endl;
}



openrisc::openrisc(const char *model) : qcpu(), vm(QCPU_NULLPTR){
}

openrisc::~openrisc(){
    delete vm;
}

void openrisc::interrupt(int irq_id, bool enable){
}

void openrisc::reset(bool reset_on){
}

void openrisc::run(run_option_e opt){
    if(!vm){
        vm= new openrisc_vm(*ext_ifs);
    }
    if(opt == RUN_OPTION_NORMAL){
        vm->run();
    }
    else if(opt == RUN_OPTION_WATI_GDB){
        ::qcpu::gdb::gdb_server gdb_srv(1234);
        gdb_srv.wait_and_run(*vm); 
    }
    else{
        qcpu_assert(!"Not supported option");
    }
}

} //end of namespace openrisc
} //end of namespace qcpu



namespace {
#include "or_ir_helpers.h"
} //end of unnamed namespace

