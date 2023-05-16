//
// Created by zhangxu on 4/1/23.
//

#ifndef DRAMSIM3_DIRECT_MAP_H
#define DRAMSIM3_DIRECT_MAP_H

#include "cache_frontend.h"

namespace dramsim3 {

using CacheAddr = uint64_t;

class DirectMap : public CacheFrontEnd {
private:
    uint64_t capacity_mask;
    const uint64_t granularity;
protected:

    bool GetTag(uint64_t hex_addr, Tag *&tag_, uint64_t &hex_addr_cache) override;
    uint64_t GetHexTag(uint64_t hex_addr) override;
    uint64_t AllocCPage(uint64_t hex_addr, Tag *&tag_) override;
    void MissHandler(uint64_t hex_addr, bool is_write) override;
    void WriteBackData(Tag tag_, uint64_t hex_addr_cache) override;
    void HashReadCallBack(uint64_t req_id) override;
    bool CheckOtherBuffer(uint64_t hex_addr, bool is_write) override{return true;};


public:
    DirectMap(std::string output_dir, JedecDRAMSystem *cache, Config &config);
    ~DirectMap(){};
};

}

#endif //DRAMSIM3_DIRECT_MAP_H
