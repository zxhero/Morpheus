//
// Created by zhangxu on 3/15/23.
//

#include "cadcache.h"

namespace dramsim3{

RemoteRequest::RemoteRequest(bool is_write_, uint64_t hex_addr_, int sz_, uint64_t exit_time_) {
    is_write = is_write_;
    hex_addr = hex_addr_;
    sz = sz_;
    exit_time = exit_time_;
}

cadcache::cadcache(Config &config, const std::string &config_str, const std::string &output_dir,
                   std::function<void (uint64_t)> read_callback,
                   std::function<void (uint64_t)> write_callback)
                   : JedecDRAMSystem(config, output_dir, read_callback, write_callback),
                   remote_latency(static_cast<unsigned long>((double)config_.rtt / config_.tCK)),
                   remote_config_(new Config(config_str, output_dir + "/remote")),
                   remote_memory(*(remote_config_),
                                 output_dir + "/remote",
                                 std::bind(&cadcache::RemoteCallback, this, std::placeholders::_1),
                                 std::bind(&cadcache::RemoteCallback, this, std::placeholders::_1))
                   {
    std::cout<<"cadcache construct\n";
};

bool cadcache::AddTransaction(uint64_t hex_addr, bool is_write) {
    if(egress_busy_clk < clk_){
        egress_busy_clk = clk_;
    }
    RemoteRequest req(
            is_write, hex_addr, 1, egress_busy_clk+remote_latency+1
            );

    ethernet.push_back(req);
    egress_busy_clk += 1;

    //std::cout<<"req: "<<req.hex_addr<<" "<<req.exit_time<<"\n";

    if(is_write){
        write_buffer.emplace_back(hex_addr, clk_ + 1);
    }
    return true;
}

bool cadcache::WillAcceptTransaction(uint64_t hex_addr, bool is_write) const {
    return true;
}

void cadcache::ClockTick() {

    if(! ethernet.empty() && ethernet[0].exit_time <= clk_){
        if(remote_memory.WillAcceptTransaction(ethernet[0])){
            remote_memory.AddTransaction(ethernet[0]);
            ethernet.erase(ethernet.begin());
        }
    }

    if(! write_buffer.empty() && write_buffer.front().second <= clk_){
        write_callback_(write_buffer.front().first);
        write_buffer.erase(write_buffer.begin());
    }

    JedecDRAMSystem::ClockTick();
    remote_memory.ClockTick();
}

void cadcache::RemoteCallback(uint64_t req_id) {
    read_callback_(req_id);
}

void cadcache::PrintStats() {
    //std::ofstream json_out(config_.json_stats_name, std::ofstream::out);
    //json_out << "DRAM Cache: \n";
    //json_out.close();
    JedecDRAMSystem::PrintStats();

    //json_out.open(config_.json_stats_name, std::ofstream::app);
    //json_out<<"Remote memory: \n";
    //json_out.close();
    remote_memory.PrintStats();
}

void cadcache::ResetStats() {
    JedecDRAMSystem::ResetStats();
    remote_memory.ResetStats();
}

MemoryPool::MemoryPool(Config &config, const std::string &output_dir,
                       std::function<void (uint64_t)> read_callback,
                       std::function<void (uint64_t)> write_callback)
                       :JedecDRAMSystem(config, output_dir,
                                        std::bind(&MemoryPool::MediaCallback, this, std::placeholders::_1),
                                        std::bind(&MemoryPool::MediaCallback, this, std::placeholders::_1)){
    read_callback_ = read_callback;
    write_callback_ = write_callback;
}

void MemoryPool::ClockTick() {
    if(! flits_issue.empty()){
        if(JedecDRAMSystem::WillAcceptTransaction(flits_issue[0].first, flits_issue[0].second)){
            JedecDRAMSystem::AddTransaction(flits_issue[0].first, flits_issue[0].second);
            flits_issue.erase(flits_issue.begin());
        }
    }

    JedecDRAMSystem::ClockTick();
}

bool MemoryPool::WillAcceptTransaction(RemoteRequest req) {
    return true;
}

bool MemoryPool::AddTransaction(RemoteRequest req) {
    std::pair<RemoteRequest, int>* tmp = new std::pair<RemoteRequest, int>(req, req.sz);
    for (int i = 0; i < req.sz; ++i) {
        flits_issue.push_back(std::make_pair(req.hex_addr + i * 64, req.is_write));
        pending_reqs[req.hex_addr + i * 64] = tmp;
    }
    return true;
}

void MemoryPool::MediaCallback(uint64_t req_id) {
    if(pending_reqs.find(req_id) != pending_reqs.end()){
        pending_reqs[req_id]->second--;
        if(pending_reqs[req_id]->second == 0){
            if(pending_reqs[req_id]->first.is_write == false){
                read_callback_(pending_reqs[req_id]->first.hex_addr);
            }
            delete pending_reqs[req_id];
            pending_reqs.erase(req_id);
        }
    }
}

}