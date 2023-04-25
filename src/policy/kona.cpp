//
// Created by zhangxu on 4/3/23.
//

#include "kona.h"

namespace dramsim3 {

Kona::Kona(std::string output_dir, JedecDRAMSystem *cache, Config &config) :
    CacheFrontEnd(output_dir, cache, config),
    index_num(Meta_SRAM.size() / 4),
    hashmap_hex_addr(Meta_SRAM.size()*granularity){
    std::cout<<"Kona frontend\n";
    utilization_file.open(output_dir+"/utilization_Kona_"+std::to_string(granularity));
    if (utilization_file.fail()) {
        std::cerr << "utilization file does not exist" << std::endl;
        AbruptExit(__FILE__, __LINE__);
    }
}

bool Kona::GetTag(uint64_t hex_addr, Tag *&tag_, uint64_t &hex_addr_cache) {
    uint64_t tag = hex_addr / granularity / index_num;
    uint64_t index = hex_addr / granularity % index_num;
    tag_ = NULL;
    for (int i = index * 4; i < ((index + 1) * 4); ++i) {
        if(Meta_SRAM[i].valid && Meta_SRAM[i].tag == tag){
            tag_ = &Meta_SRAM[i];
            hex_addr_cache = i * granularity + (hex_addr & granularity_mask);
            return true;
        }
    }
    return false;
}

uint64_t Kona::GetHexTag(uint64_t hex_addr) {
    return hex_addr / granularity / index_num;
}

uint64_t Kona::GetHexAddr(uint64_t hex_tag, uint64_t hex_addr_cache) {
    return ((hex_addr_cache / granularity / 4) + hex_tag * index_num) * granularity;
}

uint64_t Kona::AllocCPage(uint64_t hex_addr, Tag *&tag_) {
    uint64_t tag = hex_addr / granularity / index_num;
    uint64_t index = hex_addr / granularity % index_num;
    tag_ = NULL;
    for (int i = index * 4; i < ((index + 1) * 4); ++i) {
        if(!Meta_SRAM[i].valid){
            tag_ = &Meta_SRAM[i];
            return i * granularity;
        }
    }

    tag_ = &Meta_SRAM[index * 4 + tag % 4];
    return (index * 4 + tag % 4) * granularity;
}

void Kona::MissHandler(uint64_t hex_addr, bool is_write) {
    MSHR_sz ++;
    uint64_t hex_addr_remote = hex_addr / granularity * granularity;
    if(MSHRs.find(hex_addr_remote) != MSHRs.end()){
        MSHRs[hex_addr_remote].emplace_back(Transaction(hex_addr, is_write));
    }else{
        MSHRs[hex_addr_remote] = std::list<Transaction>{Transaction(hex_addr, is_write)};
        if(hex_addr_remote > (CacheAddr)32*1024*1024*1024){
            std::cerr << "$DRAM address exceed memory capacity "<<hex_addr << std::endl;
            AbruptExit(__FILE__, __LINE__);
        }
        RemoteRequest rr(false, hex_addr_remote, granularity / 64, 0);
        pending_req_to_hashmap.emplace_back(std::make_pair(hex_addr_remote+hashmap_hex_addr, rr));
    }
}

void Kona::WriteBackData(Tag tag_, uint64_t hex_addr_cache) {
    uint64_t tag = tag_.tag;
    uint64_t hex_addr = ((hex_addr_cache / granularity / 4) + tag * index_num) * granularity;
    uint64_t dirty_num = std::count(tag_.dirty_bits.begin(), tag_.dirty_bits.end(), true);
    uint64_t hex_addr_remote = hex_addr / granularity * granularity;
    RemoteRequest rr(true, hex_addr_remote, dirty_num, 0);
    pending_req_to_hashmap.emplace_back(std::make_pair(hex_addr_remote+hashmap_hex_addr, rr));
}

void Kona::HashReadCallBack(uint64_t req_id) {
    if(waiting_resp_from_hashmap.find(req_id) != waiting_resp_from_hashmap.end()){
        LSQ.emplace_back(waiting_resp_from_hashmap[req_id]);
        waiting_resp_from_hashmap.erase(req_id);
        return;
    }
    std::cerr << "$DRAM read without destination "<<req_id << std::endl;
    AbruptExit(__FILE__, __LINE__);
}

void Kona::Drained() {
    CacheFrontEnd::Drained();

    if(!pending_req_to_hashmap.empty()){
        if(waiting_resp_from_hashmap.find(pending_req_to_hashmap.front().first) != waiting_resp_from_hashmap.end())
            return;

        if(cache_->JedecDRAMSystem::WillAcceptTransaction(pending_req_to_hashmap.front().first, false)){
            cache_->JedecDRAMSystem::AddTransaction(pending_req_to_hashmap.front().first, false);
            waiting_resp_from_hashmap[pending_req_to_hashmap.front().first] = pending_req_to_hashmap.front().second;
            pending_req_to_hashmap.erase(pending_req_to_hashmap.begin());
        }
    }
}
}