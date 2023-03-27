//
// Created by zhangxu on 3/25/23.
//


#include "trace_seg.h"
namespace dramsim3 {

uint64_t seg::length() {
    return eid - sid + 1;
}

std::istream& operator>>(std::istream& is, seg& tmp){
    std::string line;
    while(getline(is, line)){
        if((line[line.find_first_not_of(" \t\r\n")]) != '#'){
            std::istringstream lstream(line);
            lstream >>tmp.sid>>tmp.eid;
            return is;
        }
    }
    tmp.sid = 0;
    tmp.eid = 0;
    return is;
}
}

using namespace dramsim3;

int main(int argc, const char **argv){
    std::ifstream trace_file_(argv[1]);
    HMTTTransaction tmp;
    std::list<seg> segs;
    uint64_t id = 0, sid = 0;
    uint64_t lasting = 0;
    std::list<uint64_t> times;

    while(!trace_file_.eof()){
        trace_file_>>tmp;
        if(tmp.added_ns > 1000000 && id > 0){
            segs.emplace_back(seg(sid, id - 1));
            times.emplace_back(lasting);
            sid = id;
            lasting = 0;
        }else{
            lasting += tmp.added_ns;
        }

        id++;
    }

    std::cout<<"# "<<argv[1]<<" has "<<segs.size()<<" segments\n";
    std::cout<<"# start id\tend id\tlength\ttime per trace\n";
    auto j = times.begin();
    for (auto i = segs.begin(); i != segs.end(); ++i, ++j) {
        if(i->length() > 1000000)
            std::cout<<i->sid<<"\t"<<i->eid<<"\t"<<i->length()<<"\t"<<1.0 * *j / (i->length())<<"\n";
    }
}