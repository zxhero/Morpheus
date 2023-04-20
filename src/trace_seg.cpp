//
// Created by zhangxu on 3/25/23.
//


#include "trace_seg.h"
#include "trace.h"
#include <iomanip>
namespace dramsim3 {

uint64_t seg::length() {
    return eid - sid + 1;
}

std::istream& operator>>(std::istream& is, seg& tmp){
    std::string line;
    while(getline(is, line)){
        if((line[line.find_first_not_of(" \t\r\n")]) != '#'){
            std::istringstream lstream(line);
            lstream >>tmp.pid>>tmp.process_num
                    >>tmp.sid>>tmp.eid;
            return is;
        }
    }
    tmp.sid = 0;
    tmp.eid = 0;
    return is;
}

std::ostream& operator<<(std::ostream& os, seg& tmp){
    os<<std::setw(15)<<tmp.pid
            <<std::setw(15)<<tmp.process_num
            <<std::setw(15)<<tmp.sid
            <<std::setw(15)<<tmp.eid
            <<std::setw(15)<<tmp.length()
            <<std::setw(15)<<tmp.time_span
            <<std::setw(15)<<tmp.pages_num
            <<std::setw(15)<<tmp.r_num
            <<std::setw(15)<<tmp.w_num
            <<std::setw(15)<<tmp.kernel_num;
    os<<"\n"<<std::flush;
    return os;
}
}