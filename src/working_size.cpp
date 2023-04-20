//
// Created by zhangxu on 4/19/23.
//

#include "working_size.h"
//

namespace dramsim3{
bool WorkingSet::operator>(const WorkingSet &a) const {
    return sz > a.sz;
}

std::ostream& operator<<(std::ostream& os, WorkingSet& tmp){
    os<<tmp.sid<<" "<<tmp.eid<<" "<<tmp.sz<<" "<<tmp.time<<"\n";
    return os;
}

std::istream& operator>>(std::istream& is, WorkingSet& tmp){
    is>>tmp.sid>>tmp.eid>>tmp.sz>>tmp.time;
    return is;
}
}