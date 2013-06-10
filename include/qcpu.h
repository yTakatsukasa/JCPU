#ifndef QCPU_H
#define QCPU_H
#include <stdint.h>
namespace qcpu{


class qcpu_ext_if{
    public:
    virtual uint64_t mem_read(uint64_t, unsigned int) = 0;
    virtual void mem_write(uint64_t, unsigned int, uint64_t) = 0;
    virtual uint64_t mem_read_dbg(uint64_t, unsigned int) = 0;
    virtual void mem_write_dbg(uint64_t, unsigned int, uint64_t) = 0;
};


class qcpu{
    protected:
    qcpu();
    qcpu_ext_if *ext_ifs;
    public:
    enum run_option_e{
        RUN_OPTION_NORMAL, RUN_OPTION_WATI_GDB
    };
    virtual ~qcpu(){}
    virtual void interrupt(int, bool) = 0;
    virtual void reset(bool) = 0;
    virtual void run(run_option_e) = 0;
    void set_ext_interface(qcpu_ext_if *);
    static qcpu * create(const char *, const char *);
    
};


}

#endif
