#include <cstdio>
#include <iostream>
#include <iomanip>

#include "jcpu_llvm_headers.h"
#include "jcpu_vm.h"
#include "gdbserver.h"
#include "jcpu_riscv.h"

//#define JCPU_RISCV_DEBUG 3


namespace {

template<unsigned int bit, unsigned int width, typename T>
inline T bit_sub(T v){
    return (v >> bit) & ((T(1) << width) - 1);
}



} //end of unnamed namespace

namespace jcpu{
namespace riscv{

struct riscv_arch{
    typedef uint32_t target_ulong;
    const static unsigned int reg_bit_width = sizeof(target_ulong) * 8;
    typedef vm::primitive_type_holder<target_ulong, riscv_arch, 0> virt_addr_t;
    typedef vm::primitive_type_holder<target_ulong, riscv_arch, 1> phys_addr_t;
    enum reg_e{
        REG_GR00, REG_ZERO = REG_GR00, REG_GR01, REG_GR02, REG_SP = REG_GR02, REG_GR03,
        REG_GR04, REG_GR05, REG_GR06, REG_GR07,
        REG_GR08, REG_GR09, REG_LR = REG_GR09, REG_GR10, REG_GR11,
        REG_GR12, REG_GR13, REG_GR14, REG_GR15,
        REG_GR16, REG_GR17, REG_GR18, REG_GR19,
        REG_GR20, REG_GR21, REG_GR22, REG_GR23,
        REG_GR24, REG_GR25, REG_GR26, REG_GR27,
        REG_GR28, REG_GR29, REG_GR30, REG_GR31,
        REG_PC, REG_PNEXT_PC, NUM_REGS
    };
    enum sr_flag_e{};
};

typedef riscv_arch::virt_addr_t virt_addr_t;
typedef riscv_arch::phys_addr_t phys_addr_t;
typedef riscv_arch::target_ulong target_ulong;

typedef vm::basic_block<riscv_arch> basic_block;
typedef vm::bb_manager<riscv_arch> bb_manager;
typedef vm::break_point<riscv_arch> break_point;
typedef vm::bp_manager<riscv_arch> bp_manager;

class riscv_vm : public vm::jcpu_vm_base<riscv_arch>{
    //gdb_target_if
    virtual void get_reg_value(std::vector<uint64_t> &)const JCPU_OVERRIDE;
    virtual void set_reg_value(unsigned int, uint64_t)JCPU_OVERRIDE;

    bool irq_status;

    llvm::Value *gen_get_reg(riscv_arch::reg_e, const char * = "")const ;
    void gen_set_reg(riscv_arch::reg_e, llvm::Value *)const ;
    bool disas_insn(virt_addr_t, int *);
    bool disas_insn_load_imm(target_ulong insn);
    bool disas_insn_integer_imm(target_ulong insn);
    bool disas_insn_integer_reg(target_ulong insn);
    bool disas_insn_load(target_ulong insn);
    bool disas_insn_store(target_ulong insn);
    bool disas_insn_cond_branch(target_ulong insn);
    bool disas_insn_jump(target_ulong insn);

    llvm::Value *gen_arith_code_with_ovf_check(llvm::Value *, llvm::Value*, llvm::Value * (vm::ir_builder_wrapper::*)(llvm::Value *, llvm::Value *, const char *)const, const char *);
    virtual void start_func(phys_addr_t) JCPU_OVERRIDE;
    const basic_block *disas(virt_addr_t, int, const break_point *);
    virtual run_state_e step_exec() JCPU_OVERRIDE;
    phys_addr_t code_v2p(virt_addr_t pc){return static_cast<phys_addr_t>(pc);} //FIXME implement MMU
    public:
    explicit riscv_vm(jcpu_ext_if &);
    virtual run_state_e run() JCPU_OVERRIDE;
    virtual void dump_regs()const JCPU_OVERRIDE;
    void reset();
    void interrupt(int, bool);
};

riscv_vm::riscv_vm(jcpu_ext_if &ifs) : vm::jcpu_vm_base<riscv_arch>(ifs) 
{
    const unsigned int address_space = 5;
    const unsigned int bit = sizeof(target_ulong) * 8;
    const unsigned int num_regs = riscv_arch::NUM_REGS;
    std::vector<llvm::Constant*> Initializer;
    Initializer.reserve(num_regs);

    for(unsigned int i = 0; i < num_regs; ++i){
        //FIXME:default value of PC and SP  is hardcoded, need to check spec
        const unsigned int reg_init_val = (i == riscv_arch::REG_PC || i == riscv_arch::REG_PNEXT_PC) ? 0x10000 : 0;
        llvm::ConstantInt *const ivc = gen_const(reg_init_val);
        Initializer.push_back(ivc);
    }

    llvm::ArrayType *const ATy = llvm::ArrayType::get(llvm::IntegerType::get(*context, bit), num_regs);
    llvm::Constant *const init = llvm::ConstantArray::get(ATy, Initializer);
#if JCPU_LLVM_VERSION_LT(3,6)
    llvm::GlobalVariable *const global_regs = new llvm::GlobalVariable(*mod, ATy, true, llvm::GlobalValue::CommonLinkage, init, "regs", 0, llvm::GlobalVariable::NotThreadLocal, address_space);
#else
    llvm::GlobalVariable *const global_regs = new llvm::GlobalVariable(*mod, ATy, false, llvm::GlobalValue::InternalLinkage, init, "regs", 0, llvm::GlobalVariable::NotThreadLocal, address_space);
#endif
    global_regs->setAlignment(bit / 8);

    vm::make_set_get(mod, global_regs, address_space);
    vm::make_mem_access(mod, address_space);
    vm::make_debug_func(mod, address_space);

    set_reg_func = get_func_ptr<void (*)(uint16_t, target_ulong)>("set_reg");
    get_reg_func = get_func_ptr<target_ulong (*)(uint16_t)>("get_reg");

    void (*const set_mem_access_if)(jcpu_ext_if *) = get_func_ptr<void(*)(jcpu_ext_if*)>("set_mem_access_if");
    set_mem_access_if(&ext_ifs);
    void (*const set_jcpu_vm_ptr)(jcpu_vm_if *) = get_func_ptr<void(*)(jcpu_vm_if*)>("set_jcpu_vm_ptr");
    set_jcpu_vm_ptr(this);
}


void riscv_vm::get_reg_value(std::vector<uint64_t> &regs)const{
    regs.clear();
    for(unsigned int i = 0; i < 32; ++i){
        regs.push_back(get_reg_func(i));
    }
    regs.push_back(get_reg_func(riscv_arch::REG_PC));
    //regs.push_back(get_reg_func(riscv_arch::REG_PNEXT_PC));
}

void riscv_vm::set_reg_value(unsigned int reg_idx, uint64_t reg_val){
    jcpu_assert(reg_idx < 32 + 1);
    set_reg_func(reg_idx, reg_val);
}

llvm::Value *riscv_vm::gen_get_reg(riscv_arch::reg_e reg, const char *nm)const{
    return reg == riscv_arch::REG_GR00 ? gen_const(0) : vm::jcpu_vm_base<riscv_arch>::gen_get_reg(reg, nm);
}

void riscv_vm::gen_set_reg(riscv_arch::reg_e reg, llvm::Value *val)const{
    if(reg != riscv_arch::REG_GR00){
        vm::jcpu_vm_base<riscv_arch>::gen_set_reg(reg, val);
    }
}

bool riscv_vm::disas_insn(virt_addr_t pc_v, int *const insn_depth){
    ++(*insn_depth);
    const phys_addr_t pc = code_v2p(pc_v);
    struct push_and_pop_pc{
        riscv_vm &vm;
        push_and_pop_pc(riscv_vm &vm, virt_addr_t pc_v, phys_addr_t pc_p) : vm(vm){
            vm.processing_pc.push(std::make_pair(pc_v, pc_p));
        }
        ~push_and_pop_pc(){
            vm.processing_pc.pop();
        }
    } push_and_pop_pc(*this, pc_v, pc);
    const target_ulong insn = ext_ifs.mem_read(pc, sizeof(target_ulong));
    const unsigned int kind = bit_sub<2, 5>(insn);
#if defined(JCPU_RISCV_DEBUG) && JCPU_RISCV_DEBUG > 0
    std::cout << std::hex << "pc:" << pc << " INSN:" << std::setw(8) << std::setfill('0') << insn << " kind:" << kind << std::endl;
#endif
#if defined(JCPU_RISCV_DEBUG) && JCPU_RISCV_DEBUG > 2
    builder->CreateCall(mod->getFunction("jcpu_vm_dump_regs"));
#endif

    switch(kind) {
        case 0x0D://LUI
        case 0x05://AUIPC
            return disas_insn_load_imm(insn);
        case 0x04:
            return disas_insn_integer_imm(insn);
        case 0x0C:
            return disas_insn_integer_reg(insn);
        case 0x00:
            return disas_insn_load(insn);
        case 0x08:
            return disas_insn_store(insn);
        case 0x18:
            return disas_insn_cond_branch(insn);
        case 0x19://jalr
        case 0x1b://jal
            return disas_insn_jump(insn);
        default:
            builder->CreateCall(mod->getFunction("jcpu_vm_dump_regs"));
            dump_regs();
            jcpu_assert(!"Not supported insn");
    }

#if defined(JCPU_RISCV_DEBUG) && JCPU_RISCV_DEBUG > 2
    builder->CreateCall(mod->getFunction("jcpu_vm_dump_regs"));
#endif
    jcpu_assert(!"Never comes here");
    return false;//suppress warning
}

bool riscv_vm::disas_insn_load_imm(target_ulong insn)
{
    const target_ulong kind = bit_sub<2, 5>(insn);
    const riscv_arch::reg_e dest = static_cast<riscv_arch::reg_e>(bit_sub<7, 5>(insn));
    llvm::Value *const imm = gen_const(bit_sub<12, 20>(insn));
    switch(kind)
    {
        case 0x0D://LUI
            {
                static const char *mn = "lui";
                gen_set_reg(dest, builder->CreateShl(imm, gen_const(12), mn));
            }
            break;
        case 0x05://AUIPC
            {
                static const char *mn = "auipc";
                llvm::Value *const shifted = builder->CreateShl(imm, gen_const(12), mn);
                llvm::Value *const added = builder->CreateAdd(shifted, gen_get_pc(), mn);
                gen_set_reg(dest, added);
            }
            break;
    }
    return false;
}

bool riscv_vm::disas_insn_integer_imm(target_ulong insn) {
    llvm::ConstantInt *const i12 = llvm::ConstantInt::get(*context, llvm::APInt(12, bit_sub<20, 12>(insn)));
    llvm::Value *const imm = gen_const(bit_sub<20, 12>(insn));
    const riscv_arch::reg_e dest = static_cast<riscv_arch::reg_e>(bit_sub<7, 5>(insn));
    llvm::Value *const src = gen_get_reg(static_cast<riscv_arch::reg_e>(bit_sub<15, 5>(insn)));
    switch(bit_sub<12, 3>(insn))
    {
        case 0://ADDI
            {//add immediate value after sign extension
                static const char *const mn = "addi";
                llvm::Value *const extended = builder->CreateSExt(i12, get_reg_type());
                llvm::Value *const added = builder->CreateAdd(extended, src, mn);
                gen_set_reg(dest, added);
            }
            break;
        case 1://SLLI
            {//shift left logical
                const target_ulong zero = bit_sub<25, 7>(insn);
                jcpu_assert(zero == 0);
                static const char *const mn = "slli";
                llvm::Value *const shifted = builder->CreateShl(src, imm, mn);
                gen_set_reg(dest, shifted);
            }
            break;
        case 2://SLTI
            {//set 1 if less than immediate (signed)
                static const char *const mn = "slti";
                gen_set_reg(dest, builder->CreateICmpSLT(src, imm, mn));
            }
            break;
        case 3://SLTIU
            {//set 1 if less than immediate (unsigned)
                static const char *const mn = "sltiu";
                gen_set_reg(dest, builder->CreateICmpULT(src, imm, mn));
            }
            break;
        case 4://XORI
            {//XOR sign extended immediate
                static const char *const mn = "xori";
                llvm::Value *const extended = builder->CreateSExt(i12, get_reg_type(), mn);
                gen_set_reg(dest, builder->CreateXor(extended, src, mn));
            }
            break;
        case 5://SRLI, SRAI
            {//shift right logical or arithmetric
                const target_ulong logical_or_arithmetric = bit_sub<25, 7>(insn);
                jcpu_assert(logical_or_arithmetric == 0 || logical_or_arithmetric == 0x20);
                const bool logical = logical_or_arithmetric == 0;
                static const char *const mn[2]  = {"srli", "srai"};
                llvm::Value *const shifted = logical ?
                    builder->CreateLShr(src, imm, mn[0]) : builder->CreateAShr(src, imm, mn[1]);
                gen_set_reg(dest, shifted);
            }
            break;
        case 6://ORI
            {//OR sign extended immediate
                static const char *const mn = "ori";
                llvm::Value *const extended = builder->CreateSExt(i12, get_reg_type(), mn);
                gen_set_reg(dest, builder->CreateOr(extended, src, mn));
            }
            break;
        case 7://ANDI
            {//AND sign extended immediate
                static const char *const mn = "andi";
                llvm::Value *const extended = builder->CreateSExt(i12, get_reg_type(), mn);
                gen_set_reg(dest, builder->CreateAnd(extended, src, mn));
            }
            break;
        default:
            jcpu_assert(!"Never comes here");
    }
    return false; 
} 


bool riscv_vm::disas_insn_integer_reg(target_ulong insn) {
    llvm::Value *const src[2] = {
        gen_get_reg(static_cast<riscv_arch::reg_e>(bit_sub<15, 5>(insn))),
        gen_get_reg(static_cast<riscv_arch::reg_e>(bit_sub<20, 5>(insn)))
    };
    const target_ulong funct3 = bit_sub<12, 3>(insn);
    const target_ulong funct7 = bit_sub<25, 7>(insn);
    llvm::Value *result = NULL;
    switch(funct3)
    {
        case 0://add, sub
            jcpu_assert(funct7 == 0 || funct7 == 32);
            result = funct7 == 0 ?
                builder->CreateAdd(src[0], src[1], "add") : builder->CreateSub(src[0], src[1], "sub");
            break;
        case 1://sll
            jcpu_assert(funct7 == 0);
            result = builder->CreateShl(src[0], src[1], "sll");
            break;
        case 2://slt
            jcpu_assert(funct7 == 0);
            result = builder->CreateICmpSLT(src[0], src[1], "slt");
            break;
        case 3://sltu
            jcpu_assert(funct7 == 0);
            result = builder->CreateICmpULT(src[0], src[1], "sltu");
            break;
        case 4://xor
            jcpu_assert(funct7 == 0);
            result = builder->CreateXor(src[0], src[1], "xor");
            break;
        case 5://srl, sra
            jcpu_assert(funct7 == 0 || funct7 == 32);
            result = funct7 == 0 ?
                builder->CreateLShr(src[0], src[1], "srl") : builder->CreateAShr(src[0], src[1], "sra");
            break;
        case 6://or
            jcpu_assert(funct7 == 0);
            result = builder->CreateOr(src[0], src[1], "or");
            break;
        case 7://and
            jcpu_assert(funct7 == 0);
            result = builder->CreateAnd(src[0], src[1], "and");
            break;
        default:jcpu_assert(!"Never comes here");
    }
    const riscv_arch::reg_e dest = static_cast<riscv_arch::reg_e>(bit_sub<7, 5>(insn));
    gen_set_reg(dest, result);

    return false; 
} 


bool riscv_vm::disas_insn_load(target_ulong insn) {
    llvm::ConstantInt *const i12 = llvm::ConstantInt::get(*context, llvm::APInt(12, bit_sub<20, 12>(insn)));
    const riscv_arch::reg_e dest = static_cast<riscv_arch::reg_e>(bit_sub<7, 5>(insn));
    llvm::Value *const base = gen_get_reg(static_cast<riscv_arch::reg_e>(bit_sub<15, 5>(insn)));
    llvm::Value *const extended = builder->CreateSExt(i12, get_reg_type());
    llvm::Value *const addr = builder->CreateAdd(extended, base, "load");
    const target_ulong opc = bit_sub<12, 3>(insn);
    switch(opc)
    {
        case 0://lb
        case 4://lbu
            {
                static const char *const mn[2] = {"lb", "lbu"};
                llvm::Value *const dat = gen_lw(addr, 1);
                llvm::Value *const dat_ext = opc == 0 ? 
                    builder->CreateSExt(dat, get_reg_type(), mn[0]) : builder->CreateZExt(dat, get_reg_type(), mn[1]);
                gen_set_reg(dest, dat_ext);
            }
            break;
        case 1://lh
        case 5://lbu
            {
                static const char *const mn[2] = {"lh", "lhu"};
                llvm::Value *const dat = gen_lw(addr, 2);
                llvm::Value *const dat_ext = opc == 1 ? 
                    builder->CreateSExt(dat, get_reg_type(), mn[0]) : builder->CreateZExt(dat, get_reg_type(), mn[1]);
                gen_set_reg(dest, dat_ext);
            }
            break;
        case 2://lw
        case 6://lwu
            {
                static const char *const mn[2] = {"lw", "lwu"};
                llvm::Value *const dat = gen_lw(addr, 4);
                llvm::Value *const dat_ext = opc == 2 ? 
                    builder->CreateSExt(dat, get_reg_type(), mn[0]) : builder->CreateZExt(dat, get_reg_type(), mn[1]);
                gen_set_reg(dest, dat_ext);
            }
            break;
        case 3://ld
            {
                jcpu_assert(riscv_arch::reg_bit_width == 64);
                //static const char *const mn = {"ld"};
                llvm::Value *const dat = gen_lw(addr, 8);
                gen_set_reg(dest, dat);
            }
            break;
        default:
            jcpu_assert(!"Never comes here");
    }
    return false; 
} 

bool riscv_vm::disas_insn_store(target_ulong insn) {
    const target_ulong opc = bit_sub<12, 3>(insn);
    jcpu_assert(0 <= opc && opc <= 3);
    static const char *const mn[4] = {"sb", "sh", "sw", "sd"};

    const target_ulong offset_tmp = (bit_sub<25, 7>(insn) << 5) | bit_sub<7, 5>(insn);
    llvm::ConstantInt *const i12 = llvm::ConstantInt::get(*context, llvm::APInt(12, offset_tmp));
    llvm::Value *const val = gen_get_reg(static_cast<riscv_arch::reg_e>(bit_sub<20, 5>(insn)));
    llvm::Value *const base = gen_get_reg(static_cast<riscv_arch::reg_e>(bit_sub<15, 5>(insn)));
    llvm::Value *const extended = builder->CreateSExt(i12, get_reg_type(), mn[opc]);
    llvm::Value *const addr = builder->CreateAdd(extended, base, mn[opc]);

    gen_sw(addr, 1U << opc, val);

    return false; 
} 

bool riscv_vm::disas_insn_cond_branch(target_ulong insn) {
    const target_ulong offset_imm = 
        (bit_sub<31, 1>(insn) << 12) |
        (bit_sub<7, 1>(insn) << 11) |
        (bit_sub<25, 6>(insn) << 5) |
        (bit_sub<8, 4>(insn) << 1);
    llvm::ConstantInt *const i13 = llvm::ConstantInt::get(*context, llvm::APInt(13, offset_imm));

    const target_ulong opc = bit_sub<12, 3>(insn);
    jcpu_assert(0 <= opc && opc <= 7 && opc != 2 && opc != 3);
    static const char *const mn_table[8] = {"beq", "bne", "", "", "blt", "bge", "bltu", "bgeu"};
    const char *const mn = mn_table[opc];

    llvm::Value *const extended = builder->CreateSExt(i13, get_reg_type(), mn);
    llvm::Value *const just_4 = gen_const(4);

    llvm::Value *const val[2] = {
        gen_get_reg(static_cast<riscv_arch::reg_e>(bit_sub<15, 5>(insn))),
        gen_get_reg(static_cast<riscv_arch::reg_e>(bit_sub<20, 5>(insn)))
    };

    llvm::Value * flag = NULL;
    switch(opc)
    {
        case 0:
            flag = builder->CreateICmpEQ(val[0], val[1], mn);
            break;
        case 1:
            flag = builder->CreateICmpNE(val[0], val[1], mn);
            break;
        case 4:
            flag = builder->CreateICmpSLT(val[0], val[1], mn);
            break;
        case 5:
            flag = builder->CreateICmpSGE(val[0], val[1], mn);
            break;
        case 6:
            flag = builder->CreateICmpULT(val[0], val[1], mn);
            break;
        case 7:
            flag = builder->CreateICmpUGE(val[0], val[1], mn);
            break;
        default:jcpu_assert(!"Never comes here");
    }
    llvm::Value *const offset = builder->CreateSelect(flag, extended, just_4, mn);
    llvm::Value *const next_pc = builder->CreateAdd(gen_get_pc(), offset, mn);
    gen_set_reg(riscv_arch::REG_PNEXT_PC, next_pc);

    return true; 
} 

bool riscv_vm::disas_insn_jump(target_ulong insn) {
    const unsigned int kind = bit_sub<2, 5>(insn);
    jcpu_assert(kind == 0x19 || kind == 0x1b);

    if(kind == 0x19){//jalr
        {
            const target_ulong zero = bit_sub<12, 3>(insn);
            jcpu_assert(zero == 0);
        }
        static const char *const mn = "jalr";
        llvm::ConstantInt *const i12 = llvm::ConstantInt::get(*context, llvm::APInt(12, bit_sub<20, 12>(insn)));
        llvm::Value *const extended = builder->CreateSExt(i12, get_reg_type(), mn);
        llvm::Value *const base = gen_get_reg(static_cast<riscv_vm::reg_e>(bit_sub<15, 5>(insn)));
        llvm::Value *const next_pc = builder->CreateAdd(base, extended, mn);
        llvm::Value *const return_pc = builder->CreateAdd(gen_get_pc(), gen_const(4), mn);
        gen_set_reg(static_cast<riscv_vm::reg_e>(bit_sub<7, 5>(insn)), return_pc);
        gen_set_reg(riscv_arch::REG_PNEXT_PC, next_pc);
    }
    else {//jal
        jcpu_assert(kind == 0x1b);
        static const char *const mn = "jal";
        const target_ulong offset_raw =
            (bit_sub<31, 1>(insn) << 20) |
            (bit_sub<12, 8>(insn) << 12) |
            (bit_sub<20, 1>(insn) << 11) |
            (bit_sub<21, 10>(insn) << 1);
        llvm::ConstantInt *const i21 = llvm::ConstantInt::get(*context, llvm::APInt(21, offset_raw));
        llvm::Value *const extended = builder->CreateSExt(i21, get_reg_type(), mn);
        llvm::Value *const cur_pc = gen_get_pc();
        llvm::Value *const next_pc = builder->CreateAdd(cur_pc, extended, mn);
        llvm::Value *const return_pc = builder->CreateAdd(cur_pc, gen_const(4), mn);
        gen_set_reg(static_cast<riscv_vm::reg_e>(bit_sub<7, 5>(insn)), return_pc);
        gen_set_reg(riscv_arch::REG_PNEXT_PC, next_pc);
    }
    return true; 
} 




const basic_block *riscv_vm::disas(virt_addr_t start_pc_, int max_insn, const break_point *const bp){
    const phys_addr_t start_pc(start_pc_);
    start_func(start_pc);
    target_ulong pc;
    unsigned int num_insn = 0;
    if(max_insn < 0){
        bool done = false;
        for(pc = start_pc; !done; pc += 4){
            if(bp && bp->get_pc() == pc){
                gen_set_reg(riscv_arch::REG_PNEXT_PC, gen_const(pc));
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
            gen_set_reg(riscv_arch::REG_PC, gen_get_reg(riscv_arch::REG_PNEXT_PC));
        }
        else{
            gen_set_reg(riscv_arch::REG_PC, gen_const(pc));
            gen_set_reg(riscv_arch::REG_PNEXT_PC, gen_const(pc));
        }
    }
    llvm::Function *const f = end_func();
    const phys_addr_t end_pc(pc - 4);
    jcpu_assert(start_pc <= end_pc);
    basic_block *const bb = new basic_block(start_pc, end_pc, f, ee, num_insn);
    bb_man.add(bb);
#if defined(JCPU_RISCV_DEBUG) && JCPU_RISCV_DEBUG > 2
    dump_ir();
#endif
    return bb;
}

void riscv_vm::start_func(phys_addr_t pc_p){
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
    gen_set_reg(riscv_arch::REG_PC, gen_get_reg(riscv_arch::REG_PNEXT_PC, "prologue"));
#if defined(JCPU_RISCV_DEBUG) && JCPU_RISCV_DEBUG > 1
    gen_set_reg(riscv_arch::REG_PNEXT_PC, gen_const(0xFFFFFFFF)); //poison value
#endif
}

gdb::gdb_target_if::run_state_e riscv_vm::run(){
    virt_addr_t pc(get_reg_func(riscv_arch::REG_PC));
    for(;;){
        const break_point *const nearest = bp_man.find_nearest(pc);
        if(nearest && nearest->get_pc() == pc){
            return RUN_STAT_BREAK;
        }
        const phys_addr_t pc_p = code_v2p(pc);
        const basic_block *const bb = bb_man.exists_by_start_addr(pc_p) ? bb_man.find_by_start_addr(pc_p) : disas(pc, -1, nearest);
        pc = bb->exec();

#if defined(JCPU_RISCV_DEBUG) && JCPU_RISCV_DEBUG > 1
        dump_regs();
#endif
        total_icount += bb->get_icount();
    }
    jcpu_assert(!"Never comes here");
    return RUN_STAT_NORMAL;
}

gdb::gdb_target_if::run_state_e riscv_vm::step_exec(){
    virt_addr_t pc(get_reg_func(riscv_arch::REG_PC));

    const break_point *const nearest = bp_man.find_nearest(pc);
    if(nearest && nearest->get_pc() == pc) return RUN_STAT_BREAK;
    const phys_addr_t pc_p = code_v2p(pc);
    bb_man.invalidate(pc_p, pc_p + phys_addr_t(4));
    const basic_block *const bb = bb_man.exists_by_start_addr(pc_p) ? bb_man.find_by_start_addr(pc_p) : disas(pc, 1, nearest);
    pc = bb->exec();
#if defined(JCPU_RISCV_DEBUG) && JCPU_RISCV_DEBUG > 1
    dump_regs();
#endif
    total_icount += bb->get_icount();
    return RUN_STAT_NORMAL;
}

void riscv_vm::dump_regs()const{
    for(unsigned int i = 0; i < riscv_arch::NUM_REGS; ++i){
        if(i < 32){
            std::cout << "reg[" << std::dec << std::setw(2) << std::setfill('0') << i << "]:";
        }
        else if(i == riscv_arch::REG_PC){
            std::cout << "pc:";
        }
        else if(i == riscv_arch::REG_PNEXT_PC){
            std::cout << "jump_to:";
        }
        else{assert(!"Unknown register");}
        std::cout << std::hex << std::setw(8) << std::setfill('0') << get_reg_func(i);
        if((i & 3) != 3) std::cout << "  ";
        else std::cout << '\n';
    }
    std::cout << std::endl;
}

void riscv_vm::interrupt(int irq_id, bool enable){
    jcpu_assert(irq_id == 0);
    irq_status = enable;
}

void riscv_vm::reset(){
    irq_status = false;
}


riscv::riscv(const char *model) : jcpu(), vm(JCPU_NULLPTR){
}

riscv::~riscv(){
    delete vm;
}

void riscv::interrupt(int irq_id, bool enable){
    if(vm) vm->interrupt(irq_id, enable);
}

void riscv::reset(bool reset_on){
    if(vm) vm->reset();
}

void riscv::run(run_option_e opt){
    if(!vm){
        vm = new riscv_vm(*ext_ifs);
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

uint64_t riscv::get_total_insn_count()const{
    return vm->get_total_insn_count();
}


} //end of namespace riscv
} //end of namespace jcpu

