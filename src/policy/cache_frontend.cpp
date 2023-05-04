//
// Created by zhangxu on 4/21/23.
//

#include "cache_frontend.h"


namespace dramsim3 {

CacheFrontEnd::CacheFrontEnd(std::string output_dir, JedecDRAMSystem *cache, Config &config):
            FrontEnd(output_dir, cache, config),
            granularity(config.granularity),
            granularity_mask(granularity-1){
    std::cout<<"Cache frontend\n";
    MSHR_sz = 0;
    hit = 0;
    miss = 0;
    wb_hit = 0;
    for (int i = 0; i <= (granularity/256); ++i) {
        line_utility[i] = 0;
    }
    for (int i = 0; i <= 64; ++i) {
        mshr_waiting[i] = 0;
    }
}

bool CacheFrontEnd::hit_and_return(Tag &tag_, uint64_t hex_addr, bool is_write){
    uint64_t offset = hex_addr & granularity_mask;
    hit++;
    if(granularity > 256)
        tag_.accessed[offset / 256] = true;
    tag_.dirty |= is_write;
    tag_.dirty_bits[offset / 64] = tag_.dirty_bits[offset / 64] | is_write;
    return true;
}

bool CacheFrontEnd::miss_and_return() {
    miss++;
    return true;
}

bool CacheFrontEnd::ProcessOneReq() {
    uint64_t hex_addr = front_q.front().first;
    bool is_write = front_q.front().second;

    //std::cout<<"cache controller: ProcessOneReq "<<std::hex<<hex_addr<<(is_write ? " W": " R")<<"\n"<<std::flush;
    uint64_t hex_addr_cache;
    Tag *t = NULL;

    //check tags
    if(GetTag(hex_addr, t, hex_addr_cache)){
        //std::cout<<"hit in Meta_SRAM\n";

        // if hit in refill buffer
        uint64_t hex_addr_refill = hex_addr_cache / granularity * granularity;
        if(std::find(refill_buffer.begin(), refill_buffer.end(), hex_addr_refill) != refill_buffer.end()){
            if(!is_write)
                resp.emplace_back(std::make_pair(hex_addr,
                                                 GetCLK() + latency));
            return hit_and_return(*t, hex_addr, is_write);
        }

        //if hit in refill_req_to_cache
        if(std::find_if(refill_req_to_cache.begin(), refill_req_to_cache.end(), [hex_addr_cache](const Transaction a){
            return a.addr == hex_addr_cache;
        }) != refill_req_to_cache.end()){
            if(!is_write)
                resp.emplace_back(std::make_pair(hex_addr,
                                                 GetCLK() + latency));
            return hit_and_return(*t, hex_addr, is_write);
        }

        if(cache_->JedecDRAMSystem::WillAcceptTransaction(hex_addr_cache, is_write)){
            cache_->JedecDRAMSystem::AddTransaction(hex_addr_cache, is_write);
            if(!is_write)
                pending_req_to_cache[hex_addr_cache] = hex_addr;
            return hit_and_return(*t, hex_addr, is_write);
        }
        return false;
    }

    //check write back buffer
    auto wb_entry = std::find_if(write_back_buffer.begin(), write_back_buffer.end(),
                    [hex_addr, this](std::pair<uint64_t, std::pair<Tag, int>> a){
        return this->GetHexAddr(a.second.first.tag, a.first) ==
            (hex_addr / this->granularity * this->granularity);
    });
    if(wb_entry != write_back_buffer.end()){
        //std::cout<<"hit in write back buffer\n";
        if(!is_write)
            resp.emplace_back(std::make_pair(hex_addr,
                                             GetCLK() + latency));
        wb_hit++;
        return hit_and_return(wb_entry->second.first, hex_addr, is_write);
    }

    //miss check MSHR
    if(MSHR_sz >= 64)
        return false;

    //std::cout<<"cache miss\n";
    MissHandler(hex_addr, is_write);
    return miss_and_return();
}

void CacheFrontEnd::ProcessRefillReq() {
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

void CacheFrontEnd::Drained() {
    if(!front_q.empty() && ProcessOneReq())
        front_q.erase(front_q.begin());

    ProcessRefillReq();
}

void CacheFrontEnd::CacheReadCallBack(uint64_t req_id) {
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
            WriteBackData(write_back_buffer[hex_addr_aligned].first, hex_addr_aligned);
            write_back_buffer.erase(hex_addr_aligned);
        }
    }else{
        HashReadCallBack(req_id);
    }
}

void CacheFrontEnd::DoRefill(uint64_t req_id, Tag &t, uint64_t hex_addr_cache) {
    auto reqs = MSHRs[req_id];
    MSHRs.erase(req_id);
    MSHR_sz -= reqs.size();
    uint64_t tag = GetHexTag(req_id);

    bool refill_buffer_miss = std::find(refill_buffer.begin(), refill_buffer.end(), hex_addr_cache)
            == refill_buffer.end();
    bool is_old_dirty = t.valid && t.dirty;

    if(t.valid){
        line_utility[t.utilized()] ++;
    }

    //hit in refill buffer
    if(!refill_buffer_miss && is_old_dirty){
        //std::cout<<"Cache refill: hit and dirty "<<std::hex<<req_id<<"\n";
        //replace and write back the page in refill buffer
        WriteBackData(t, hex_addr_cache);
    }

    // read and evict dirty data
    if(is_old_dirty && refill_buffer_miss){
        //std::cout<<"Cache refill: miss and dirty "<<std::hex<<req_id<<"\n";
        for (uint64_t i = 0; i < granularity; i += 64) {
            refill_req_to_cache.emplace_back(
                    Transaction(hex_addr_cache + i, false));
        }
        write_back_buffer[hex_addr_cache] = std::make_pair(t, granularity / 64);
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

    mshr_waiting[reqs.size()] ++;
    // update tags
    // response data
    //std::cout<<reqs.size()<<" wait in MSHR\n";
    t = Tag(tag, true, false, granularity);
    for (auto i = reqs.begin(); i != reqs.end(); ++i) {
        if(!i->is_write){
            resp.emplace_back(std::make_pair(i->addr, GetCLK() + 1));
        }
        t.dirty |= i->is_write;
        if(granularity > 256)
            t.accessed[(i->addr & granularity_mask)/256] = true;
    }
}

void CacheFrontEnd::Refill(uint64_t req_id) {
    Tag *t = NULL;
    uint64_t hex_addr_cache = AllocCPage(req_id, t);
    DoRefill(req_id, *t, hex_addr_cache);
}

void CacheFrontEnd::WarmUp(uint64_t hex_addr, bool is_write) {
    uint64_t tag = GetHexTag(hex_addr);
    uint64_t hex_addr_cache;
    Tag *t = NULL;

    if(GetTag(hex_addr, t, hex_addr_cache)){
        t->dirty |= is_write;
    }else{
        AllocCPage(hex_addr, t);
        *t = Tag(tag, true, is_write, granularity);
    }
    if(granularity > 256)
        t->accessed[(hex_addr & granularity_mask) / 256] = true;
}

void CacheFrontEnd::PrintStat() {
    utilization_file<<"# hit: "<<std::dec<<hit<<"\n"
            <<"# miss: "<<miss<<"\n"
            <<"# hit in write back buffer: "<<wb_hit<<"\n";
    utilization_file<<"# cache line utilization\n";
    for (auto i = line_utility.begin(); i != line_utility.end(); ++i) {
        utilization_file<<i->first<<" "<<i->second<<"\n";
    }
    std::cout<<"# waiting reqs in MSHR\n";
    for (auto i = mshr_waiting.begin(); i != mshr_waiting.end(); ++i) {
        std::cout<<i->first<<" "<<i->second<<"\n";
    }
}

void CacheFrontEnd::ResetStat() {
    std::cout<<"reset directmap stats\n";
    hit = 0;
    wb_hit = 0;
    miss = 0;
    for (int i = 0; i <= (granularity/256); ++i) {
        line_utility[i] = 0;
    }
    for (int i = 0; i <= 64; ++i) {
        mshr_waiting[i] = 0;
    }
}
}