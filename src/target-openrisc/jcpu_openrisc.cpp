#include <cstdio>
#include <iostream>
#include <iomanip>

#include "jcpu_llvm_headers.h"
#include "jcpu_vm.h"
#include "gdbserver.h"
#include "jcpu_openrisc.h"

//#define JCPU_OPENRISC_DEBUG 3


namespace {
#define jcpu_or_disas_assert(cond) do{if(!(cond)){ \
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



} //end of unnamed namespace

namespace jcpu{
namespace openrisc{

struct openrisc_arch{
    typedef uint32_t target_ulong;
    const static unsigned int reg_bit_width = 32;
    typedef vm::primitive_type_holder<target_ulong, openrisc_arch, 0> virt_addr_t;
    typedef vm::primitive_type_holder<target_ulong, openrisc_arch, 1> phys_addr_t;
    enum reg_e{
        REG_GR00, REG_GR01, REG_GR02, REG_GR03,
        REG_GR04, REG_GR05, REG_GR06, REG_GR07,
        REG_GR08, REG_GR09, REG_LR = REG_GR09, REG_GR10, REG_GR11,
        REG_GR12, REG_GR13, REG_GR14, REG_GR15,
        REG_GR16, REG_GR17, REG_GR18, REG_GR19,
        REG_GR20, REG_GR21, REG_GR22, REG_GR23,
        REG_GR24, REG_GR25, REG_GR26, REG_GR27,
        REG_GR28, REG_GR29, REG_GR30, REG_GR31,
        REG_PC, REG_SR, REG_CPUCFGR, REG_EPCR0, REG_PNEXT_PC, NUM_REGS
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

typedef openrisc_arch::virt_addr_t virt_addr_t;
typedef openrisc_arch::phys_addr_t phys_addr_t;
typedef openrisc_arch::target_ulong target_ulong;

typedef vm::basic_block<openrisc_arch> basic_block;
typedef vm::bb_manager<openrisc_arch> bb_manager;
typedef vm::break_point<openrisc_arch> break_point;
typedef vm::bp_manager<openrisc_arch> bp_manager;

class openrisc_vm : public vm::jcpu_vm_base<openrisc_arch>{
    //gdb_target_if
    virtual void get_reg_value(std::vector<uint64_t> &)const JCPU_OVERRIDE;
    virtual void set_reg_value(unsigned int, uint64_t)JCPU_OVERRIDE;

    bool irq_status;

    bool disas_insn(virt_addr_t, int *);
    bool disas_arith(target_ulong);
    bool disas_logical(target_ulong);
    bool disas_compare_immediate(target_ulong);
    bool disas_compare(target_ulong);
    bool disas_others(target_ulong, int *);
    virtual void start_func(phys_addr_t) JCPU_OVERRIDE;
    void gen_set_sr(sr_flag_e flag, llvm::Value *val, const char *mn = "")const{//val must be 0 or 1
        using namespace llvm;
        Value *const sr = gen_get_reg(openrisc_arch::REG_SR, mn);
        Value *const drop_mask = gen_const(~(static_cast<target_ulong>(1) << flag));
        Value *const shifted_val = builder->CreateShl(builder->CreateZExt(val, get_reg_type()), flag, mn);
        Value *const new_sr = builder->CreateOr(builder->CreateAnd(sr, drop_mask, mn), shifted_val, mn);
        gen_set_reg(openrisc_arch::REG_SR, new_sr, mn);
    }
    const basic_block *disas(virt_addr_t, int, const break_point *);
    virtual run_state_e step_exec() JCPU_OVERRIDE;
    phys_addr_t code_v2p(virt_addr_t pc){return static_cast<phys_addr_t>(pc);} //FIXME implement MMU
    public:
    explicit openrisc_vm(jcpu_ext_if &);
    virtual run_state_e run() JCPU_OVERRIDE;
    virtual void dump_regs()const JCPU_OVERRIDE;
    void reset();
    void interrupt(int, bool);
};

openrisc_vm::openrisc_vm(jcpu_ext_if &ifs) : vm::jcpu_vm_base<openrisc_arch>(ifs) 
{
    const unsigned int address_space = 5;
    const unsigned int bit = sizeof(target_ulong) * 8;
    const unsigned int num_regs = openrisc_arch::NUM_REGS;
    llvm::ArrayType *const ATy = llvm::ArrayType::get(llvm::IntegerType::get(*context, bit), num_regs);
    std::vector<llvm::Constant*> Initializer;
    Initializer.reserve(num_regs);

    for(unsigned int i = 0; i < num_regs; ++i){
        const unsigned int reg_init_val = (i == openrisc_arch::REG_PC || i == openrisc_arch::REG_PNEXT_PC) ? 0x100 : 0;
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


void openrisc_vm::get_reg_value(std::vector<uint64_t> &regs)const{
    regs.clear();
    for(unsigned int i = 0; i < 32; ++i){
        regs.push_back(get_reg_func(i));
    }
    //regs.push_back(get_reg_func(openrisc_arch::REG_PC));
    regs.push_back(get_reg_func(openrisc_arch::REG_PNEXT_PC));
}

void openrisc_vm::set_reg_value(unsigned int reg_idx, uint64_t reg_val){
    jcpu_assert(reg_idx < 32 + 1);
    set_reg_func(reg_idx, reg_val);
}

bool openrisc_vm::disas_insn(virt_addr_t pc_v, int *const insn_depth){
    ++(*insn_depth);
    const phys_addr_t pc = code_v2p(pc_v);
    struct push_and_pop_pc{
        openrisc_vm &vm;
        push_and_pop_pc(openrisc_vm &vm, virt_addr_t pc_v, phys_addr_t pc_p) : vm(vm){
            vm.processing_pc.push(std::make_pair(pc_v, pc_p));
        }
        ~push_and_pop_pc(){
            vm.processing_pc.pop();
        }
    } push_and_pop_pc(*this, pc_v, pc);
    const target_ulong insn = ext_ifs.mem_read(pc, sizeof(target_ulong));
    const unsigned int kind = bit_sub<26, 6>(insn);
#if defined(JCPU_OPENRISC_DEBUG) && JCPU_OPENRISC_DEBUG > 0
    std::cout << std::hex << "pc:" << pc << " INSN:" << std::setw(8) << std::setfill('0') << insn << " kind:" << kind << std::endl;
#endif
#if defined(JCPU_OPENRISC_DEBUG) && JCPU_OPENRISC_DEBUG > 2
    builder->CreateCall(mod->getFunction("jcpu_vm_dump_regs"));
#endif
    switch(kind){
        case 0x08: //system
            jcpu_or_disas_assert(!"Not implemented yet");
            break;
        case 0x2E: //logical
            return disas_logical(insn);
        case 0x2F: //compare
            return disas_compare_immediate(insn);
        case 0x31: //media
            jcpu_or_disas_assert(!"Not implemented yet");
            break;
        case 0x32: //floating point
            jcpu_or_disas_assert(!"Not implemented yet");
            break;
        case 0x38: //arithmetric
            return disas_arith(insn);
            break;
        case 0x39: //compare
            return disas_compare(insn);
        default: //others
            return disas_others(insn, insn_depth);
    }
#if defined(JCPU_OPENRISC_DEBUG) && JCPU_OPENRISC_DEBUG > 2
    builder->CreateCall(mod->getFunction("jcpu_vm_dump_regs"));
#endif
    jcpu_assert(!"Never comes here");
    return false;//suppress warning
}

const basic_block *openrisc_vm::disas(virt_addr_t start_pc_, int max_insn, const break_point *const bp){
    const phys_addr_t start_pc(start_pc_);
    start_func(start_pc);
    target_ulong pc;
    unsigned int num_insn = 0;
    if(max_insn < 0){
        bool done = false;
        for(pc = start_pc; !done; pc += 4){
            if(bp && bp->get_pc() == pc){
                gen_set_reg(openrisc_arch::REG_PNEXT_PC, gen_const(pc));
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
            gen_set_reg(openrisc_arch::REG_PC, gen_get_reg(openrisc_arch::REG_PNEXT_PC));
        }
        else{
            gen_set_reg(openrisc_arch::REG_PC, gen_const(pc));
            gen_set_reg(openrisc_arch::REG_PNEXT_PC, gen_const(pc));
        }
    }
    llvm::Function *const f = end_func();
    const phys_addr_t end_pc(pc - 4);
    jcpu_assert(start_pc <= end_pc);
    basic_block *const bb = new basic_block(start_pc, end_pc, f, ee, num_insn);
    bb_man.add(bb);
#if defined(JCPU_OPENRISC_DEBUG) && JCPU_OPENRISC_DEBUG > 2
    dump_ir();
#endif
    return bb;
}

bool openrisc_vm::disas_arith(target_ulong insn){
    using namespace llvm;
    const target_ulong op = bit_sub<0, 4>(insn);
    const target_ulong op2 = bit_sub<8, 2>(insn);
    const target_ulong op3 = bit_sub<6, 4>(insn);
    const openrisc_arch::reg_e rD = static_cast<openrisc_arch::reg_e>(bit_sub<21, 5>(insn));
    const openrisc_arch::reg_e rA = static_cast<openrisc_arch::reg_e>(bit_sub<16, 5>(insn));
    const openrisc_arch::reg_e rB = static_cast<openrisc_arch::reg_e>(bit_sub<11, 5>(insn));
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
            case 0x03: //l.and rD = rA | rB
                gen_set_reg(rD, builder->CreateAnd(gen_get_reg(rA, "l.or_A"), gen_get_reg(rB, "l.or_B"), "l.or"), "l.or_D");
                return false;
            case 0x04: //l.or rD = rA | rB
                gen_set_reg(rD, builder->CreateOr(gen_get_reg(rA, "l.or_A"), gen_get_reg(rB, "l.or_B"), "l.or"), "l.or_D");
                return false;
            case 0x08:
                if(op3 == 0x00){//l.sll rD = rA << rB[4:0]
                    Value *const rega = gen_get_reg(rA, "l.sll_A");
                    Value *const regb5 = builder->CreateTrunc(gen_get_reg(rB, "l.sll_B"), IntegerType::get(*context, 5), "l.sll_B5");
                    gen_set_reg(rD, builder->CreateShl(rega, regb5, "l.sll"), "l.sll_D");
                }
                else if(op3 == 0x01){//l.srl rD = rA >> rB[4:0]
                    Value *const rega = gen_get_reg(rA, "l.srl_A");
                    Value *const regb5 = builder->CreateTrunc(gen_get_reg(rB, "l.srl_B"), IntegerType::get(*context, 5), "l.srl_B5");
                    gen_set_reg(rD, builder->CreateLShr(rega, regb5, "l.srl"), "l.srl_D");
                }
                else{
                    jcpu_or_disas_assert(!"Not implemented yet");
                }
                return false;
            default:
                jcpu_or_disas_assert(!"Not implemented yet");
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
                jcpu_or_disas_assert(!"Not implemented yet");
                break;
        }
    }
    jcpu_or_disas_assert(!"Never comes here");
}

bool openrisc_vm::disas_logical(target_ulong insn){
    using namespace llvm;
    const target_ulong op = bit_sub<6, 2>(insn);
    const openrisc_arch::reg_e rD = static_cast<openrisc_arch::reg_e>(bit_sub<21, 5>(insn));
    const openrisc_arch::reg_e rA = static_cast<openrisc_arch::reg_e>(bit_sub<16, 5>(insn));

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
            jcpu_or_disas_assert(!"Not implemented yet");
            break;
    }
    jcpu_or_disas_assert(!"Never comes here");
}

bool openrisc_vm::disas_compare_immediate(target_ulong insn){
    using namespace llvm;
    const target_ulong op0 = bit_sub<21, 5>(insn);
    const openrisc_arch::reg_e rA = static_cast<openrisc_arch::reg_e>(bit_sub<16, 5>(insn));
    ConstantInt *const I16 = ConstantInt::get(*context, APInt(16, bit_sub<0, 16>(insn)));
    Value *const I16s = builder->CreateSExt(I16, get_reg_type());

    switch(op0){
        case 0x00: //l.sfeqi SR[F] = rA == sext(I16)
            gen_set_sr(openrisc_arch::SR_F, builder->CreateICmpEQ(gen_get_reg(rA), I16s, "l.sfeqi"), "l.sfeqi");
            return false;
        case 0x01: //l.sfnei SR[F} = rA != sext(I16)
            gen_set_sr(openrisc_arch::SR_F, builder->CreateICmpNE(gen_get_reg(rA), I16s, "l.sfnwi"), "l.sfnei");
            return false;
        case 0x02: //l.sfgtui SR[F] = rA > sext(I16)
            gen_set_sr(openrisc_arch::SR_F, builder->CreateICmpUGT(gen_get_reg(rA), I16s, "l.sfgtui"), "l.sfgtui");
            return false;
        case 0x05: //l.sfleui SR[F} = rA <= sext(I16)
            gen_set_sr(openrisc_arch::SR_F, builder->CreateICmpULE(gen_get_reg(rA), I16s, "l.sfleui"), "l.sfleui");
            return false;
        case 0x0A: //l.sfgtsi SR[F] = rA > sext(I16)
            gen_set_sr(openrisc_arch::SR_F, builder->CreateICmpSGT(gen_get_reg(rA), I16s, "l.sfgtsi"), "l.sfgtsi");
            return false;
        case 0x0B: //l.sfgesi SR[F] = rA >= sext(I16)
            gen_set_sr(openrisc_arch::SR_F, builder->CreateICmpSGE(gen_get_reg(rA), I16s, "l.sfgesi"), "l.sfgesi");
            return false;
        case 0x0C: //l.sfltsi SR[F] = rA < sext(I16)
            gen_set_sr(openrisc_arch::SR_F, builder->CreateICmpSLT(gen_get_reg(rA), I16s, "l.sfltsi"), "l.sfltsi");
            return false;
        case 0x0D: //l.sflesi SR[F] = rA <= sext(I16)
            gen_set_sr(openrisc_arch::SR_F, builder->CreateICmpSLE(gen_get_reg(rA, "l.sflesi_A"), I16s, "l.sflesi_I"), "l.sflesi");
            return false;
        default:
            jcpu_or_disas_assert(!"Not implemented yet");
            break;
    }
}


bool openrisc_vm::disas_compare(target_ulong insn){
    using namespace llvm;
    const target_ulong op0 = bit_sub<21, 5>(insn);
    const openrisc_arch::reg_e rA = static_cast<openrisc_arch::reg_e>(bit_sub<16, 5>(insn));
    const openrisc_arch::reg_e rB = static_cast<openrisc_arch::reg_e>(bit_sub<11, 5>(insn));

    switch(op0){
        case 0x00: //l.sfeq SR[F] = rA == rB
            gen_set_sr(openrisc_arch::SR_F, builder->CreateICmpEQ(gen_get_reg(rA, "l.sfeq"), gen_get_reg(rB, "l.sfeq"), "l.sfeq"), "l.sfeq");
            return false;
        case 0x01: //l.sfne SR[F] <= rA != rB
            gen_set_sr(openrisc_arch::SR_F, builder->CreateICmpNE(gen_get_reg(rA, "l.sfne"), gen_get_reg(rB, "l.sfne"), "l.sfne"), "l.sfne");
            return false;
        case 0x02: //l.sfgtu SR[F] <= rA > rB
            gen_set_sr(openrisc_arch::SR_F, builder->CreateICmpUGT(gen_get_reg(rA, "l.sfgtu_A"), gen_get_reg(rB, "l.sfgtu_B"), "l.sfgtu"), "l.sfgtu_SRF");
            return false;
        case 0x03: //l.sfgeu SR[F] <= rA >= rB
            gen_set_sr(openrisc_arch::SR_F, builder->CreateICmpUGE(gen_get_reg(rA, "l.sfgeu_A"), gen_get_reg(rB, "l.sfgeu_B"), "l.sfgeu"), "l.sfgeu_SRF");
            return false;
        case 0x04: //l.sfltu SR[F} <= rA < rB
            gen_set_sr(openrisc_arch::SR_F, builder->CreateICmpULT(gen_get_reg(rA, "l.sfltu_A"), gen_get_reg(rB, "l.sfltu_B"), "l.sfltu"), "l.sfltu_SRF");
            return false;
        case 0x05: //l.sfleu SR[F] <= rA <= rB
            gen_set_sr(openrisc_arch::SR_F, builder->CreateICmpULE(gen_get_reg(rA, "l.sfleu_A"), gen_get_reg(rB, "l.sfleu_B"), "l.sfleu"), "l.sfleu_SRF");
            return false;
        case 0x0A: //l.sfgts SR[F] <= rA > rB
            gen_set_sr(openrisc_arch::SR_F, builder->CreateICmpSGT(gen_get_reg(rA), gen_get_reg(rB)));
            return false;
        case 0x0B: //l.sfges SR[F] <= rA >= rB
            gen_set_sr(openrisc_arch::SR_F, builder->CreateICmpSGE(gen_get_reg(rA), gen_get_reg(rB)));
            return false;
        case 0x0C: //l.sflts SR[F] <= rA < rB
            gen_set_sr(openrisc_arch::SR_F, builder->CreateICmpSLT(gen_get_reg(rA, "l.sflts_A"), gen_get_reg(rB, "l.sflts_B"), "l.sflts"), "l.sflts_SRF");
            return false;
        case 0x0D: //l.sfles SR[F} <= rA <= rB
            gen_set_sr(openrisc_arch::SR_F, builder->CreateICmpSLE(gen_get_reg(rA), gen_get_reg(rB)));
            return false;
        default:
            jcpu_or_disas_assert(!"Not implemented yet");
            break;
    }
}

bool openrisc_vm::disas_others(target_ulong insn, int *const insn_depth){
    using namespace llvm;
    const target_ulong op0 = bit_sub<26, 6>(insn);
    const target_ulong op1 = bit_sub<24, 2>(insn);
    const openrisc_arch::reg_e rD = static_cast<openrisc_arch::reg_e>(bit_sub<21, 5>(insn));
    const openrisc_arch::reg_e rA = static_cast<openrisc_arch::reg_e>(bit_sub<16, 5>(insn));
    const openrisc_arch::reg_e rB = static_cast<openrisc_arch::reg_e>(bit_sub<11, 5>(insn));
    //const target_ulong lo6 = bit_sub<5, 6>(insn);
    //const target_ulong k5 = bit_sub<0, 5>(insn);
    ConstantInt *const lo16 = ConstantInt::get(*context, APInt(16, bit_sub<0, 16>(insn)));
    //const target_ulong i5 = bit_sub<21, 5>(insn);
    ConstantInt *const I11 = ConstantInt::get(*context, APInt(11, bit_sub<0, 11>(insn)));
    ConstantInt *const n26 = ConstantInt::get(*context, APInt(26, bit_sub<0, 26>(insn)));
    ConstantInt *const I = ConstantInt::get(*context, APInt(16, (bit_sub<21, 5>(insn) << 11) | bit_sub<0, 11>(insn)));
#if defined(JCPU_OPENRISC_DEBUG) && JCPU_OPENRISC_DEBUG > 0
    //std::cerr << "op0:" << std::hex << op0 << " rA:" << bit_sub<21, 5>(insn) << " rB:" << bit_sub<11, 5>(insn) << " rD" << bit_sub<21, 5>(insn) << " lo16:" << bit_sub<0, 16>(insn) << std::endl;
#endif
    switch(op0){
        case 0x00://l.j PC = sext(n26) << 2 + PC
            {
                static const char *const mn = "l.j";
                ConstantInt *const pc = gen_get_pc();
                Value *const pc_offset = builder->CreateShl(builder->CreateSExt(n26, get_reg_type(), mn), 2, mn);
                gen_set_reg(openrisc_arch::REG_PNEXT_PC, builder->CreateAdd(pc, pc_offset, mn), mn);
                const bool ret = disas_insn(processing_pc.top().first + static_cast<virt_addr_t>(4), insn_depth); //delay slot
                jcpu_or_disas_assert(!ret);
                return true;
            }
        case 0x01://l.jal
            {
                ConstantInt *const pc = gen_get_pc();
                Value *const pc_offset = builder->CreateShl(builder->CreateSExt(n26, get_reg_type()), 2);
                gen_set_reg(openrisc_arch::REG_PNEXT_PC, builder->CreateAdd(pc, pc_offset));
                Value *const nd_bit = builder->CreateAnd(builder->CreateLShr(gen_get_reg(openrisc_arch::REG_CPUCFGR), gen_const(openrisc_arch::CPUCFGR_ND)), gen_const(1));
                gen_set_reg(openrisc_arch::REG_LR, builder->CreateAdd(pc, gen_cond_code(nd_bit, gen_const(4), gen_const(8))));//check spr
                const bool ret = disas_insn(processing_pc.top().first + static_cast<virt_addr_t>(4), insn_depth); //delay slot
                jcpu_or_disas_assert(!ret);
                return true;
            }
        case 0x03: //l.bnf
        case 0x04: //l.bf
            {
                const bool is_bnf = op0 == 0x03;
                const char *const mn = is_bnf ? "l.bnf" : "l.bf";
                const virt_addr_t &pc = processing_pc.top().first;
                Value *const flag = gen_get_reg(openrisc_arch::REG_SR);
                Value *const shifted_flag = builder->CreateAnd(builder->CreateLShr(flag, openrisc_arch::SR_F), 1, mn);
                Value *const pc_offset = builder->CreateShl(builder->CreateSExt(lo16, get_reg_type(), mn), 2, mn);
                Value *not_taken_pc = gen_const(pc + 8);
                Value *taken_pc = builder->CreateAdd(gen_const(pc), pc_offset, mn);
                if(is_bnf) std::swap(taken_pc, not_taken_pc);
                Value *const next_pc = gen_cond_code(shifted_flag, taken_pc, not_taken_pc);
                gen_set_reg(openrisc_arch::REG_PNEXT_PC, next_pc);
                gen_set_reg(openrisc_arch::REG_SR, builder->CreateAnd(flag, ~(static_cast<target_ulong>(1) << openrisc_arch::SR_F), mn));
                const bool ret = disas_insn(pc + static_cast<virt_addr_t>(4), insn_depth); //delay slot
                jcpu_or_disas_assert(!ret);
            }
            return true;
        case 0x05:
            if(op1 == 1){//l.nop
                return false;
            }
            else{
                jcpu_or_disas_assert(!"Not implemented yet");
            }
            break;
        case 0x06:
            if(!bit_sub<16, 1>(insn)){//l.movhi rD = extz(lo16) << 16
                static const char *const mn = "l.movhi";
                gen_set_reg(rD, builder->CreateShl(builder->CreateZExt(lo16, get_reg_type(), mn), gen_const(16), mn), mn);
                return false;
            }
            else{
                jcpu_or_disas_assert(!"Not implemented yet");
            }
            break;
        case 0x09: //l.rfe PC <= PRCR, SR <= ESR
            {
                static const char *const mn = "l.rfe";
                gen_set_reg(openrisc_arch::REG_PNEXT_PC, gen_get_reg(openrisc_arch::REG_EPCR0, mn), mn);
                gen_set_sr(openrisc_arch::SR_IEE, ConstantInt::get(*context, APInt(32, 0)));
            }
            return true;
        case 0x11: //l.jr PC = rB
            {
                static const char *const mn = "l.jr";
                gen_set_reg(openrisc_arch::REG_PNEXT_PC, gen_get_reg(rB, mn), mn);
                const bool ret = disas_insn(processing_pc.top().first + static_cast<virt_addr_t>(4), insn_depth); //delay slot
                jcpu_or_disas_assert(!ret);
            }
            return true;
        case 0x12: //l.jalr PC = rB, LR <= PC + 8
            {
                static const char *const mn = "l.jalr";
                gen_set_reg(openrisc_arch::REG_PNEXT_PC, gen_get_reg(rB, mn), mn);
                const bool ret = disas_insn(processing_pc.top().first + static_cast<virt_addr_t>(4), insn_depth); //delay slot
                jcpu_or_disas_assert(!ret);
                //gen_set_reg(openrisc_arch::REG_LR, processing_pc.top().first + static_cast<virt_addr_t>(8), insn_depth); //delay slot
                Value *const nd_bit = builder->CreateAnd(builder->CreateLShr(gen_get_reg(openrisc_arch::REG_CPUCFGR), gen_const(openrisc_arch::CPUCFGR_ND)), gen_const(1));
                gen_set_reg(openrisc_arch::REG_LR, builder->CreateAdd(gen_get_pc(), gen_cond_code(nd_bit, gen_const(4), gen_const(8))));//check spr
            }
            return true;
        case 0x21: //l.lwz rD = (rA + sext(I))
            {
                Value *const addr = builder->CreateAdd(gen_get_reg(rA), builder->CreateSExt(I11, get_reg_type()));
                Value *const dat = gen_lw(addr, sizeof(target_ulong));
                gen_set_reg(rD, dat);
            }
            return false;
        case 0x23: //l.lbz rD = zext(lb(sext(lo16) + rA))
            {
                static const char *const mn = "l.lbz";
                Value *const addr = builder->CreateAdd(gen_get_reg(rA, mn), builder->CreateSExt(lo16, get_reg_type(), mn), mn);
                Value *const dat = gen_lw(addr, 1, mn);
                gen_set_reg(rD, builder->CreateZExt(dat, get_reg_type(), mn), mn);
            }
            return false;
        case 0x24: //l.lbs rD = sext(lb(sext(lo16) + rA))
            {
                static const char *const mn = "l.lbs";
                Value *const addr = builder->CreateAdd(gen_get_reg(rA, mn), builder->CreateSExt(lo16, get_reg_type(), mn), mn);
                Value *const dat = gen_lw(addr, 1, mn);
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
        case 0x2B: //l.xori (set rD (xor rA sext(lo16 )))
            gen_set_reg(rD, builder->CreateXor(gen_get_reg(rA), builder->CreateSExt(lo16, get_reg_type())));
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
                gen_sw(EA, sizeof(target_ulong), gen_get_reg(rB, mn), mn);
            }
            return false;
        case 0x36: //l.sb 
            {
                static const char *const mn = "l.sb";
                Value *const EA = builder->CreateAdd(gen_get_reg(rA, mn), builder->CreateSExt(I, get_reg_type(), mn), mn);
                gen_sw(EA, 1, gen_get_reg(rB, mn), mn);
            }
            return false;
        case 0x37: //l.sh
            {
                static const char *const mn = "l.sh";
                Value *const EA = builder->CreateAdd(gen_get_reg(rA, mn), builder->CreateSExt(I, get_reg_type(), mn), mn);
                gen_sw(EA, 2, gen_get_reg(rB, mn), mn);
            }
            return false;
 
        default:
            std::cerr << "Insn:" << std::hex << insn << std::endl;
            jcpu_or_disas_assert(!"Not implemented yet");
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
    gen_set_reg(openrisc_arch::REG_PC, gen_get_reg(openrisc_arch::REG_PNEXT_PC, "prologue"), "prologue");
#if defined(JCPU_OPENRISC_DEBUG) && JCPU_OPENRISC_DEBUG > 1
    gen_set_reg(openrisc_arch::REG_PNEXT_PC, gen_const(0xFFFFFFFF), "prologue"); //poison value
#endif
}

gdb::gdb_target_if::run_state_e openrisc_vm::run(){
    virt_addr_t pc(get_reg_func(openrisc_arch::REG_PC));
    for(;;){
        const break_point *const nearest = bp_man.find_nearest(pc);
        if(nearest && nearest->get_pc() == pc){
            return RUN_STAT_BREAK;
        }
        const phys_addr_t pc_p = code_v2p(pc);
        const basic_block *const bb = bb_man.exists_by_start_addr(pc_p) ? bb_man.find_by_start_addr(pc_p) : disas(pc, -1, nearest);
#if 0
std::cerr << "PC=" << std::hex << std::setw(8) << std::setfill('0') << pc << std::endl;
std::cerr
    << "R00=" << std::hex << std::setw(8) << std::setfill('0') << get_reg_func(0)
    << " R01=" << std::hex << std::setw(8) << std::setfill('0') << get_reg_func(1)
    << " R02=" << std::hex << std::setw(8) << std::setfill('0') << get_reg_func(2)
    << " R03=" << std::hex << std::setw(8) << std::setfill('0') << get_reg_func(3) << '\n'
    << "R04=" << std::hex << std::setw(8) << std::setfill('0') << get_reg_func(4)
    << " R05=" << std::hex << std::setw(8) << std::setfill('0') << get_reg_func(5)
    << " R06=" << std::hex << std::setw(8) << std::setfill('0') << get_reg_func(6)
    << " R07=" << std::hex << std::setw(8) << std::setfill('0') << get_reg_func(7) << '\n'
    << "R08=" << std::hex << std::setw(8) << std::setfill('0') << get_reg_func(8)
    << " R09=" << std::hex << std::setw(8) << std::setfill('0') << get_reg_func(9)
    << " R10=" << std::hex << std::setw(8) << std::setfill('0') << get_reg_func(10)
    << " R11=" << std::hex << std::setw(8) << std::setfill('0') << get_reg_func(11) << '\n'
    << "R12=" << std::hex << std::setw(8) << std::setfill('0') << get_reg_func(12)
    << " R13=" << std::hex << std::setw(8) << std::setfill('0') << get_reg_func(13)
    << " R14=" << std::hex << std::setw(8) << std::setfill('0') << get_reg_func(14)
    << " R15=" << std::hex << std::setw(8) << std::setfill('0') << get_reg_func(15) << '\n'
    << "R16=" << std::hex << std::setw(8) << std::setfill('0') << get_reg_func(16)
    << " R17=" << std::hex << std::setw(8) << std::setfill('0') << get_reg_func(17)
    << " R18=" << std::hex << std::setw(8) << std::setfill('0') << get_reg_func(18)
    << " R19=" << std::hex << std::setw(8) << std::setfill('0') << get_reg_func(19) << '\n'
    << "R20=" << std::hex << std::setw(8) << std::setfill('0') << get_reg_func(20)
    << " R21=" << std::hex << std::setw(8) << std::setfill('0') << get_reg_func(21)
    << " R22=" << std::hex << std::setw(8) << std::setfill('0') << get_reg_func(22)
    << " R23=" << std::hex << std::setw(8) << std::setfill('0') << get_reg_func(23) << '\n'
    << "R24=" << std::hex << std::setw(8) << std::setfill('0') << get_reg_func(24)
    << " R25=" << std::hex << std::setw(8) << std::setfill('0') << get_reg_func(25)
    << " R26=" << std::hex << std::setw(8) << std::setfill('0') << get_reg_func(26)
    << " R27=" << std::hex << std::setw(8) << std::setfill('0') << get_reg_func(27) << '\n'
    << "R28=" << std::hex << std::setw(8) << std::setfill('0') << get_reg_func(28)
    << " R29=" << std::hex << std::setw(8) << std::setfill('0') << get_reg_func(29)
    << " R30=" << std::hex << std::setw(8) << std::setfill('0') << get_reg_func(30)
    << " R31=" << std::hex << std::setw(8) << std::setfill('0') << get_reg_func(31) << std::endl;
#endif
        pc = bb->exec();

#if defined(JCPU_OPENRISC_DEBUG) && JCPU_OPENRISC_DEBUG > 1
        dump_regs();
#endif
        total_icount += bb->get_icount();
        const target_ulong sr = get_reg_func(openrisc_arch::REG_SR);
        if(irq_status && (sr & (1U << openrisc_arch::SR_IEE)) == 0){//jump to exception handler
            set_reg_func(openrisc_arch::REG_EPCR0, get_reg_func(openrisc_arch::REG_PNEXT_PC));
            set_reg_func(openrisc_arch::REG_PNEXT_PC, 0x800);
            set_reg_func(openrisc_arch::REG_SR, sr | (1U << openrisc_arch::SR_IEE));
            pc = virt_addr_t(0x800);
        }
    }
    jcpu_assert(!"Never comes here");
    return RUN_STAT_NORMAL;
}

gdb::gdb_target_if::run_state_e openrisc_vm::step_exec(){
    virt_addr_t pc(get_reg_func(openrisc_arch::REG_PC));
    const break_point *const nearest = bp_man.find_nearest(pc);
    const phys_addr_t pc_p = code_v2p(pc);
    bb_man.invalidate(pc_p, pc_p + phys_addr_t(4));
    const basic_block *const bb = bb_man.exists_by_start_addr(pc_p) ? bb_man.find_by_start_addr(pc_p) : disas(pc, 1, nearest);
    pc = bb->exec();
#if defined(JCPU_OPENRISC_DEBUG) && JCPU_OPENRISC_DEBUG > 1
    dump_regs();
#endif
    total_icount += bb->get_icount();
    jcpu_assert(irq_status == false);//Not implemented yet
    return (nearest && nearest->get_pc() == pc) ? RUN_STAT_BREAK : RUN_STAT_NORMAL;
}

void openrisc_vm::dump_regs()const{
    for(unsigned int i = 0; i < openrisc_arch::NUM_REGS; ++i){
        if(i < 32){
            std::cout << "reg[" << std::dec << std::setw(2) << std::setfill('0') << i << "]:";
        }
        else if(i == openrisc_arch::REG_PC){
            std::cout << "pc:";
        }
        else if(i == openrisc_arch::REG_SR){
            std::cout << "sr:";
        }
        else if(i == openrisc_arch::REG_PNEXT_PC){
            std::cout << "jump_to:";
        }
        else if(i == openrisc_arch::REG_CPUCFGR){
            std::cout << "cpucfgr";
        }
        else if(i == openrisc_arch::REG_EPCR0){
            std::cout << "EPCR0:";
        }
        else{assert(!"Unknown register");}
        std::cout << std::hex << std::setw(8) << std::setfill('0') << get_reg_func(i);
        if((i & 3) != 3) std::cout << "  ";
        else std::cout << '\n';
    }
    std::cout << std::endl;
}

void openrisc_vm::interrupt(int irq_id, bool enable){
    jcpu_assert(irq_id == 0);
    irq_status = enable;
}

void openrisc_vm::reset(){
    irq_status = false;
}


openrisc::openrisc(const char *model) : jcpu(), vm(JCPU_NULLPTR){
}

openrisc::~openrisc(){
    delete vm;
}

void openrisc::interrupt(int irq_id, bool enable){
    if(vm) vm->interrupt(irq_id, enable);
}

void openrisc::reset(bool reset_on){
    if(vm) vm->reset();
}

void openrisc::run(run_option_e opt){
    if(!vm){
        vm = new openrisc_vm(*ext_ifs);
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

uint64_t openrisc::get_total_insn_count()const{
    return vm->get_total_insn_count();
}


} //end of namespace openrisc
} //end of namespace jcpu

