//
// Created by zhangxu on 4/1/23.
//

#include "direct_map.h"

namespace dramsim3 {

DirectMap::DirectMap(std::string output_dir, JedecDRAMSystem *cache, Config &config):
            FrontEnd(output_dir, cache, config),
            granularity(config.granularity){
    std::cout<<"Direct mapped frontend\n";
    capacity_mask = Meta_SRAM.size() * granularity - 1;
    MSHR_sz = 0;
    hit = 0;
    miss = 0;
    wb_hit = 0;
    for (int i = 0; i < (granularity/256); ++i) {
        line_utility[i] = 0;
    }
}

bool DirectMap::hit_and_return(uint64_t hex_addr_cache, bool is_write){
    uint64_t index = hex_addr_cache / granularity;
    uint64_t offset = hex_addr_cache % granularity;
    hit++;
    line_utility[Meta_SRAM[index].utilized()] ++;
    Meta_SRAM[index].accessed[offset / 256] = true;
    Meta_SRAM[index].dirty |= is_write;
    return true;
}

bool DirectMap::miss_and_return() {
    line_utility[0]++;
    miss++;
    return true;
}

bool DirectMap::ProcessOneReq() {
    uint64_t hex_addr = front_q.front().first;
    bool is_write = front_q.front().second;

    //std::cout<<"cache controller: ProcessOneReq "<<std::hex<<hex_addr<<(is_write ? " W": " R")<<"\n"<<std::flush;
    uint64_t hex_addr_cache = hex_addr & capacity_mask;
    uint64_t tag = hex_addr / granularity / Meta_SRAM.size();
    uint64_t index = hex_addr_cache / granularity;

    //check tags
    if(Meta_SRAM[index].valid && Meta_SRAM[index].tag == tag){
        //std::cout<<"hit in Meta_SRAM\n";

        // if hit in refill buffer
        uint64_t hex_addr_refill = hex_addr_cache / granularity * granularity;
        if(std::find(refill_buffer.begin(), refill_buffer.end(), hex_addr_refill) != refill_buffer.end()){
            if(!is_write)
                resp.emplace_back(std::make_pair(hex_addr,
                                                 GetCLK() + latency));
            return hit_and_return(hex_addr_cache, is_write);
        }

        //if hit in refill_req_to_cache
        if(std::find_if(refill_req_to_cache.begin(), refill_req_to_cache.end(), [hex_addr_cache](const Transaction a){
            return a.addr == hex_addr_cache;
        }) != refill_req_to_cache.end()){
            if(!is_write)
                resp.emplace_back(std::make_pair(hex_addr,
                                                 GetCLK() + latency));
            return hit_and_return(hex_addr_cache, is_write);
        }

        if(cache_->JedecDRAMSystem::WillAcceptTransaction(hex_addr_cache, is_write)){
            cache_->JedecDRAMSystem::AddTransaction(hex_addr_cache, is_write);
            if(!is_write)
                pending_req_to_cache[hex_addr_cache] = hex_addr;
            return hit_and_return(hex_addr_cache, is_write);
        }
        return false;
    }

    //check write back buffer
    uint64_t hex_addr_aligned = index * granularity;
    if(write_back_buffer.find(hex_addr_aligned) != write_back_buffer.end()){
        //std::cout<<"hit in write back buffer\n";
        if(!is_write)
            resp.emplace_back(std::make_pair(hex_addr,
                                             GetCLK() + latency));
        wb_hit++;
        return true;
    }

    //miss check MSHR
    if(MSHR_sz >= 64)
        return false;

    //std::cout<<"cache miss\n";
    MSHR_sz ++;
    uint64_t hex_addr_remote = hex_addr / granularity * granularity;
    if(MSHRs.find(hex_addr_remote) != MSHRs.end()){
        MSHRs[hex_addr_remote].emplace_back(Transaction(hex_addr, is_write));
    }else{
        MSHRs[hex_addr_remote] = std::list<Transaction>{Transaction(hex_addr, is_write)};
        LSQ.emplace_back(RemoteRequest(false, hex_addr_remote,
                                   granularity / 64, latency));
    }

    return miss_and_return();
}

void DirectMap::Drained() {
    if(!front_q.empty() && ProcessOneReq())
        front_q.erase(front_q.begin());

    if(refill_req_to_cache.empty())
        return;

    //stick to WAR RAR order
    if(pending_req_to_cache.find(refill_req_to_cache.front().addr) != pending_req_to_cache.end())
        return;

    if(cache_->JedecDRAMSystem::WillAcceptTransaction(refill_req_to_cache.front().addr,
                                     refill_req_to_cache.front().is_write)){

        cache_->JedecDRAMSystem::AddTransaction(refill_req_to_cache.front().addr,
                               refill_req_to_cache.front().is_write);
        refill_req_to_cache.erase(refill_req_to_cache.begin());
    }
}

void DirectMap::CacheReadCallBack(uint64_t req_id) {
    uint64_t hex_addr_aligned = req_id / granularity * granularity;
    //check pending_req_to_cache first in case the refill request coming right after
    //the read req sent to $DRAM
    if(pending_req_to_cache.find(req_id) != pending_req_to_cache.end()){
        resp.emplace_back(std::make_pair(pending_req_to_cache[req_id],
                                     GetCLK() + latency));
        pending_req_to_cache.erase(req_id);
    }else if(write_back_buffer.find(hex_addr_aligned) != write_back_buffer.end()){
        write_back_buffer[hex_addr_aligned].second --;
        //send eviction to remote
        //std::cout<<"Cache eviction "<<std::hex<<req_id<<" of "<<write_back_buffer[hex_addr_aligned].first.hex_addr<<"\n";
        if(write_back_buffer[hex_addr_aligned].second == 0){
            // write new data from refill buffer
            for (uint64_t i = 0; i < granularity; i += 64) {
                refill_req_to_cache.emplace_back(
                        Transaction(hex_addr_aligned + i, true));
            }
            refill_buffer.erase(std::find(refill_buffer.begin(),
                                          refill_buffer.end(),
                                          hex_addr_aligned));

            LSQ.emplace_back(write_back_buffer[hex_addr_aligned].first);
            write_back_buffer.erase(hex_addr_aligned);
        }
    }else{
        //std::cout<<"Cache response "<<std::hex<<pending_req_to_cache[req_id]<<"\n";
        std::cerr << "$DRAM read without destination" << std::endl;
        AbruptExit(__FILE__, __LINE__);
    }
}

void DirectMap::Refill(uint64_t req_id) {
    auto reqs = MSHRs[req_id];
    MSHRs.erase(req_id);
    MSHR_sz -= reqs.size();
    uint64_t hex_addr_cache = req_id & capacity_mask;
    uint64_t tag = req_id / granularity / Meta_SRAM.size();
    uint64_t index = hex_addr_cache / granularity;

    bool refill_buffer_miss = std::find(refill_buffer.begin(), refill_buffer.end(), hex_addr_cache)
            == refill_buffer.end();
    bool is_old_dirty = Meta_SRAM[index].valid && Meta_SRAM[index].dirty;

    //hit in refill buffer
    if(!refill_buffer_miss && is_old_dirty){
        //std::cout<<"Cache refill: hit and dirty "<<std::hex<<req_id<<"\n";
        LSQ.emplace_back(RemoteRequest(true,
                                       ((Meta_SRAM[index].tag * Meta_SRAM.size())+index)*granularity,
                                       granularity / 64,
                                       latency));
    }

    // read and evict dirty data
    if(is_old_dirty && refill_buffer_miss){
        //std::cout<<"Cache refill: miss and dirty "<<std::hex<<req_id<<"\n";
        for (uint64_t i = 0; i < granularity; i += 64) {
            refill_req_to_cache.emplace_back(
                    Transaction(hex_addr_cache + i, false));
        }
        write_back_buffer[hex_addr_cache] = std::make_pair(RemoteRequest(true,
                                            ((Meta_SRAM[index].tag * Meta_SRAM.size())+index)*granularity,
                                            granularity / 64,
                                            latency), granularity / 64);
        //new data are stored in refill buffer temporarily.
        refill_buffer.push_back(hex_addr_cache);
    }

    if(!is_old_dirty && refill_buffer_miss){
        //std::cout<<"Cache refill: miss and clean "<<std::hex<<req_id<<"\n";
        // write data
        for (uint64_t i = 0; i < granularity; i += 64) {
            refill_req_to_cache.emplace_back(
                    Transaction(hex_addr_cache + i, true));
        }
    }

    // update tags
    // response data
    //std::cout<<reqs.size()<<" wait in MSHR\n";
    Meta_SRAM[index] = Tag(tag, true, false, granularity);
    for (auto i = reqs.begin(); i != reqs.end(); ++i) {
        if(!i->is_write){
            resp.emplace_back(std::make_pair(i->addr, GetCLK() + 1));
        }
        Meta_SRAM[index].dirty |= i->is_write;
        Meta_SRAM[index].accessed[(i->addr%granularity)/256] = true;
    }

}

void DirectMap::WarmUp(uint64_t hex_addr, bool is_write) {
    uint64_t hex_addr_cache = hex_addr & capacity_mask;
    uint64_t tag = hex_addr / granularity / Meta_SRAM.size();
    uint64_t index = hex_addr_cache / granularity;

    if(Meta_SRAM[index].valid && Meta_SRAM[index].tag == tag){
        Meta_SRAM[index].dirty |= is_write;
    }else{
        Meta_SRAM[index] = Tag(tag, true, is_write, granularity);
    }

    Meta_SRAM[index].accessed[(hex_addr_cache % granularity) / 256] = true;
}

void DirectMap::PrintStat() {
    std::cout<<"hit: "<<std::dec<<hit<<"\n"
            <<"miss: "<<miss<<"\n"
            <<"hit in write back buffer: "<<wb_hit<<"\n";
    std::cout<<"cache line utilization\n";
    for (auto i = line_utility.begin(); i != line_utility.end(); ++i) {
        std::cout<<i->first<<" "<<i->second<<"\n";
    }
}

void DirectMap::ResetStat() {
    std::cout<<"reset directmap stats\n";
    hit = 0;
    wb_hit = 0;
    miss = 0;
    for (int i = 0; i < (granularity/256); ++i) {
        line_utility[i] = 0;
    }
}
}