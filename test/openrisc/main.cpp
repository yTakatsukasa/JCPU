#include <iostream>
#include <memory> //unique_ptr
#include <llvm/Support/Debug.h> //EnableDebugBuffering
#include <llvm/Support/raw_ostream.h> //outs()
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/PrettyStackTrace.h>
#include <llvm/Support/Signals.h>// llvm::sys::PrintStackTraceOnErrorSignal()

#include "jcpu.h"
#include "elfio/elfio_dump.hpp"

#if __cplusplus >= 201103L
#define RISCV_OVERRIDE override
#define RISCV_NULLPTR  nullptr
#else
#define RISCV_OVERRIDE 
#define RISCV_NULLPTR  NULL
#endif

uint32_t
swap_endian(uint32_t v)
{
    union {
        uint8_t u[4];
        uint32_t v;
    } uni;
    uni.v = v;
    std::swap(uni.u[0], uni.u[3]);
    std::swap(uni.u[1], uni.u[2]);
    return uni.v;
}

class dummy_mem : public jcpu::jcpu_ext_if{
    const jcpu::jcpu &jcpu_if;
    std::vector<uint32_t> tmp_mem;
    bool prefix_need_to_show;

    virtual uint64_t mem_read(uint64_t addr, unsigned int size)RISCV_OVERRIDE;
    virtual void mem_write(uint64_t addr, unsigned int size, uint64_t val)RISCV_OVERRIDE;
    virtual uint64_t mem_read_dbg(uint64_t addr, unsigned int size) RISCV_OVERRIDE {
        return mem_read(addr, size);
    }
    virtual void mem_write_dbg(uint64_t addr, unsigned int size, uint64_t val)RISCV_OVERRIDE {
        mem_write(addr, size, val);
    }
    public:
    dummy_mem(const char *fn, jcpu::jcpu &ifs);
};

dummy_mem::dummy_mem(const char *fn, jcpu::jcpu &ifs) : jcpu_if(ifs) {
    prefix_need_to_show = true;

    ELFIO::elfio reader;

    if ( !reader.load( fn ) ) {
        std::cout << "File " << fn << " is not found or it is not an ELF file\n" << std::endl;
        abort();
    }
    tmp_mem.resize(0x20000 / sizeof(tmp_mem.front()));

    //for( auto s : reader.sections ) {
    for(std::vector<ELFIO::section*>::const_iterator it = reader.sections.begin(), it_end = reader.sections.end(); it != it_end; ++it){
        const ELFIO::section *const s = *it;

        if(s->get_name().substr(0, 5) != ".text" &&
                s->get_name().substr(0, 7) != ".rodata" &&
                s->get_name().substr(0, 4) != ".bss" &&
                s->get_name().substr(0, 5) != ".data"
                ) continue;

        if(s->get_address() + s->get_size() >= tmp_mem.size())
            tmp_mem.resize(s->get_address() + s->get_size());
        if(s->get_data()) {
            std::cout << "Loading " << s->get_name() << " from " << s->get_address() << " len:" << s->get_size() << std::endl;
            std::memcpy(reinterpret_cast<char *>(tmp_mem.data()) + s->get_address(), s->get_data(), s->get_size());
        }
        else {
            std::cout << "Clearing " << s->get_name() << " from " << s->get_address() << " len:" << s->get_size() << std::endl;
            std::memset(reinterpret_cast<char *>(tmp_mem.data()) + s->get_address(), s->get_size(), 0);
        }
    }
    
    /*
    ELFIO::dump::header         ( std::cout, reader );
    ELFIO::dump::section_headers( std::cout, reader );
    ELFIO::dump::segment_headers( std::cout, reader );
    ELFIO::dump::symbol_tables  ( std::cout, reader );
    ELFIO::dump::notes          ( std::cout, reader );
    ELFIO::dump::dynamic_tags   ( std::cout, reader );
    ELFIO::dump::section_datas  ( std::cout, reader );
    ELFIO::dump::segment_datas  ( std::cout, reader );
    */

}

uint64_t dummy_mem::mem_read(uint64_t addr, unsigned int size){
    if(addr >= tmp_mem.size() * sizeof(tmp_mem[0])){
        std::cerr << "Address " << std::hex << addr << " is out of bound" << std::endl;
        if(addr == 0xFFFFFFFF){//GDB?
            return 0;
        }
        else{
            abort();
        }
    }
    uint64_t v = tmp_mem[addr / 4];
    if(size == 1){
        switch(addr % 4){
            case 3: v >>= 24; break;
            case 2: v >>= 16; break;
            case 1: v >>=  8; break;
            case 0: v >>=  0; break;
        }
        v &= 0xFF;
        //std::cout << "Load 1byte from "<< addr << " " << std::hex << v << " ('" << (char)v << ")" << std::endl;
    }
    else if(size == 2){
        if(addr % 2){std::cerr << "Not aligned" << std::endl; abort();}
        switch(addr / 2){
            case 0: v >>= 16; break;
            case 2: v >>=  0; break;
            default:abort();
        }
        v &= 0xFFFF;
    }
    else if(size == 4)
    {
        v = swap_endian(v);
    }
    else if(size == 8){
        v = 0;
        for(int i = 0; i < 8; ++i){
            v <<= 8;
            v |= mem_read(addr + i, 1);
        }
    }
    return v;
}

void dummy_mem::mem_write(uint64_t addr, unsigned int size, uint64_t val){
    if(addr < tmp_mem.size() * sizeof(tmp_mem[0])){
        if(size == 4){
            tmp_mem[addr / 4] = swap_endian(val);
        }
        else if(size == 1){
            val <<= ((addr % 4)) * 8;
            tmp_mem[addr / 4] &= 0xFFFFFFFF ^ (0xFF << ((addr % 4) * 8));
            tmp_mem[addr / 4] |= val;
        }
        //else if(size == 2){
        //}
        else{
            std::cerr << "Write for Address " << std::hex << addr << " with size:" << size << " is not supported" << std::endl;
            abort();
        }
    }
    else if(addr == 0x60000004){
        //assert(be == 0x8);
        const char c = val;
        if(prefix_need_to_show)
            std::cerr << "UTIL:" << c << std::flush;
        else
            std::cerr << c << std::flush;
        prefix_need_to_show = c == '\n';
    }
    else if(addr == 0x60000008){
        //assert(be == 0xF);
        //throw finish_ex();
        std::cerr << "Simulation done after " << std::dec << jcpu_if.get_total_insn_count() << " instruction" << std::endl;
        exit(0);
    }
    else{
        std::cerr << "Address " << std::hex << addr << " is out of bound" << std::endl;
        abort();
    }
}




int main( int argc, char** argv )
{
    if ( argc != 2 ) {
        std::cerr << "Usage: main.x <file_name>\n";
        return 1;
    }

    jcpu::jcpu::initialize();

    llvm::sys::PrintStackTraceOnErrorSignal();
    llvm::PrettyStackTraceProgram X(argc, argv);
    llvm::EnableDebugBuffering = true;

#if __cplusplus >= 201103L
    std::unique_ptr<jcpu::jcpu> riscv(jcpu::jcpu::create("openrisc", "or1200"));
#else
    std::auto_ptr<jcpu::jcpu> riscv(jcpu::jcpu::create("openrisc", "or1200"));
#endif
    dummy_mem mem(argv[1], *riscv);
    riscv->set_ext_interface(&mem);
    riscv->reset(true);
    riscv->reset(false);
    riscv->run(riscv->RUN_OPTION_NORMAL);
    //riscv->run(riscv->RUN_OPTION_WATI_GDB);

    return 0;
}
