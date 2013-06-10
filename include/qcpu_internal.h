#ifndef QCPU_INTERNAL_H
#define QCPU_INTERNAL_H
#include <iostream>
#include <cstdlib>
#define QCPU_OVERRIDE override
#define QCPU_NULLPTR  nullptr

#define qcpu_assert(cond) do{if(!(cond)){std::cerr << "Assert(" << #cond << ") failed in " << __FILE__ << __LINE__ << std::endl; std::abort();}} while(false)
namespace qcpu{



}
#endif
