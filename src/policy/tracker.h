//
// Created by zhangxu on 5/6/23.
//

#ifndef DRAMSIM3_TRACKER_H
#define DRAMSIM3_TRACKER_H
#include "../cadcache.h"
namespace dramsim3{

class Tracker {
protected:
    FrontEnd *fe;
    uint64_t tag_num;
public:
    Tracker(FrontEnd *fe_);
    ~Tracker(){};
    virtual bool TestHit(Tag &t, uint64_t hex_addr, uint64_t hex_addr_cache) = 0;
    void AddTransaction(uint64_t offset, bool is_write, Tag &t);
    void ResetTag(Tag &t, uint64_t tag);
};

class DirectMapTracker : public Tracker{
public:
    explicit DirectMapTracker(FrontEnd *fe_): Tracker(fe_){};
    ~DirectMapTracker()= default;
    bool TestHit(Tag &t, uint64_t hex_addr, uint64_t hex_addr_cache) override;
};

class KonaTracker : public Tracker{
private:
    const uint64_t index_num;
public:
    KonaTracker(FrontEnd *fe_, uint64_t index_num_);
    ~KonaTracker()= default;
    bool TestHit(Tag &t, uint64_t hex_addr, uint64_t hex_addr_cache) override;
};

class OurTracker : public Tracker{
public:
    explicit OurTracker(FrontEnd *fe_): Tracker(fe_){};
    ~OurTracker()= default;
    bool TestHit(Tag &t, uint64_t hex_addr, uint64_t hex_addr_cache) override;
};
}
#endif //DRAMSIM3_TRACKER_H
