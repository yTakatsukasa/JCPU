#ifndef QCPU_GDBSERVER_H
#define QCPU_GDBSERVER_H
#include <stdint.h>
#include <vector>
namespace qcpu{
namespace gdb{

class gdb_target_if{
    public:
    virtual unsigned int get_reg_width()const = 0;
    virtual void get_reg_value(std::vector<uint64_t> &)const = 0;
    virtual void set_reg_value(unsigned int, uint64_t) = 0;
    virtual void run_continue(bool) = 0;
    virtual uint64_t read_mem_dbg(uint64_t, unsigned int) = 0;
    virtual void write_mem_dbg(uint64_t, unsigned int, uint64_t) = 0;
    virtual void set_unset_break_point(bool, unsigned int, uint64_t) = 0;
    virtual ~gdb_target_if(){}
};


class gdb_server{
    struct impl;
    impl *pimpl;
    public:
    explicit gdb_server(unsigned int);
    ~gdb_server();
    void wait_and_run(gdb_target_if&);
};


}
}
#endif
