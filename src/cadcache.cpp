//
// Created by zhangxu on 3/15/23.
//

#include "cadcache.h"
#include "policy/direct_map.h"
#include "policy/kona.h"
#include "policy/our.h"

namespace dramsim3{

template<class pktType>
Ethernet<pktType>::Ethernet(Config &config):
        remote_latency(static_cast<unsigned long>((double)config.rtt / config.tCK)),
        bandwidth(512 / config.bw / config.tCK){
    egress_busy_clk = 0;
}

template<class pktType>
bool Ethernet<pktType>::AddTransaction(pktType req, uint64_t clk, uint64_t req_sz) {
    if(egress_busy_clk < clk){
        egress_busy_clk = clk;
    }

    {
        uint64_t exit_time = egress_busy_clk + remote_latency + req_sz * bandwidth;
        struct pkt tmp{.data = req, .sz = req_sz, .exit_time=exit_time};
        ethernet.push_back(tmp);
        egress_busy_clk += (req_sz * bandwidth);
        return true;
    }

    return false;
}

template<class pktType>
bool Ethernet<pktType>::GetReq(pktType &req, uint64_t clk) {
    if(! ethernet.empty() && ethernet.front().exit_time <= clk){
        req = ethernet.front().data;
        ethernet.erase(ethernet.begin());
        return true;
    }
    return false;
}

template<class pktType>
void Ethernet<pktType>::PrintStat() {}

RemoteRequest::RemoteRequest(bool is_write_, uint64_t hex_addr_, int sz_, uint64_t exit_time_) {
    is_write = is_write_;
    hex_addr = hex_addr_;
    sz = sz_;
    exit_time = exit_time_;
}

Tag::Tag(uint64_t tag_, bool valid_, bool dirty_, uint64_t granularity)
: tag(tag_), valid(valid_), dirty(dirty_){
    accessed.resize(granularity/256, false);
    dirty_bits.resize(granularity/64, false);
}

int Tag::utilized() {
    return std::count(accessed.begin(), accessed.end(), true);
}

FrontEnd::FrontEnd(std::string output_dir, JedecDRAMSystem *cache, Config &config):
    benchmark_name(output_dir.substr(output_dir.find_last_of('/') + 1)){
    cache_ = cache;
    int cache_line_num = 1 << (LogBase2(BenchmarkInfo.at(benchmark_name) * config.ratio / config.granularity));
    Meta_SRAM.resize(cache_line_num, Tag(0, false, false,config.granularity));
    std::cout<<"cache line num: "<<cache_line_num<<"\n";
}

bool FrontEnd::GetReq(RemoteRequest &req) {
    if(!LSQ.empty()){
        req = LSQ.front();
        LSQ.erase(LSQ.begin());
        return true;
    }
    return false;
}

void FrontEnd::CacheWriteCallBack(uint64_t req_id) {

}

void FrontEnd::Refill(uint64_t req_id) {
    resp.emplace_back(std::make_pair(req_id, cache_->clk_ + 1));
}

bool FrontEnd::GetResp(uint64_t &req_id) {
    if(!resp.empty() && cache_->clk_ >= resp.front().second){
        req_id = resp.front().first;
        resp.erase(resp.begin());
        return true;
    }
    return false;
}

void FrontEnd::Drained() {
    if(!front_q.empty()){
        LSQ.emplace_back(RemoteRequest(front_q.front().second,
                                       front_q.front().first,
                                       1,
                                       1));
        front_q.erase(front_q.begin());
    }
}

bool FrontEnd::AddTransaction(uint64_t hex_addr, bool is_write) {
    front_q.emplace_back(std::make_pair(hex_addr, is_write));
    return true;
}

bool FrontEnd::WillAcceptTransaction(uint64_t hex_addr, bool is_write) const {
    if(front_q.size() < queue_capacity)
        return true;
    return false;
}

cadcache::cadcache(Config &config, const std::string &config_str, const std::string &output_dir,
                   std::function<void (uint64_t)> read_callback,
                   std::function<void (uint64_t)> write_callback)
                   : JedecDRAMSystem(config, output_dir, read_callback, write_callback),
                   remote_config_(new Config(config_str, output_dir + "/remote")),
                   remote_memory(*(remote_config_),
                                 output_dir + "/remote",
                                 std::bind(&cadcache::RemoteCallback, this, std::placeholders::_1),
                                 nullptr),
                   egress_link(config)
                   {
    std::cout<<"cadcache construct "<<output_dir.substr(output_dir.find_last_of('/') + 1)<<"\n";
    if(config_.cache_policy == Policy::Dummy)
        cache_controller = new FrontEnd(output_dir, this, config);
    else if(config_.cache_policy == Policy::DirectMapped)
        cache_controller = new DirectMap(output_dir, this, config);
    else if(config_.cache_policy == Policy::Kona)
        cache_controller = new Kona(output_dir, this, config);
    else if(config_.cache_policy == Policy::Our)
        cache_controller = new our(output_dir, this, config);
    JedecDRAMSystem::RegisterCallbacks(
            std::bind(&FrontEnd::CacheReadCallBack, cache_controller, std::placeholders::_1),
            std::bind(&FrontEnd::CacheWriteCallBack, cache_controller, std::placeholders::_1)
            );
    read_callback_ = read_callback;
    write_callback_ = write_callback;
};

bool cadcache::AddTransaction(uint64_t hex_addr, bool is_write) {
    cache_controller->AddTransaction(hex_addr, is_write);

    if(is_write){
        write_buffer.emplace_back(hex_addr, clk_ + 1);
    }
    return true;
}

bool cadcache::WillAcceptTransaction(uint64_t hex_addr, bool is_write) const {
    return cache_controller->WillAcceptTransaction(hex_addr, is_write);
}

void cadcache::ClockTick() {
    //TODO: set proper sz.
    if(req_.sz == 0)
        cache_controller->GetReq(req_);

    uint64_t req_sz = 0;
    if(req_.sz != 0)
        req_sz = req_.is_write ? req_.sz : 1;

    if(req_sz != 0){
        if(egress_link.AddTransaction(req_, clk_, req_sz))
            req_.sz = 0;
    }

    RemoteRequest tmp;
    if(egress_link.GetReq(tmp, clk_)){
        if(remote_memory.WillAcceptTransaction(tmp)){
            remote_memory.AddTransaction(tmp);
        }else{
            std::cerr<<" remote memory must accept \n";
            AbruptExit(__FILE__, __LINE__);
        }
    }

    if(! write_buffer.empty() && write_buffer.front().second <= clk_){
        write_callback_(write_buffer.front().first);
        write_buffer.erase(write_buffer.begin());
    }

    uint64_t req_id;
    if(cache_controller->GetResp(req_id)){
        read_callback_(req_id);
    }

    JedecDRAMSystem::ClockTick();
    remote_memory.ClockTick();
    cache_controller->Drained();
}

void cadcache::WarmUp(uint64_t hex_addr, bool is_write) {
    cache_controller->WarmUp(hex_addr, is_write);
}

void cadcache::RemoteCallback(uint64_t req_id) {
    cache_controller->Refill(req_id);
}

void cadcache::PrintStats() {
    //std::ofstream json_out(config_.json_stats_name, std::ofstream::out);
    //json_out << "DRAM Cache: \n";
    //json_out.close();
    egress_link.PrintStat();
    cache_controller->PrintStat();
    JedecDRAMSystem::PrintStats();

    //json_out.open(config_.json_stats_name, std::ofstream::app);
    //json_out<<"Remote memory: \n";
    //json_out.close();
    remote_memory.PrintStats();
}

void cadcache::ResetStats() {
    cache_controller->ResetStat();
}

MemoryPool::MemoryPool(Config &config, const std::string &output_dir,
                       std::function<void (uint64_t)> read_callback,
                       std::function<void (uint64_t)> write_callback)
                       :JedecDRAMSystem(config, output_dir,
                                        std::bind(&MemoryPool::MediaCallback, this, std::placeholders::_1),
                                        std::bind(&MemoryPool::MediaCallback, this, std::placeholders::_1)),
                       egress_link(config){
    read_callback_ = read_callback;
    write_callback_ = write_callback;
}

void MemoryPool::ClockTick() {
    uint64_t req_id;
    if(egress_link.GetReq(req_id, clk_)){
        read_callback_(req_id);
    }

    if(! flits_issue.empty()){
        uint64_t index = flits_issue.front().second;
        uint64_t hex_addr = flits_issue.front().first->first.hex_addr + index * 64;
        //kept WAR order
        if(pending_reqs.find(hex_addr) == pending_reqs.end()
            && JedecDRAMSystem::WillAcceptTransaction(hex_addr,
                                                      flits_issue.front().first->first.is_write)){
            JedecDRAMSystem::AddTransaction(hex_addr, flits_issue.front().first->first.is_write);
            index++;
            pending_reqs[hex_addr] = flits_issue.front().first;
            if(index == flits_issue.front().first->first.sz){
                flits_issue.erase(flits_issue.begin());
            }else{
                flits_issue.front().second = index;
            }
        }
    }

    JedecDRAMSystem::ClockTick();
}

bool MemoryPool::WillAcceptTransaction(RemoteRequest req) {
    return true;
}

bool MemoryPool::AddTransaction(RemoteRequest req) {
    std::pair<RemoteRequest, int>* tmp = new std::pair<RemoteRequest, int>(req, req.sz);
    flits_issue.push_back(std::make_pair(tmp, 0));
    return true;
}

void MemoryPool::MediaCallback(uint64_t req_id) {
    if(pending_reqs.find(req_id) != pending_reqs.end()){
        pending_reqs[req_id]->second--;
        if(pending_reqs[req_id]->second == 0){
            if(pending_reqs[req_id]->first.is_write == false){
                egress_link.AddTransaction(pending_reqs[req_id]->first.hex_addr, clk_,
                                           pending_reqs[req_id]->first.sz);
            }
            delete pending_reqs[req_id];
        }
        pending_reqs.erase(req_id);
    }
}

}