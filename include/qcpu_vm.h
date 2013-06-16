#ifndef QCPU_VM_H
#define QCPU_VM_H

#include <stdint.h>
#include <vector>
#include <utility>
#include <llvm/ExecutionEngine/JIT.h> 
#include "qcpu_internal.h"
namespace llvm{class Function;}

namespace qcpu{
namespace vm{

class qcpu_vm_if{
    public:
    virtual void dump_regs()const = 0;
    virtual ~qcpu_vm_if(){}
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
        qcpu_assert(it != bps_by_addr.end());
        delete it->second;
        bps_by_addr.erase(it);
    }
    const bp_type *find_nearest(virt_addr_t pc)const{
        const typename std::map<virt_addr_t, bp_type *>::const_iterator it = bps_by_addr.lower_bound(pc);
        return it == bps_by_addr.end() ? QCPU_NULLPTR : it->second;
    }
};



} //end of namespace vm
} //end of namespace qcpu
#endif
