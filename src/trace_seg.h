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
    seg(uint64_t s, uint64_t e): sid(s), eid(e){};
    uint64_t length();
    friend std::istream& operator>>(std::istream& is, seg& tmp);
};
}
#endif //DRAMSIM3_TRACE_SEG_H
