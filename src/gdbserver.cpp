#include <numeric>
#include <cstdio>
#include "clx/tcp.h"
#include "jcpu_internal.h"
#include "gdbserver.h"

//#define JCPU_GDBSERVER_DEBUG 1

namespace{

class gdb_send_msg{
    std::vector<unsigned char> msg;
    unsigned char csum;
    public:
    gdb_send_msg() : csum(0){}
    void add(unsigned char m){
        if(m == '#' || m == '$' || m == '}'){
            msg.push_back('}');
            m ^= 0x20;
        }
        msg.push_back(m);
        csum += m;
    }
    char get_csum(int idx)const{
        jcpu_assert(idx ==0 || idx == 1);
        const char c = (csum >> (4 - 4 * idx)) & 0x0F;
        jcpu_assert(c < 16);
        return c > 9 ? (c + 'a' - 10) : (c + '0');
    }
    gdb_send_msg & operator << (const char *msg){
        while(msg && *msg)  add(*(msg++));
        return *this;
    }
    unsigned int get_msg_size()const{return msg.size();}
    unsigned char operator [] (unsigned int idx)const{
        return msg.at(idx);
    }
};

std::ostream & operator << (std::ostream &os, const gdb_send_msg &msg){
    os << '$';
    for(unsigned int i = 0, size = msg.get_msg_size(); i < size; ++i){
        os << msg[i];
    }
    return os << '#' << msg.get_csum(0) << msg.get_csum(1);
}



class gdb_rcv_msg{
    std::vector<unsigned char> msg;
    unsigned char csum;
    public:
    explicit gdb_rcv_msg(std::istream &is);
    bool check_csum()const;
    unsigned int get_msg_size()const;
    const unsigned char & operator [] (unsigned int idx)const;
    bool start_with(const char *str, unsigned int offset = 0)const;
    std::vector<std::string> split()const;
};

gdb_rcv_msg::gdb_rcv_msg(std::istream &is){
    csum = 0;
    enum {ST_IDLE, ST_HEAD, ST_BODY, ST_CSUM0, ST_CSUM1, ST_DONE} state;
    for(state = ST_IDLE; state != ST_DONE;){
        const int c = is.get();
        if(c < 0) break;
        if(state == ST_BODY){
            if(c == '}'){
                int real_c = is.get();
                if(real_c < 0) break;
                msg.push_back(real_c ^ 0x20);
            }
            else{
                if(c == '#') state = ST_CSUM0;
                else msg.push_back(c);
            }
        }
        else if(state == ST_CSUM0 || state == ST_CSUM1){
            if('0' <= c && c <= '9'){csum <<= 4; csum |= c - '0';}
            else if('A' <= c && c <= 'F'){csum <<= 4; csum |= c - 'A' + 10;}
            else if('a' <= c && c <= 'f'){csum <<= 4; csum |= c - 'a' + 10;}
            else{
                std::cerr << "ERROR unexpected char:" << static_cast<char>(c) << "  for CRC field" << std::endl;
                abort();
            }
            state = state == ST_CSUM0 ? ST_CSUM1 : ST_DONE;
        }
        else{
            switch(c){
                case '+'://ACK
                    state = ST_IDLE;
                    break;
                case '$'://start of packet
                    msg.clear();
                    //msg.push_back(c);
                    state = ST_BODY;
                    break;
                case '-'://NACK
                    state = ST_DONE;
                    break;
                default:
                    std::cerr << "ERROR unexpected char '" << static_cast<char>(c) << "' (" << std::hex << c << ") received" << std::endl;
                    abort();
                    break;
            }
        }
    }
}

bool gdb_rcv_msg::check_csum()const{
    const unsigned char csum_act = std::accumulate(msg.begin(), msg.end(), 0);
    return csum_act == csum;
}

unsigned int gdb_rcv_msg::get_msg_size()const{return msg.size();}

const unsigned char & gdb_rcv_msg::operator [] (unsigned int idx)const{
    return msg.at(idx);
}

bool gdb_rcv_msg::start_with(const char *str, unsigned int offset)const{
    unsigned int idx = 0;
    while(*str){
        if(idx >= msg.size()) return false;
        if(*str != msg[idx]) return false;
        ++str;
        ++idx;
    }
    return true;
}

std::vector<std::string> gdb_rcv_msg::split()const{
    std::vector<std::string> ret;
    unsigned int start_point = 0;
    for(unsigned int i = 0; i < msg.size(); ++i){
        if(msg[i] == ','){
            ret.push_back(std::string(static_cast<const char *>(static_cast<const void *>(&msg[start_point])), i - start_point));
            start_point = i + 1;
        }
        else if(i == msg.size() - 1){
            ret.push_back(std::string(static_cast<const char *>(static_cast<const void *>(&msg[start_point])), i - start_point + 1));
        }
        if(!ret.empty()){
            //std::cerr << "Tok[" << ret.size() - 1 << "]:" << ret.back() << std::endl;
        }
    }
    return ret;
}

#ifdef JCPU_GDBSERVER_DEBUG
std::ostream & operator << (std::ostream &os, const gdb_rcv_msg &msg){
    for(unsigned int i = 0, len = msg.get_msg_size(); i < len; ++i){
        os << msg[i];
    }
    return os << ' ' << (msg.check_csum() ? "CSUM OK" : "CSUM NG");
}
#endif


} //end of unnamed namespace


namespace jcpu{
namespace gdb{

struct gdb_server::impl{
    const unsigned int port;
    explicit impl(unsigned int);
    void wait_and_run(gdb_target_if &, unsigned int);
};

gdb_server::impl::impl(unsigned int port) : port(port)
{
}

void gdb_server::impl::wait_and_run(gdb_target_if &tgt, unsigned int port_num){
    std::cerr << "Listening at localhost:" << std::dec << port_num << std::endl;
    char buf[32];
    std::sprintf(buf, "%d", port_num);
    clx::tcp::acceptor s(clx::tcp::port(buf));
    clx::tcp::socket clt = s.accept();
    clx::tcp::sockstream ss(clt);
    std::cerr << clt.address().ipaddr() << ':' << clt.address().port()
        << " Connection was established" << std::endl;
    try {
        while (1) {
            gdb_rcv_msg msg(ss);
#if defined(JCPU_GDBSERVER_DEBUG) && JCPU_GDBSERVER_DEBUG > 0
            std::cout << "Received msg:" << msg << std::endl;
#endif
            gdb_send_msg smsg;
            if(msg.start_with("qSupported")){
                smsg << "PacketSize=ff";
            }
            else if(msg.start_with("qC")){
                smsg << "QC1";
            }
            else if(msg.start_with("qOffsets")){
                smsg << "Text=0;Data=0;Bss=0;";
            }
            else if(msg.start_with("Hc")){
                smsg << "OK";
            }
            else if(msg.start_with("Hg")){
                smsg << "OK";
            }
            else if(msg.start_with("?")){
                smsg << "S05";
            }
            else if(msg.start_with("c")){//continue
                smsg << "";
                const gdb_target_if::run_state_e stat = tgt.run_continue(false);
                if(stat == gdb_target_if::RUN_STAT_BREAK){
#if defined(JCPU_GDBSERVER_DEBUG) && JCPU_GDBSERVER_DEBUG > 0
                    std::cerr << "Break point" << std::endl;
#endif
                    smsg << "S05";
                }
            }
            else if(msg.start_with("s")){//step
                const gdb_target_if::run_state_e stat = tgt.run_continue(true);
                smsg << "S05";
#if defined(JCPU_GDBSERVER_DEBUG) && JCPU_GDBSERVER_DEBUG > 0
                if(stat == gdb_target_if::RUN_STAT_BREAK){
                    std::cerr << "Break point" << std::endl;
                }
#else
                static_cast<void>(stat); //suppress compiler warning
#endif
            }
            else if(msg.start_with("m")){ //mem read
                const std::vector<std::string> toks = msg.split();
                jcpu_assert(toks.size() >= 2);
                const uint64_t addr = std::strtoll(toks[0].c_str() + 1, JCPU_NULLPTR, 16);
                const unsigned int len = std::strtol(toks[1].c_str(), JCPU_NULLPTR, 16);
                const uint64_t val = tgt.read_mem_dbg(addr, len);

                std::stringstream ss;
                ss << std::hex << std::setw(len * 2) << std::setfill('0') << val;
                smsg << ss.str().c_str();
            }
            else if(msg.start_with("Z") || msg.start_with("z")){//set or unset breakpoints
                const std::vector<std::string> toks = msg.split();
                jcpu_assert(toks.size() >= 3);
                const unsigned int break_point_id = std::strtol(toks[0].c_str() + 1, JCPU_NULLPTR, 16);
                const uint64_t addr = std::strtoll(toks[1].c_str(), JCPU_NULLPTR, 16);
                const bool set_bp = toks[0][0] == 'Z';
                tgt.set_unset_break_point(set_bp, addr);
                smsg << "OK";
#if defined(JCPU_GDBSERVER_DEBUG) && JCPU_GDBSERVER_DEBUG > 0
                std::cerr << (set_bp ? "Set " : "Unset ") << "Break point " << break_point_id
                    << " at " << std::hex << addr << std::endl;
#else
                static_cast<void>(break_point_id);//suppress compiler warning
#endif
            }
            else if(msg.start_with("g")){//registers
                const unsigned int reg_width = tgt.get_reg_width();
                std::vector<uint64_t> regs;
                tgt.get_reg_value(regs);
                for(unsigned int i = 0; i < regs.size(); ++i){
                    std::stringstream ss;
                    ss << std::hex << std::setw(reg_width / 4) << std::setfill('0') << regs[i];
                    smsg << ss.str().c_str();
                }
            }
            else{
                jcpu_assert(!"Not supported command");
            }
#if defined(JCPU_GDBSERVER_DEBUG) && JCPU_GDBSERVER_DEBUG > 0
            std::cout << "Send msg:" << smsg << std::endl;
#endif
            ss << '+' << smsg;
            ss << '+' << std::flush;
        }
    }
    catch (clx::socket_error& e) {
        std::cerr << e.what() << std::endl;
        std::exit(-1);
    }
    catch (clx::sockaddress_error& e) {
        std::cerr << e.what() << std::endl;
        std::exit(-1);
    }
    s.close();
    std::cout << clt.address().ipaddr() << ':' << clt.address().port()
        << " Connection was closed" << std::endl;
    clt.close();

}


gdb_server::gdb_server(unsigned int port){
    pimpl = new impl(port);
}

gdb_server::~gdb_server(){
    delete pimpl;
}

void gdb_server::wait_and_run(gdb_target_if &tgt){
    for(int i = 0; i < 10; ++i){
        try{
            pimpl->wait_and_run(tgt, i + pimpl->port);
        }
        catch(const clx::system_error &){
            std::cerr << "Port:" << std::dec << (i + pimpl->port) << " is used, try next" << std::endl;
            continue;
        }
        break;
    }
}

} //end of namespace gdbserver
} //end of namespace jcpu
