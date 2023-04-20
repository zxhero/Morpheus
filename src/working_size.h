//
// Created by zhangxu on 4/19/23.
//

#ifndef DRAMSIM3_WORKING_SIZE_H
#define DRAMSIM3_WORKING_SIZE_H



#include "trace_seg.h"

namespace dramsim3{

class WorkingSet{
    public:
    uint64_t sid;
    uint64_t eid;
    uint64_t sz;
    uint64_t time;
    WorkingSet(uint64_t s_, uint64_t e_, uint64_t sz_): sid(s_), eid(e_), sz(sz_){};
    bool operator > (const WorkingSet &a) const;
    friend std::ostream& operator<<(std::ostream& os, WorkingSet& tmp);
    friend std::istream& operator>>(std::istream& is, WorkingSet& tmp);
};

}



#endif //DRAMSIM3_WORKING_SIZE_H
