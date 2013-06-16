#ifndef JCPU_GDBSERVER_H
#define JCPU_GDBSERVER_H
#include <stdint.h>
#include <vector>
namespace jcpu{
namespace gdb{

class gdb_target_if{
    public:
    enum run_state_e{RUN_STAT_NORMAL, RUN_STAT_BREAK};
    virtual unsigned int get_reg_width()const = 0;
    virtual void get_reg_value(std::vector<uint64_t> &)const = 0;
    virtual void set_reg_value(unsigned int, uint64_t) = 0;
    virtual run_state_e run_continue(bool) = 0;
    virtual uint64_t read_mem_dbg(uint64_t, unsigned int) = 0;
    virtual void write_mem_dbg(uint64_t, unsigned int, uint64_t) = 0;
    virtual void set_unset_break_point(bool, uint64_t) = 0;
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
