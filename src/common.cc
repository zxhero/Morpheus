#include "common.h"
#include "fmt/format.h"
#include <sstream>
#include <unordered_set>
#include <sys/stat.h>

namespace dramsim3 {

std::ostream& operator<<(std::ostream& os, const Command& cmd) {
    std::vector<std::string> command_string = {
        "read",
        "read_p",
        "write",
        "write_p",
        "activate",
        "precharge",
        "refresh_bank",  // verilog model doesn't distinguish bank/rank refresh
        "refresh",
        "self_refresh_enter",
        "self_refresh_exit",
        "WRONG"};
    os << fmt::format("{:<20} {:>3} {:>3} {:>3} {:>3} {:>#8x} {:>#8x}",
                      command_string[static_cast<int>(cmd.cmd_type)],
                      cmd.Channel(), cmd.Rank(), cmd.Bankgroup(), cmd.Bank(),
                      cmd.Row(), cmd.Column());
    return os;
}

std::ostream& operator<<(std::ostream& os, const Transaction& trans) {
    const std::string trans_type = trans.is_write ? "WRITE" : "READ";
    os << fmt::format("{:<30} {:>8}", trans.addr, trans_type);
    return os;
}

std::istream& operator>>(std::istream& is, Transaction& trans) {
    std::unordered_set<std::string> write_types = {"WRITE", "write", "P_MEM_WR",
                                                   "BOFF"};
    std::string mem_op;
    is >> std::hex >> trans.addr >> mem_op >> std::dec >> trans.added_cycle;
    trans.is_write = write_types.count(mem_op) == 1;
    return is;
}

std::ostream& operator<<(std::ostream& os, const HMTTTransaction& trans){
    const std::string trans_type = trans.r_w ? "WRITE" : "READ";
    os << fmt::format("{:<30} {:>8}", trans.addr, trans_type);
    return os;
}

std::istream& operator>>(std::istream& is, HMTTTransaction& trans){
    uint64_t buffer = 0;
    uint64_t invalid_count = 0;

    while(1){
        is.read(reinterpret_cast<char*>(&buffer), 6);
        trans.seq_no = (unsigned int) ((buffer >> 40) & 0xffU);
        unsigned long long timer  = (unsigned long long)((buffer >> 32) & 0xffULL);
        trans.r_w    = (unsigned int)((buffer >> 31) & 0x1U);
        trans.addr   = (unsigned long)(buffer & 0x7fffffffUL);
        trans.addr   = (unsigned long)(trans.addr << 6);
        //print(tmp);
        if (trans.addr == 0 && timer == 0) {
            invalid_count++;
        }
        else {
            timer += invalid_count * 256;
            trans.added_ns = timer * 5; //200MHz
            trans.valid = true;
            if(trans.addr >= (2ULL << 30)) {
                trans.addr += (2ULL << 30);
                trans.is_kernel = false;
            }else{
                trans.is_kernel = true;
            }
            //::cout<<trans.added_ns<<" "<<std::hex<<trans.addr<<(trans.is_kernel ? " kernel" : " app")
            //<<(trans.r_w ? " Read": " Write")<<"\n"<<std::dec;
            return is;
        }

        if(is.eof()){
            trans.valid = false;
            return is;
        }
    }
}

int GetBitInPos(uint64_t bits, int pos) {
    // given a uint64_t value get the binary value of pos-th bit
    // from MSB to LSB indexed as 63 - 0
    return (bits >> pos) & 1;
}

int LogBase2(int power_of_two) {
    int i = 0;
    while (power_of_two > 1) {
        power_of_two /= 2;
        i++;
    }
    return i;
}

std::vector<std::string> StringSplit(const std::string& s, char delim) {
    std::vector<std::string> elems;
    StringSplit(s, delim, std::back_inserter(elems));
    return elems;
}

template <typename Out>
void StringSplit(const std::string& s, char delim, Out result) {
    std::stringstream ss;
    ss.str(s);
    std::string item;
    while (std::getline(ss, item, delim)) {
        if (!item.empty()) {
            *(result++) = item;
        }
    }
}

void AbruptExit(const std::string& file, int line) {
    std::cerr << "Exiting Abruptly - " << file << ":" << line << std::endl;
    std::exit(-1);
}

bool DirExist(std::string dir) {
    // courtesy to stackoverflow
    struct stat info;
    if (stat(dir.c_str(), &info) != 0) {
        return false;
    } else if (info.st_mode & S_IFDIR) {
        return true;
    } else {  // exists but is file
        return false;
    }
}

}  // namespace dramsim3
