#include <cstdio>
#include <iostream>
#include <iomanip>

#include <llvm/LLVMContext.h>
#include <llvm/ExecutionEngine/JIT.h> 
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/PrettyStackTrace.h>
#include <llvm/Support/Signals.h>
#include <llvm/Support/Debug.h> //EnableDebugBuffering
#include <llvm/Analysis/Verifier.h> //verifyModule
#include <llvm/Instructions.h> //LoadInst

#include "jcpu_vm.h"
#include "gdbserver.h"
#include "jcpu_arm.h"

#define JCPU_ARM_DEBUG 3


namespace {
#define jcpu_arm_disas_assert(cond) do{if(!(cond)){ \
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



void make_set_get(llvm::Module *, llvm::GlobalVariable *, unsigned int);
void make_mem_access(llvm::Module *, unsigned int);
void make_debug_func(llvm::Module *, unsigned int);


} //end of unnamed namespace

namespace jcpu{
namespace arm{

struct arm_arch{
    typedef uint32_t target_ulong;
    const static unsigned int reg_bit_width = 32;
    typedef vm::primitive_type_holder<target_ulong, arm_arch, 0> virt_addr_t;
    typedef vm::primitive_type_holder<target_ulong, arm_arch, 1> phys_addr_t;
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

};

typedef arm_arch::virt_addr_t virt_addr_t;
typedef arm_arch::phys_addr_t phys_addr_t;
typedef arm_arch::target_ulong target_ulong;

typedef vm::basic_block<arm_arch> basic_block;
typedef vm::bb_manager<arm_arch> bb_manager;
typedef vm::break_point<arm_arch> break_point;
typedef vm::bp_manager<arm_arch> bp_manager;

class arm_vm : public vm::jcpu_vm_base<arm_arch>{
    //gdb_target_if
    virtual void get_reg_value(std::vector<uint64_t> &)const JCPU_OVERRIDE;
    virtual void set_reg_value(unsigned int, uint64_t)JCPU_OVERRIDE;

    virtual bool disas_insn(virt_addr_t, int *)JCPU_OVERRIDE;
    void start_func(phys_addr_t);
    llvm::Function * end_func();
    const basic_block *disas(virt_addr_t, int, const break_point *);
    virtual run_state_e step_exec() JCPU_OVERRIDE;
    phys_addr_t code_v2p(virt_addr_t pc){return static_cast<phys_addr_t>(pc);} //FIXME implement MMU
    public:
    explicit arm_vm(jcpu_ext_if &);
    virtual run_state_e run() JCPU_OVERRIDE;
    virtual void dump_regs()const JCPU_OVERRIDE;
};

arm_vm::arm_vm(jcpu_ext_if &ifs) : vm::jcpu_vm_base<arm_arch>(ifs) 
{
    const unsigned int address_space = 3;
    const unsigned int bit = sizeof(target_ulong) * 8;
    const unsigned int num_regs = arm_arch::NUM_REGS;
    llvm::ArrayType *const ATy = llvm::ArrayType::get(llvm::IntegerType::get(*context, bit), num_regs);
    std::vector<llvm::Constant*> Initializer;
    Initializer.reserve(num_regs);

    for(unsigned int i = 0; i < num_regs; ++i){
        const unsigned int reg_init_val = (i == arm_arch::REG_PC || i == arm_arch::REG_PNEXT_PC) ? 0x0000 : 0;
        llvm::ConstantInt *const ivc = gen_const(reg_init_val);
        Initializer.push_back(ivc);
    }

    llvm::Constant *const init = llvm::ConstantArray::get(ATy, Initializer);
    llvm::GlobalVariable *const global_regs = new llvm::GlobalVariable(*mod, ATy, true, llvm::GlobalValue::CommonLinkage, init, "regs", 0, llvm::GlobalVariable::NotThreadLocal, address_space);
    global_regs->setAlignment(bit / 8);

    make_set_get(mod, global_regs, address_space);
    make_mem_access(mod, address_space);
    make_debug_func(mod, address_space);

    set_reg_func = reinterpret_cast<void (*)(uint16_t, target_ulong)>(ee->getPointerToFunction(mod->getFunction("set_reg")));
    get_reg_func = reinterpret_cast<target_ulong (*)(uint16_t)>(ee->getPointerToFunction(mod->getFunction("get_reg")));

    void (*const set_mem_access_if)(jcpu_ext_if *) = reinterpret_cast<void(*)(jcpu_ext_if*)>(ee->getPointerToFunction(mod->getFunction("set_mem_access_if")));
    set_mem_access_if(&ext_ifs);
    void (*const set_jcpu_vm_ptr)(jcpu_vm_if *) = reinterpret_cast<void(*)(jcpu_vm_if*)>(ee->getPointerToFunction(mod->getFunction("set_jcpu_vm_ptr")));
    set_jcpu_vm_ptr(this);


}


void arm_vm::get_reg_value(std::vector<uint64_t> &regs)const{
    regs.clear();
    for(unsigned int i = 0; i < 32; ++i){
        regs.push_back(get_reg_func(i));
    }
    //regs.push_back(get_reg_func(arm_arch::REG_PC));
    regs.push_back(get_reg_func(arm_arch::REG_PNEXT_PC));
}

void arm_vm::set_reg_value(unsigned int reg_idx, uint64_t reg_val){
    jcpu_assert(reg_idx < 32 + 1);
    set_reg_func(reg_idx, reg_val);
}

bool arm_vm::disas_insn(virt_addr_t pc_v, int *const insn_depth){
    ++(*insn_depth);
    const phys_addr_t pc = code_v2p(pc_v);
    struct push_and_pop_pc{
        arm_vm &vm;
        push_and_pop_pc(arm_vm &vm, virt_addr_t pc_v, phys_addr_t pc_p) : vm(vm){
            vm.processing_pc.push(std::make_pair(pc_v, pc_p));
        }
        ~push_and_pop_pc(){

            vm.gen_set_reg(arm_arch::REG_PC, vm.gen_const(vm.processing_pc.top().first + 4));
#if defined(JCPU_ARM_DEBUG) && JCPU_ARM_DEBUG > 1
            //vm.gen_set_reg(arm_arch::REG_PC, vm.gen_const(vm.processing_pc.top().second));
#endif
#if defined(JCPU_ARM_DEBUG) && JCPU_ARM_DEBUG > 2
            vm.builder->CreateCall(vm.mod->getFunction("jcpu_vm_dump_regs"));
#endif
            vm.processing_pc.pop();
        }
    } push_and_pop_pc(*this, pc_v, pc);
    const target_ulong insn = ext_ifs.mem_read(pc, sizeof(target_ulong));
    const unsigned int kind = bit_sub<26, 6>(insn);
#if defined(JCPU_ARM_DEBUG) && JCPU_ARM_DEBUG > 0
    std::cout << std::hex << "pc:" << pc << " INSN:" << std::setw(8) << std::setfill('0') << insn << " kind:" << kind << std::endl;
#endif
#if defined(JCPU_ARM_DEBUG) && JCPU_ARM_DEBUG > 2
    builder->CreateCall(mod->getFunction("jcpu_vm_dump_regs"));
#endif
    switch(kind){
        default:
        jcpu_assert(!"Not implemented yet");
    }
    jcpu_assert(!"Never comes here");
    return false;
}

const basic_block *arm_vm::disas(virt_addr_t start_pc_, int max_insn, const break_point *const bp){
    const phys_addr_t start_pc(start_pc_);
    start_func(start_pc);
    target_ulong pc;
    unsigned int num_insn = 0;
    if(max_insn < 0){
        bool done = false;
        for(pc = start_pc; !done; pc += 4){
            if(bp && bp->get_pc() == pc){
                gen_set_reg(arm_arch::REG_PNEXT_PC, gen_const(pc));
                break;
            }
            int insn_depth = 0;
            done = disas_insn(virt_addr_t(pc), &insn_depth);
            num_insn += insn_depth;
        }
    }
    else{
        jcpu_assert(max_insn ==  1); //only step exec is supported
        int insn_depth = 0;
        const bool done = disas_insn(start_pc_, &insn_depth);
        num_insn += insn_depth;
        pc = start_pc + (done ? 8 : 4);
        if(done){
            gen_set_reg(arm_arch::REG_PC, gen_get_reg(arm_arch::REG_PNEXT_PC));
        }
        else{
            gen_set_reg(arm_arch::REG_PNEXT_PC, gen_const(pc));
        }
    }
    llvm::Function *const f = end_func();
    basic_block *const bb = new basic_block(start_pc, phys_addr_t(pc - 4), f, ee, num_insn);
    bb_man.add(bb);
#if defined(JCPU_ARM_DEBUG) && JCPU_ARM_DEBUG > 1
    dump_ir();
#endif
    return bb;
}


void arm_vm::start_func(phys_addr_t pc_p){
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
    gen_set_reg(arm_arch::REG_PC, gen_get_reg(arm_arch::REG_PNEXT_PC, "prologue"), "prologue");
#if defined(JCPU_ARM_DEBUG) && JCPU_ARM_DEBUG > 1
    gen_set_reg(arm_arch::REG_PNEXT_PC, gen_const(0xFFFFFFFF), "prologue"); //poison value
#endif
}

llvm::Function * arm_vm::end_func(){
    llvm::Value *const pc = gen_get_reg(arm_arch::REG_PNEXT_PC, "epilogue");
#if defined(JCPU_ARM_DEBUG) && JCPU_ARM_DEBUG > 1
    builder->CreateCall(mod->getFunction("jcpu_vm_dump_regs"));
#endif
    builder->CreateRet(pc);
    llvm::Function *const ret = cur_func;
    cur_func = JCPU_NULLPTR;
    cur_bb = JCPU_NULLPTR;

    return ret;
}

gdb::gdb_target_if::run_state_e arm_vm::run(){
    virt_addr_t pc(get_reg_func(arm_arch::REG_PC));
    for(;;){
        const break_point *const nearest = bp_man.find_nearest(pc);
        const phys_addr_t pc_p = code_v2p(pc);
        const basic_block *const bb = bb_man.exists_by_start_addr(pc_p) ? bb_man.find_by_start_addr(pc_p) : disas(pc, -1, nearest);
        if(nearest && nearest->get_pc() == pc){
            return RUN_STAT_BREAK;
        }
        pc = bb->exec();
#if defined(JCPU_ARM_DEBUG) && JCPU_ARM_DEBUG > 1
        dump_regs();
#endif
        total_icount += bb->get_icount();
    }
    jcpu_assert(!"Never comes here");
    return RUN_STAT_NORMAL;
}

gdb::gdb_target_if::run_state_e arm_vm::step_exec(){
    virt_addr_t pc(get_reg_func(arm_arch::REG_PC));
    const break_point *const nearest = bp_man.find_nearest(pc);
    const phys_addr_t pc_p = code_v2p(pc);
    bb_man.invalidate(pc_p, pc_p + phys_addr_t(4));
    const basic_block *const bb = bb_man.exists_by_start_addr(pc_p) ? bb_man.find_by_start_addr(pc_p) : disas(pc, 1, nearest);
    pc = bb->exec();
#if defined(JCPU_ARM_DEBUG) && JCPU_ARM_DEBUG > 1
    dump_regs();
#endif
    total_icount += bb->get_icount();
    return (nearest && nearest->get_pc() == pc) ? RUN_STAT_BREAK : RUN_STAT_NORMAL;
}

void arm_vm::dump_regs()const{
    for(unsigned int i = 0; i < arm_arch::NUM_REGS; ++i){
        if(i < 32){
            std::cout << "reg[" << std::dec << std::setw(2) << std::setfill('0') << i << "]:";
        }
        else if(i == arm_arch::REG_PC){
            std::cout << "pc:";
        }
        else if(i == arm_arch::REG_SR){
            std::cout << "sr:";
        }
        else if(i == arm_arch::REG_PNEXT_PC){
            std::cout << "jump_to:";
        }
        else if(i == arm_arch::REG_CPUCFGR){
            std::cout << "cpucfgr";
        }
        else{assert(!"Unknown register");}
        std::cout << std::hex << std::setw(8) << std::setfill('0') << get_reg_func(i);
        if((i & 3) != 3) std::cout << "  ";
        else std::cout << '\n';
    }
    std::cout << std::endl;
}

arm::arm(const char *model) : jcpu(), vm(JCPU_NULLPTR){
}

arm::~arm(){
    delete vm;
}

void arm::interrupt(int irq_id, bool enable){
}

void arm::reset(bool reset_on){
}

void arm::run(run_option_e opt){
    if(!vm){
        vm= new arm_vm(*ext_ifs);
    }
    if(opt == RUN_OPTION_NORMAL){
        vm->run();
    }
    else if(opt == RUN_OPTION_WATI_GDB){
        ::jcpu::gdb::gdb_server gdb_srv(1234);
        gdb_srv.wait_and_run(*vm); 
    }
    else{
        jcpu_assert(!"Not supported option");
    }
}

uint64_t arm::get_total_insn_count()const{
    return vm->get_total_insn_count();
}


} //end of namespace arm
} //end of namespace jcpu



namespace {
#include "arm_ir_helpers.h"
} //end of unnamed namespace

