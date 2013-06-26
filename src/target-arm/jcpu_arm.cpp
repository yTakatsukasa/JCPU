#include <cstdio>
#include <iostream>
#include <iomanip>

#include "jcpu_llvm_headers.h"
#include "jcpu_vm.h"
#include "gdbserver.h"
#include "jcpu_arm.h"

#define JCPU_ARM_DEBUG 2


namespace {
#define jcpu_arm_disas_assert(cond) do{if(!(cond)){ \
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
        REG_GR12, REG_GR13, REG_GR14, REG_LINK = REG_GR14, REG_GR15, REG_PC = REG_GR15,
        REG_SR, REG_CPUCFGR, REG_PNEXT_PC, NUM_REGS
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

    llvm::Value *gen_get_reg_pc_check(reg_e r, const char *nm = "")const{
        if(r == arm_arch::REG_PC){
            return gen_get_pc();
        }
        else{
            return gen_get_reg(r, nm);
        }
    }
    bool gen_set_reg_pc_check(reg_e r, llvm::Value *val, const char *nm)const{
        if(r == arm_arch::REG_PC){
            gen_set_reg(arm_arch::REG_PNEXT_PC, val, nm);
            return true;
        }
        else{
            gen_set_reg(r, val, nm);
            return false;
        }
    }

    bool disas_insn(virt_addr_t, int *);
    bool disas_data_proc(target_ulong, int *);
    bool disas_data_imm(target_ulong, int *);
    bool disas_imm_ldst(target_ulong, int *);
    bool disas_offset_ldst(target_ulong, int *);
    bool disas_multi_ldst(target_ulong, int *);
    bool disas_jump(target_ulong, int *);
    bool disas_copro_ldst(target_ulong, int *);
    bool disas_swi(target_ulong, int *);
    bool disas_copro(target_ulong, int *);
    virtual void start_func(phys_addr_t) JCPU_OVERRIDE;
    const basic_block *disas(virt_addr_t, int, const break_point *);
    virtual run_state_e step_exec() JCPU_OVERRIDE;
    phys_addr_t code_v2p(virt_addr_t pc){return static_cast<phys_addr_t>(pc);} //FIXME implement MMU
    bool gen_set_reg_by_cond(arm_arch::reg_e, unsigned int, llvm::Value *, const char * = "");
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


void arm_vm::get_reg_value(std::vector<uint64_t> &regs)const{
    regs.clear();
    for(unsigned int i = 0; i < arm_arch::NUM_REGS; ++i){
        regs.push_back(get_reg_func(i));
    }
    //regs.push_back(get_reg_func(arm_arch::REG_PC));
    //regs.push_back(get_reg_func(arm_arch::REG_PNEXT_PC));
}

void arm_vm::set_reg_value(unsigned int reg_idx, uint64_t reg_val){
    jcpu_assert(reg_idx < arm_arch::NUM_REGS);
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
#if defined(JCPU_ARM_DEBUG) && JCPU_ARM_DEBUG > 2
            vm.builder->CreateCall(vm.mod->getFunction("jcpu_vm_dump_regs"));
#endif
            vm.processing_pc.pop();
vm.dump_ir();
        }
    } push_and_pop_pc(*this, pc_v, pc);
    const target_ulong insn = ext_ifs.mem_read(pc, sizeof(target_ulong));
    const unsigned int kind = bit_sub<25, 3>(insn);
#if defined(JCPU_ARM_DEBUG) && JCPU_ARM_DEBUG > 0
    std::cout << std::hex << "pc:" << pc << " INSN:" << std::setw(8) << std::setfill('0') << insn << std::endl;
#endif
#if defined(JCPU_ARM_DEBUG) && JCPU_ARM_DEBUG > 2
    builder->CreateCall(mod->getFunction("jcpu_vm_dump_regs"));
#endif
    switch(kind){
        case 0x0: //data_proc
            return disas_data_proc(insn, insn_depth);
        case 0x01:
            return disas_data_imm(insn, insn_depth);
        case 0x02:
            return disas_imm_ldst(insn, insn_depth);
        case 0x03:
            return disas_offset_ldst(insn, insn_depth);
        case 0x04:
            return disas_multi_ldst(insn, insn_depth);
        case 0x05:
            return disas_jump(insn, insn_depth);
        case 0x06:
            return disas_copro_ldst(insn, insn_depth);
        case 0x07:
            if(bit_sub<24, 1>(insn))
                return disas_swi(insn, insn_depth);
            else
                return disas_copro(insn, insn_depth);
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
        gen_set_reg(arm_arch::REG_PC, gen_const(pc));
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
        if(i < 14){
            std::cout << "reg[" << std::dec << std::setw(2) << std::setfill('0') << i << "]:";
        }
        else if(i == arm_arch::REG_LINK){
            std::cout << "lr:";
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



bool arm_vm::disas_data_proc(target_ulong insn, int *const insn_dpeth){
    //const unsigned int cond = bit_sub<28, 4>(insn);
    jcpu_assert(!"Not implemented yet");
    jcpu_assert(!"Never comes here");
    return false; //suppress warnings
}

bool arm_vm::disas_data_imm(target_ulong insn, int *const insn_dpeth){
    using namespace llvm;
    const unsigned int cond = bit_sub<28, 4>(insn);
    const unsigned int op = bit_sub<21, 4>(insn);
    const bool S = bit_sub<20, 1>(insn);
    const arm_arch::reg_e Rn_raw = static_cast<arm_arch::reg_e>(bit_sub<16, 4>(insn));
    const arm_arch::reg_e Rd_raw = static_cast<arm_arch::reg_e>(bit_sub<12, 4>(insn));
    llvm::Value *const shifter = ConstantInt::get(*context, APInt(12, bit_sub<0, 12>(insn)));
    jcpu_assert(!S);
    switch(op){
        case 0x4://add 
            return gen_set_reg_by_cond(Rd_raw, cond, builder->CreateAdd(gen_get_reg_pc_check(Rn_raw), shifter, "add"), "add");
        default:
            jcpu_assert(!"Not implemented yet");
    }

    jcpu_assert(!"Not implemented yet");
    jcpu_assert(!"Never comes here");
    return false; //suppress warnings
}

bool arm_vm::disas_imm_ldst(target_ulong insn, int *const insn_dpeth){
    using namespace llvm;
    const unsigned int cond = bit_sub<28, 4>(insn);
    const bool P = bit_sub<24, 1>(insn);
    const bool U = bit_sub<23, 1>(insn);
    const bool B = bit_sub<22, 1>(insn);
    const bool W = bit_sub<21, 1>(insn);
    const bool L = bit_sub<20, 1>(insn);
    const arm_arch::reg_e Rn_raw = static_cast<arm_arch::reg_e>(bit_sub<16, 4>(insn));
    const arm_arch::reg_e Rd_raw = static_cast<arm_arch::reg_e>(bit_sub<12, 4>(insn));
    llvm::Value *const Rn_val = gen_get_reg_pc_check(Rn_raw, "Rn");
    llvm::Value *const offset12 = ConstantInt::get(*context, APInt(12, bit_sub<0, 12>(insn)));
    llvm::Value *const calc_addr = U ? builder->CreateAdd(Rn_val, offset12) : builder->CreateSub(Rn_val, offset12);
    llvm::Value *const dst_addr = P ? calc_addr : Rn_val;
    jcpu_assert(!B);
    bool is_jump = false;
    if(L){//load
        Value *const dat = gen_lw(dst_addr, gen_const(sizeof(target_ulong)), "ldr.val");
        is_jump = gen_set_reg_by_cond(Rd_raw, cond, dat);
    }
    else{//store
        gen_sw(dst_addr, gen_const(sizeof(target_ulong)), gen_get_reg_pc_check(Rd_raw, "str.val"), "str");
    }
    if(!(P ==1 && W == 0)){
        is_jump |= gen_set_reg_by_cond(Rn_raw, cond, calc_addr);
    }
    return is_jump;
    jcpu_assert(!"Not implemented yet");
    jcpu_assert(!"Never comes here");
    return false; //suppress warnings
}

bool arm_vm::disas_offset_ldst(target_ulong insn, int *const insn_dpeth){
    //const unsigned int cond = bit_sub<28, 4>(insn);
    jcpu_assert(!"Not implemented yet");
    jcpu_assert(!"Never comes here");
    return false; //suppress warnings
}

bool arm_vm::disas_multi_ldst(target_ulong insn, int *const insn_dpeth){
    //const unsigned int cond = bit_sub<28, 4>(insn);
    jcpu_assert(!"Not implemented yet");
    jcpu_assert(!"Never comes here");
    return false; //suppress warnings
}

bool arm_vm::disas_jump(target_ulong insn, int *const insn_dpeth){
    const unsigned int cond = bit_sub<28, 4>(insn);
    const bool link = bit_sub<24,1>(insn);
    const target_ulong offset_uint = bit_sub<0, 24>(insn);
    llvm::Value *const offset_cint = llvm::ConstantInt::get(*context, llvm::APInt(24, offset_uint));
    llvm::Value *const offset = builder->CreateShl(builder->CreateSExt(offset_cint, get_reg_type()), 2, "offset");
    llvm::Value *const new_pc = builder->CreateAdd(builder->CreateAdd(gen_get_pc(), gen_const(8)), offset, "b.new_pc");
    gen_set_reg_by_cond(arm_arch::REG_PNEXT_PC, cond, new_pc);
    if(link){
        llvm::Value *const next_pc = builder->CreateAdd(gen_get_pc(), gen_const(4), "bl.next_pc");
        gen_set_reg_by_cond(arm_arch::REG_LINK, cond, next_pc);
    }
    return true;
    jcpu_arm_disas_assert(!"Not implemented yet");
    jcpu_assert(!"Never comes here");
    return false; //suppress warnings
}

bool arm_vm::disas_copro_ldst(target_ulong insn, int *const insn_dpeth){
    //const unsigned int cond = bit_sub<28, 4>(insn);
    jcpu_arm_disas_assert(!"Not implemented yet");
    jcpu_assert(!"Never comes here");
    return false; //suppress warnings
}

bool arm_vm::disas_swi(target_ulong insn, int *const insn_dpeth){
    //const unsigned int cond = bit_sub<28, 4>(insn);
    jcpu_arm_disas_assert(!"Not implemented yet");
    jcpu_assert(!"Never comes here");
    return false; //suppress warnings
}

bool arm_vm::disas_copro(target_ulong insn, int *const insn_dpeth){
    //const unsigned int cond = bit_sub<28, 4>(insn);
    jcpu_arm_disas_assert(!"Not implemented yet");
    jcpu_assert(!"Never comes here");
    return false; //suppress warnings
}


bool arm_vm::gen_set_reg_by_cond(arm_arch::reg_e reg, unsigned int cond, llvm::Value* val, const char *nm){
    jcpu_arm_disas_assert(cond == 0xE);
    return gen_set_reg_pc_check(reg, val, nm);
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

