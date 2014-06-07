#include <cstdio>
#include <iostream>
#include <iomanip>

#include "jcpu_llvm_headers.h"
#include "jcpu_vm.h"
#include "gdbserver.h"
#include "jcpu_cortexm0.h"

static const int CORTEXM0_DEBUG_LEVEL = 0;

namespace {
#define jcpu_m0_disas_assert(cond) do{if(!(cond)){ \
    dump_ir(); \
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



} //end of unnamed namespace

namespace jcpu{
namespace cortexm0{

struct cortexm0_arch{
    typedef uint32_t target_ulong;
    const static unsigned int reg_bit_width = sizeof(target_ulong) * 8;
    typedef vm::primitive_type_holder<target_ulong, cortexm0_arch, 0> virt_addr_t;
    typedef vm::primitive_type_holder<target_ulong, cortexm0_arch, 1> phys_addr_t;

    enum reg_e{
        REG_R0, REG_R1, REG_R2, REG_R3,
        REG_R4, REG_R5, REG_R6, REG_R7,
        REG_R8, REG_R9, REG_R10, REG_R11,
        REG_R12,
        REG_R13, REG_MSP = REG_R13, REG_PSP = REG_R13,
        REG_R14, REG_LR = REG_R14,
        REG_R15, REG_PC = REG_R15,
        //REG_PRIMASK, REG_CONTROL,
        REG_PNEXT_PC,
        NUM_REGS
    };
    enum sr_flag_e{};
};

typedef vm::basic_block<cortexm0_arch> basic_block;
typedef vm::break_point<cortexm0_arch> break_point;

class cortexm0_vm : public vm::jcpu_vm_base<cortexm0_arch>{
    //gdb_target_if
    virtual void get_reg_value(std::vector<uint64_t> &)const JCPU_OVERRIDE;
    virtual void set_reg_value(unsigned int, uint64_t)JCPU_OVERRIDE;

    //bool irq_status;

    bool disas_insn(virt_addr_t, int *);
    virtual void start_func(phys_addr_t) JCPU_OVERRIDE;
    virtual run_state_e step_exec() JCPU_OVERRIDE;
    phys_addr_t code_v2p(virt_addr_t pc){return static_cast<phys_addr_t>(pc);}
    const basic_block *disas(virt_addr_t, int, const break_point *);
    public:
    explicit cortexm0_vm(jcpu_ext_if &);
    virtual run_state_e run() JCPU_OVERRIDE;
    virtual void dump_regs()const JCPU_OVERRIDE;
    void reset();
    void interrupt(int, bool);
};


void cortexm0_vm::get_reg_value(std::vector<uint64_t> &regs)const{
    regs.clear();
    for(unsigned int i = 0; i < 16; ++i){
        regs.push_back(get_reg_func(i));
    }
    regs.push_back(get_reg_func(cortexm0_arch::REG_PC));
}

void cortexm0_vm::set_reg_value(unsigned int reg_idx, uint64_t reg_val){
    jcpu_assert(reg_idx < 16 + 1);
    set_reg_func(reg_idx, reg_val);
}

bool cortexm0_vm::disas_insn(virt_addr_t, int *){
    jcpu_assert(!"Not implemented yet");
}

void cortexm0_vm::start_func(phys_addr_t pc_p){
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
    gen_set_reg(cortexm0_arch::REG_PC, gen_get_reg(cortexm0_arch::REG_PNEXT_PC, "prologue"));
    if(::CORTEXM0_DEBUG_LEVEL > 1){
        gen_set_reg(cortexm0_arch::REG_PNEXT_PC, gen_const(0xFFFFFFFF)); //poison value
    }
}


gdb::gdb_target_if::run_state_e cortexm0_vm::step_exec(){
    virt_addr_t pc(get_reg_func(cortexm0_arch::REG_PC));

    const break_point *const nearest = bp_man.find_nearest(pc);
    if(nearest && nearest->get_pc() == pc) return RUN_STAT_BREAK;
    const phys_addr_t pc_p = code_v2p(pc);
    bb_man.invalidate(pc_p, pc_p + phys_addr_t(2));
    const basic_block *const bb = bb_man.exists_by_start_addr(pc_p) ? bb_man.find_by_start_addr(pc_p) : disas(pc, 1, nearest);
    pc = bb->exec();
    if(CORTEXM0_DEBUG_LEVEL > 1){
        dump_regs();
    }
    total_icount += bb->get_icount();
    return RUN_STAT_NORMAL;
}


const basic_block *cortexm0_vm::disas(virt_addr_t start_pc_, int max_insn, const break_point *const bp){
    const phys_addr_t start_pc(start_pc_);
    start_func(start_pc);
    target_ulong pc;
    unsigned int num_insn = 0;
    if(max_insn < 0){
        bool done = false;
        for(pc = start_pc; !done; pc += 4){
            if(bp && bp->get_pc() == pc){
                gen_set_reg(cortexm0_arch::REG_PNEXT_PC, gen_const(pc));
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
            gen_set_reg(cortexm0_arch::REG_PC, gen_get_reg(cortexm0_arch::REG_PNEXT_PC));
        }
        else{
            gen_set_reg(cortexm0_arch::REG_PC, gen_const(pc));
            gen_set_reg(cortexm0_arch::REG_PNEXT_PC, gen_const(pc));
        }
    }
    llvm::Function *const f = end_func();
    const phys_addr_t end_pc(pc - 4);
    jcpu_assert(start_pc <= end_pc);
    basic_block *const bb = new basic_block(start_pc, end_pc, f, ee, num_insn);
    bb_man.add(bb);
    if(CORTEXM0_DEBUG_LEVEL > 2){
        dump_ir();
    }
    return bb;
}

cortexm0_vm::cortexm0_vm(jcpu_ext_if &ifs) : vm::jcpu_vm_base<cortexm0_arch>(ifs) 
{
    const unsigned int address_space = 5;
    const unsigned int bit = sizeof(target_ulong) * 8;
    const unsigned int num_regs = cortexm0_arch::NUM_REGS;
    llvm::ArrayType *const ATy = llvm::ArrayType::get(llvm::IntegerType::get(*context, bit), num_regs);
    std::vector<llvm::Constant*> Initializer;
    Initializer.reserve(num_regs);

    for(unsigned int i = 0; i < num_regs; ++i){
        const unsigned int reg_init_val = 0;//Reser vector for PC?
        llvm::ConstantInt *const ivc = gen_const(reg_init_val);
        Initializer.push_back(ivc);
    }

    llvm::Constant *const init = llvm::ConstantArray::get(ATy, Initializer);
    llvm::GlobalVariable *const global_regs = new llvm::GlobalVariable(*mod, ATy, true, llvm::GlobalValue::CommonLinkage, init, "regs", 0, llvm::GlobalVariable::NotThreadLocal, address_space);
    global_regs->setAlignment(bit / 8);

    vm::make_set_get(mod, global_regs, address_space);
    vm::make_mem_access(mod, address_space);
    vm::make_debug_func(mod, address_space);

    set_reg_func = reinterpret_cast<void (*)(uint16_t, target_ulong)>(ee->getPointerToFunction(mod->getFunction("set_reg")));
    get_reg_func = reinterpret_cast<target_ulong (*)(uint16_t)>(ee->getPointerToFunction(mod->getFunction("get_reg")));

    void (*const set_mem_access_if)(jcpu_ext_if *) = reinterpret_cast<void(*)(jcpu_ext_if*)>(ee->getPointerToFunction(mod->getFunction("set_mem_access_if")));
    set_mem_access_if(&ext_ifs);
    void (*const set_jcpu_vm_ptr)(jcpu_vm_if *) = reinterpret_cast<void(*)(jcpu_vm_if*)>(ee->getPointerToFunction(mod->getFunction("set_jcpu_vm_ptr")));
    set_jcpu_vm_ptr(this);

}

gdb::gdb_target_if::run_state_e cortexm0_vm::run(){
    virt_addr_t pc(get_reg_func(cortexm0_arch::REG_PC));
    for(;;){
        const break_point *const nearest = bp_man.find_nearest(pc);
        if(nearest && nearest->get_pc() == pc){
            return RUN_STAT_BREAK;
        }
        const phys_addr_t pc_p = code_v2p(pc);
        const basic_block *const bb = bb_man.exists_by_start_addr(pc_p) ? bb_man.find_by_start_addr(pc_p) : disas(pc, -1, nearest);
        pc = bb->exec();

        if(CORTEXM0_DEBUG_LEVEL > 1){
            dump_regs();
        }
        total_icount += bb->get_icount();
    }
    jcpu_assert(!"Never comes here");
    return RUN_STAT_NORMAL;
}

void cortexm0_vm::dump_regs()const{
    for(unsigned int i = 0; i < cortexm0_arch::NUM_REGS; ++i){
        if(i <= cortexm0_arch::REG_R12){
            std::cout << "reg[" << std::dec << std::setw(2) << std::setfill('0') << i << "]:";
        }
        else if(i == cortexm0_arch::REG_MSP){
            std::cout << "MSP:";
        }
        else if(i == cortexm0_arch::REG_LR){
            std::cout << "LR:";
        }
        else if(i == cortexm0_arch::REG_PC){
            std::cout << "PC:";
        }
        else if(i == cortexm0_arch::REG_PNEXT_PC){
            std::cout << "NEXT_PC:";
        }
        else{assert(!"Unknown register");}
        std::cout << std::hex << std::setw(8) << std::setfill('0') << get_reg_func(i);
        if((i & 3) != 3) std::cout << "  ";
        else std::cout << '\n';
    }
    std::cout << std::endl;
}

void cortexm0_vm::reset(){
    //irq_status = false;
}

void cortexm0_vm::interrupt(int irq_id, bool enable){
    jcpu_assert(!"Not implemented yet");
    //jcpu_assert(irq_id == 0);
    //irq_status = enable;
}

// ******************** jcpu::cortexm0::cortexm0 ******************** //

cortexm0::cortexm0(const char *model) : jcpu(), vm(JCPU_NULLPTR){
}

cortexm0::~cortexm0(){
    delete vm;
}

void cortexm0::interrupt(int irq_id, bool enable){
    if(vm) vm->interrupt(irq_id, enable);
}

void cortexm0::reset(bool reset_on){
    if(vm) vm->reset();
}

void cortexm0::run(run_option_e opt){
    if(!vm){
        vm = new cortexm0_vm(*ext_ifs);
        vm->reset();
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

uint64_t cortexm0::get_total_insn_count()const{
    return vm->get_total_insn_count();
}




} //end of namespace arm
} //end of namespace jcpu

