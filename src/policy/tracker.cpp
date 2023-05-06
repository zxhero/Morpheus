//
// Created by zhangxu on 5/6/23.
//

#include "tracker.h"

namespace dramsim3{

Tracker::Tracker(FrontEnd *fe_) {
    fe = fe_;
    tag_num = fe->Meta_SRAM.size();
}

void Tracker::AddTransaction(uint64_t offset, bool is_write, Tag &t) {
    if(t.granularity > 256)
        t.accessed[offset / 256] = true;
    t.dirty |= is_write;
    t.dirty_bits[offset / 64] = t.dirty_bits[offset / 64] | is_write;
}

void Tracker::ResetTag(Tag &t, uint64_t tag) {
    t = Tag(tag, true, false, t.granularity);
}

bool DirectMapTracker::TestHit(Tag &t, uint64_t hex_addr, uint64_t hex_addr_cache) {
    //std::cerr<<"Tracker\n";
    uint64_t hex_addr_b = ((t.tag * tag_num) + (hex_addr_cache / t.granularity));
    return (hex_addr / t.granularity) == hex_addr_b;
}

KonaTracker::KonaTracker(FrontEnd *fe_, uint64_t index_num_): Tracker(fe_), index_num(index_num_) {}

bool KonaTracker::TestHit(Tag &t, uint64_t hex_addr, uint64_t hex_addr_cache) {
    uint64_t hex_addr_b = ((hex_addr_cache / t.granularity / 4) + t.tag * index_num);
    return (hex_addr / t.granularity) == hex_addr_b;
}

bool OurTracker::TestHit(Tag &t, uint64_t hex_addr, uint64_t hex_addr_cache) {
    //std::cerr<<"OurTracker\n";
    return (hex_addr / t.granularity * t.granularity) == t.tag;
}
}