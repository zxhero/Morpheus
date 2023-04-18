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

template<class pktType>
class Ethernet{
  private:
    struct pkt{
        pktType data;
        uint64_t sz;
        uint64_t exit_time;
    };
    const uint64_t remote_latency;   //RTT in cycles
    const uint64_t bandwidth;       //cycles pre 64B
    std::list<struct pkt> ethernet;
    uint64_t egress_busy_clk;

  public:
    Ethernet(Config &config);
    bool AddTransaction(pktType req, uint64_t clk, uint64_t req_sz);
    bool GetReq(pktType &req, uint64_t clk);
    void PrintStat();
};

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
    std::list<std::pair<std::pair<RemoteRequest, int>*, uint64_t>> flits_issue;
    std::unordered_map<uint64_t, std::pair<RemoteRequest, int>*> pending_reqs;
    void MediaCallback(uint64_t req_id);
    std::function<void(uint64_t req_id)> read_callback_, write_callback_;
    Ethernet<uint64_t> egress_link;

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
        {"hpcg", 139956224},
        {"ligra_pagerank", (uint64_t)1557642 *4096},
        {"imagenet", 9614663680},
        {"nightmare", (uint64_t)2030002*4096},
        {"cg", (uint64_t)2851610*4096},
        {"bt", (uint64_t)2797946*4096},
        {"mg", (uint64_t)873017*4096},
        {"mix1", (uint64_t)2215324*4096},
        {"sp", (uint64_t)2929664*4096},
        {"ua", (uint64_t)1909247*4096},
        {"ft", (uint64_t)2628437*4096},
        {"is", (uint64_t)2105643*4096},
        {"lu", (uint64_t)3037122*4096}
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
    const uint64_t queue_capacity = 64;
    std::string benchmark_name;
    JedecDRAMSystem *cache_;
    //friend class JedecDRAMSystem;
    std::list<RemoteRequest> LSQ;
    std::list<std::pair<uint64_t, uint64_t>> resp;
    std::vector<Tag> Meta_SRAM;
    std::list<std::pair<uint64_t, bool>> front_q;

    public:
    FrontEnd(std::string output_dir, JedecDRAMSystem *cache, Config &config);
    bool GetReq(RemoteRequest &req);
    bool AddTransaction(uint64_t hex_addr, bool is_write);
    bool WillAcceptTransaction(uint64_t hex_addr, bool is_write) const;
    bool GetResp(uint64_t &req_id);
    void Refill(uint64_t req_id);
    void CacheReadCallBack(uint64_t req_id);
    void CacheWriteCallBack(uint64_t req_id);
    void Drained();
    void WarmUp(uint64_t hex_addr, bool is_write);
};

class KONAMethod : public FrontEnd{

};

class cadcache : public JedecDRAMSystem {
    private:
    Config* remote_config_;
    MemoryPool remote_memory;
    std::list<std::pair<uint64_t, uint64_t>> write_buffer;
    FrontEnd *cache_controller;
    std::function<void(uint64_t req_id)> read_callback_, write_callback_;
    RemoteRequest req_;
    Ethernet<RemoteRequest> egress_link;

    void RemoteCallback(uint64_t req_id);

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
    void WarmUp(uint64_t hex_addr, bool is_write);
};

}

#endif //DRAMSIM3_CADCACHE_H
