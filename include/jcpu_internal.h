#ifndef JCPU_INTERNAL_H
#define JCPU_INTERNAL_H
#include <iostream>
#include <cstdlib>
#define JCPU_OVERRIDE override
#define JCPU_NULLPTR  nullptr

#define jcpu_assert(cond) do{if(!(cond)){std::cerr << "Assert(" << #cond << ") failed in " << __FILE__ << __LINE__ << std::endl; std::abort();}} while(false)
namespace jcpu{



}
#endif
