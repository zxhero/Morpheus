//
// Created by zhangxu on 4/21/23.
//

#ifndef DRAMSIM3_CACHE_FRONTEND_H
#define DRAMSIM3_CACHE_FRONTEND_H

#include "../cadcache.h"
namespace dramsim3 {

using CacheAddr = uint64_t;

class CacheFrontEnd : public FrontEnd {
private:
    const uint64_t latency = 3;
    std::unordered_map<CacheAddr, uint64_t> pending_req_to_cache;
    std::list<Transaction> refill_req_to_cache;
    std::vector<CacheAddr> refill_buffer;
    std::unordered_map<CacheAddr, std::pair<Tag, int>> write_back_buffer;

    uint64_t hit;
    uint64_t wb_hit;
    uint64_t miss;
    SimpleStats::HistoCount line_utility;
    SimpleStats::HistoCount mshr_waiting;
    bool miss_and_return();
    bool hit_and_return(Tag &tag_, uint64_t hex_addr, bool is_write);

protected:
    const uint64_t granularity;
    const uint64_t granularity_mask;
    std::ofstream utilization_file;
    std::unordered_map<uint64_t, std::list<Transaction>> MSHRs;
    uint64_t MSHR_sz;

    virtual bool GetTag(uint64_t hex_addr, Tag *&tag_, uint64_t &hex_addr_cache) = 0;
    virtual uint64_t GetHexTag(uint64_t hex_addr) = 0;
    virtual uint64_t GetHexAddr(uint64_t hex_tag, uint64_t hex_addr_cache) = 0;
    virtual uint64_t AllocCPage(uint64_t hex_addr, Tag *&tag_) = 0;
    virtual void MissHandler(uint64_t hex_addr, bool is_write) = 0;
    virtual void WriteBackData(Tag tag_, uint64_t hex_addr_cache) = 0;
    virtual void HashReadCallBack(uint64_t req_id) = 0;
    void DoRefill(uint64_t req_id, Tag &t, uint64_t hex_addr_cache);
    bool ProcessOneReq(uint64_t hex_addr, bool is_write, Tag *t, uint64_t hex_addr_cache);
    void ProcessRefillReq();

public:
    CacheFrontEnd(std::string output_dir, JedecDRAMSystem *cache, Config &config);
    ~CacheFrontEnd(){utilization_file.close();};
    void Refill(uint64_t req_id) override;
    void CacheReadCallBack(uint64_t req_id) override;
    void Drained() override;
    void WarmUp(uint64_t hex_addr, bool is_write) override;
    void PrintStat() override;
    void ResetStat() override;
};

}

#endif //DRAMSIM3_CACHE_FRONTEND_H
