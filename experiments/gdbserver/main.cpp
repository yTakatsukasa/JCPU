#include <iostream>
#include <numeric>
#include <cassert>
#include "clx/tcp.h"

int recv(clx::tcp::socket sock);

int main(int argc, char* argv[]) {
    if (argc < 2) std::exit(-1);
    try {
        clx::tcp::acceptor s(clx::tcp::port(argv[1]));

        while (1) {
            clx::tcp::socket clt = s.accept();
            std::cout << clt.address().ipaddr() << ':' << clt.address().port()
                << " Connection was established" << std::endl;

            if (recv(clt) == -1) break;
            else {
                std::cout << clt.address().ipaddr() << ':' << clt.address().port()
                    << " Connection was closed" << std::endl;
                clt.close();
            }
        }
        s.close();
    }
    catch (clx::socket_error& e) {
        std::cerr << e.what() << std::endl;
        std::exit(-1);
    }
    catch (clx::sockaddress_error& e) {
        std::cerr << e.what() << std::endl;
        std::exit(-1);
    }

    return 0;
}

// +$qSupported:multiprocess+;qRelocInsn+#2a$qSupported:multiprocess+;qRelocInsn+#2a$qSupported:multiprocess+;qRelocInsn+#2a$qSupported:multiprocess+;qRelocInsn+#2a---+$Hg0#df$Hg0#df$Hg0#df$Hg0#df-


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
        assert(idx ==0 || idx == 1);
        const char c = (csum >> (4 - 4 * idx)) & 0x0F;
        assert(c < 16);
        return c > 9 ? (c + 'a' - 10) : (c + '0');
    }
    gdb_send_msg & operator << (const char *msg){
        while(msg && *msg)  add(*(msg++));
        return *this;
    }
    unsigned int get_msg_size()const{return msg.size();}
    const unsigned char operator [] (unsigned int idx)const{
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
    explicit gdb_rcv_msg(std::istream &is){
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
    bool check_csum()const{
        const unsigned char csum_act = std::accumulate(msg.begin(), msg.end(), 0);
        return csum_act == csum;
    }
    unsigned int get_msg_size()const{return msg.size();}
    const unsigned char operator [] (unsigned int idx)const{
        return msg.at(idx);
    }
    bool start_with(const char *str, unsigned int offset = 0)const{
        unsigned int idx = 0;
        while(*str){
            if(idx >= msg.size()) return false;
            if(*str != msg[idx]) return false;
            ++str;
            ++idx;
        }
        return true;
    }
};

std::ostream & operator << (std::ostream &os, const gdb_rcv_msg &msg){
    for(unsigned int i = 0, len = msg.get_msg_size(); i < len; ++i){
        os << msg[i];
    }
    return os << (msg.check_csum() ? "CSUM OK" : "CSUM NG");
}


int recv(clx::tcp::socket sock) {
    clx::tcp::sockstream ss(sock);
    for(;;){
        gdb_rcv_msg msg(ss);
        std::cout << "Received msg:" << msg << std::endl;
        gdb_send_msg smsg;
        if(msg.start_with("qSupported")){
            smsg << "PacketSize=ff";
        }
        else if(msg.start_with("Hc")){
           smsg << "OK";
        }
        else if(msg.start_with("?")){
            smsg << "S05";
        }
        else if(msg.start_with("qC")){
            smsg << "QC1";
        }
        else if(msg.start_with("g")){
            for(unsigned int i = 0; i < 32; ++i){
                smsg << "00000000";
            }
        }
        else{
        }
        if(msg.get_msg_size() != 0){
            std::cout << "Send msg:" << smsg << std::endl;
            ss << '+' << smsg;
            ss << '+';
        }
    }
    return 0;
}
