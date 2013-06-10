#include <stdint.h>
#include "../../include/qcpu.h"


qcpu::qcpu_ext_if *mem_access_if;


extern "C" {
void set_mem_access_if(qcpu::qcpu_ext_if *ifs){
    mem_access_if = ifs;
}
uint64_t helper_mem_read(uint64_t addr, unsigned int length){
    return mem_access_if->mem_read(addr, length);
}
void helper_mem_write(uint64_t addr, unsigned int length, uint64_t val){
    mem_access_if->mem_write(addr, length, val);
}
uint64_t helper_mem_read_debug(uint64_t addr, unsigned int length){
    return mem_access_if->mem_read_dbg(addr, length);
}
void helper_mem_write_debug(uint64_t addr, unsigned int length, uint64_t val){
    mem_access_if->mem_write_dbg(addr, length, val);
}

}
