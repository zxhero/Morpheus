//
// Created by zhangxu on 3/25/23.
//

#ifndef DRAMSIM3_TRACE_SEG_H
#define DRAMSIM3_TRACE_SEG_H
#include <fstream>
#include <iostream>
#include <list>
#include <string>
#include <sstream>
#include "common.h"

namespace dramsim3 {
class seg{
    public:
    uint64_t sid;
    uint64_t eid;
    uint64_t time_span;
    uint64_t r_num;
    uint64_t w_num;
    uint64_t kernel_num;
    uint64_t pages_num;
    int pid;
    uint64_t process_num;
    seg(uint64_t s, uint64_t e): sid(s), eid(e), r_num(0), w_num(0){};
    uint64_t length();
    friend std::istream& operator>>(std::istream& is, seg& tmp);
    friend std::ostream& operator<<(std::ostream& os, seg& tmp);
};
}
#endif //DRAMSIM3_TRACE_SEG_H
