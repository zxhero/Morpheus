//
// Created by zhangxu on 4/1/23.
//

#ifndef DRAMSIM3_DIRECT_MAP_H
#define DRAMSIM3_DIRECT_MAP_H

#include "../cadcache.h"

namespace dramsim3 {

class DirectMap : public FrontEnd {
private:
    const uint64_t latency = 3;
    uint64_t capacity_mask;
    const uint64_t granularity;
    using CacheAddr = uint64_t;

    std::unordered_map<uint64_t, std::list<Transaction>> MSHRs;
    uint64_t MSHR_sz;
    std::unordered_map<CacheAddr, uint64_t> pending_req_to_cache;
    std::list<Transaction> refill_req_to_cache;
    std::vector<CacheAddr> refill_buffer;
    std::unordered_map<CacheAddr, std::pair<RemoteRequest, int>> write_back_buffer;

    uint64_t hit;
    uint64_t wb_hit;
    uint64_t miss;
    SimpleStats::HistoCount line_utility;
    bool hit_and_return(uint64_t index, bool is_write);
    bool miss_and_return();

    bool ProcessOneReq();

public:
    DirectMap(std::string output_dir, JedecDRAMSystem *cache, Config &config);
    ~DirectMap(){};
    void Refill(uint64_t req_id) override;
    void CacheReadCallBack(uint64_t req_id) override;
    void Drained() override;
    void WarmUp(uint64_t hex_addr, bool is_write) override;
    void PrintStat() override;
    void ResetStat() override;
};

}

#endif //DRAMSIM3_DIRECT_MAP_H
