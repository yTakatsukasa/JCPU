#include <iostream>
#include <cstdio> //fopen, fread
#include <llvm/Support/Debug.h> //EnableDebugBuffering
#include <llvm/Support/raw_ostream.h> //outs()
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/PrettyStackTrace.h>
#include <llvm/Support/Signals.h>// llvm::sys::PrintStackTraceOnErrorSignal()
#include "jcpu.h"


//#include "bubblesort.h"
//#include "bubblesort100.h"
//#include "bubblesort100_check.h"
#if __cplusplus >= 201103L
#define MAIN_CPP_OVERRIDE override
#else
#define MAIN_CPP_OVERRIDE
#endif

class dummy_mem : public jcpu::jcpu_ext_if{
    const jcpu::jcpu &jcpu_if;
    std::vector<uint32_t> tmp_mem;
    bool prefix_need_to_show;
    public:
    dummy_mem(const char *fn, jcpu::jcpu &ifs) : jcpu_if(ifs){
        prefix_need_to_show = true;
        tmp_mem.resize(0x10000 / sizeof(tmp_mem[0]));
#if 0
        std::memcpy(&tmp_mem.front(), bubblesort_prog, sizeof(bubblesort_prog));
#else
        FILE *const fp = std::fopen(fn, "r");
        const size_t copy_unit = 4096;
        for(size_t copied = 0; tmp_mem.size() > copied + copy_unit &&  std::fread(&tmp_mem[copied / sizeof(uint32_t)], copy_unit, 1, fp) == 1; copied += copy_unit){}
        std::fclose(fp);
#endif
    }
    virtual uint64_t mem_read(uint64_t addr, unsigned int size)MAIN_CPP_OVERRIDE{
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
                case 0: v >>= 24; break;
                case 1: v >>= 16; break;
                case 2: v >>=  8; break;
                case 3: v >>=  0; break;
            }
            v &= 0xFF;
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
        else if(size == 8){
            v = 0;
            for(int i = 0; i < 8; ++i){
                v <<= 8;
                v |= mem_read(addr + i, 1);
            }
        }
#if 0
        std::cout << "mem_read(" << std::hex << addr << ", 0x" << size << ")" << " " << v << std::endl;
#endif
        return v;
    }
    virtual void mem_write(uint64_t addr, unsigned int size, uint64_t val)MAIN_CPP_OVERRIDE{
#if 0
        std::cout << "mem_write(" << std::hex << addr
            << ", 0x" << size
            << ", 0x" << val
            << ")" << std::endl;
#endif
        if(addr < tmp_mem.size() * sizeof(tmp_mem[0])){
            if(size == 4){
                tmp_mem[addr / 4] = val;
            }
            else if(size == 1){
                val <<= (3 - (addr % 4)) * 8;
                tmp_mem[addr / 4] &= 0xFFFFFFFF ^ (0xFF << (24 - (addr % 4) * 8));
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
    virtual uint64_t mem_read_dbg(uint64_t addr, unsigned int size) MAIN_CPP_OVERRIDE{
        return mem_read(addr, size);
    }
    virtual void mem_write_dbg(uint64_t addr, unsigned int size, uint64_t val)MAIN_CPP_OVERRIDE{
        mem_write(addr, size, val);
    }
};




int main(int argc, char *argv[]){

    if(argc < 2){
        std::cerr << "Specify the binary for openrisc" << std::endl;
        return -1;
    }

    llvm::InitializeNativeTarget();
    llvm::sys::PrintStackTraceOnErrorSignal();
    llvm::PrettyStackTraceProgram X(argc, argv);

    llvm::EnableDebugBuffering = true;


    jcpu::jcpu *or1200 = jcpu::jcpu::create("openrisc", "or1200");
    dummy_mem mem(argv[1], *or1200);
    or1200->set_ext_interface(&mem);
    or1200->reset(true);
    or1200->reset(false);
    or1200->run(or1200->RUN_OPTION_NORMAL);
    //or1200->run(or1200->RUN_OPTION_WATI_GDB);

    delete or1200;

    return 0;
}


