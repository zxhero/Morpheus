//
// Created by zhangxu on 3/15/23.
//

#ifndef DRAMSIM3_CADCACHE_H
#define DRAMSIM3_CADCACHE_H

#include <list>
#include "common.h"
#include "configuration.h"
#include "controller.h"
#include "timing.h"
#include "dram_system.h"

namespace dramsim3{

class RemoteRequest {
   public:
    RemoteRequest(){};
    RemoteRequest(bool is_write_, uint64_t hex_addr_, int sz_, uint64_t exit_time_);
    uint64_t hex_addr;
    int sz; //number of 64B flits
    bool is_write;
    // this exit_time is the time to exit ethernet to memory pool
    uint64_t exit_time;
};

class MemoryPool : public JedecDRAMSystem{
    private:
    std::vector<std::pair<uint64_t, bool>> flits_issue;
    std::unordered_map<uint64_t, std::pair<RemoteRequest, int>*> pending_reqs;
    void MediaCallback(uint64_t req_id);
    std::function<void(uint64_t req_id)> read_callback_, write_callback_;

    public:
    MemoryPool(Config &config, const std::string &output_dir,
                    std::function<void(uint64_t)> read_callback,
                    std::function<void(uint64_t)> write_callback);
    ~MemoryPool(){};
    bool WillAcceptTransaction(RemoteRequest req);
    bool AddTransaction(RemoteRequest req);
    void ClockTick() override;

};

const std::unordered_map<std::string, uint64_t> BenchmarkInfo = {
        {"ligra_bfs", 76914688},
        {"memcached", 325341184},
        {"hpcc", 1051770880},
        {"wtdbg", 45912064},
        {"hpcg", 139956224}
};

class Tag {
    uint8_t tag;
    bool valid;
    bool dirty;
public:
    Tag(uint8_t tag_, bool valid_, bool dirty_): tag(tag_), valid(valid_), dirty(dirty_){}
};

class FrontEnd {
    private:
    std::string benchmark_name;
    JedecDRAMSystem *cache_;
    //friend class JedecDRAMSystem;
    std::list<RemoteRequest> LSQ;
    std::list<std::pair<uint64_t, uint64_t>> resp;
    std::vector<Tag> Meta_SRAM;

    public:
    FrontEnd(std::string output_dir, JedecDRAMSystem *cache, Config &config);
    bool GetReq(RemoteRequest &req);
    bool AddTransaction(uint64_t hex_addr, bool is_write);
    bool GetResp(uint64_t &req_id, uint64_t clk);
    void Refill(uint64_t req_id, uint64_t clk);
    void CacheReadCallBack(uint64_t req_id);
    void CacheWriteCallBack(uint64_t req_id);
};

class KONAMethod : public FrontEnd{

};

class cadcache : public JedecDRAMSystem {
    private:
    const unsigned long remote_latency;   //RTT in cycles
    Config* remote_config_;
    MemoryPool remote_memory;
    std::vector<RemoteRequest> ethernet;
    std::vector<std::pair<uint64_t, uint64_t>> write_buffer;
    uint64_t egress_busy_clk;
    FrontEnd *cache_controller;
    std::function<void(uint64_t req_id)> read_callback_, write_callback_;

    void RemoteCallback(uint64_t req_id);
    uint64_t GetData(uint64_t caddr);

    //SRAM tag or in-dram tag guarantees reading tag without penalty
    uint64_t GetTag(uint64_t hex_addr);

    public:
    cadcache(Config &config, const std::string &config_str, const std::string &output_dir,
                    std::function<void(uint64_t)> read_callback,
                    std::function<void(uint64_t)> write_callback);
    ~cadcache(){
        delete remote_config_;
        delete cache_controller;
    };
    bool WillAcceptTransaction(uint64_t hex_addr, bool is_write) const override;
    bool AddTransaction(uint64_t hex_addr, bool is_write) override;
    void ClockTick() override;
    void PrintStats() override;
    void ResetStats() override;
};

}

#endif //DRAMSIM3_CADCACHE_H
