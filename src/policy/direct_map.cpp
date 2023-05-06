//
// Created by zhangxu on 4/1/23.
//

#include "direct_map.h"
#include <string.h>

namespace dramsim3 {

DirectMap::DirectMap(std::string output_dir, JedecDRAMSystem *cache, Config &config):
            CacheFrontEnd(output_dir, cache, config),
            granularity(config.granularity){
    std::cout<<"Direct mapped frontend\n";
    capacity_mask = Meta_SRAM.size() * granularity - 1;
    utilization_file.open(output_dir+"/utilization_DirectMap_"+std::to_string(granularity));
    if (utilization_file.fail()) {
        std::cerr << "utilization file does not exist" << std::endl;
        AbruptExit(__FILE__, __LINE__);
    }
    tracker_ = new DirectMapTracker(this);
}

bool DirectMap::GetTag(uint64_t hex_addr, Tag *&tag_, uint64_t &hex_addr_cache) {
    uint64_t tag = hex_addr / granularity / Meta_SRAM.size();
    uint64_t index = hex_addr / granularity % Meta_SRAM.size();
    hex_addr_cache = hex_addr & capacity_mask;

    if(Meta_SRAM[index].valid && Meta_SRAM[index].tag == tag){
        tag_ = &Meta_SRAM[index];
        return true;
    }else{
        tag_ = NULL;
        return false;
    }
}

uint64_t DirectMap::GetHexTag(uint64_t hex_addr) {
    return hex_addr / granularity / Meta_SRAM.size();
}

uint64_t DirectMap::AllocCPage(uint64_t hex_addr, Tag *&tag_) {
    hex_addr = hex_addr / granularity * granularity;
    uint64_t index = hex_addr / granularity % Meta_SRAM.size();
    tag_ = &Meta_SRAM[index];
    return hex_addr & capacity_mask;
}

void DirectMap::MissHandler(uint64_t hex_addr, bool is_write) {
    MSHR_sz ++;
    uint64_t hex_addr_remote = hex_addr / granularity * granularity;
    if(MSHRs.find(hex_addr_remote) != MSHRs.end()){
        MSHRs[hex_addr_remote].emplace_back(Transaction(hex_addr, is_write));
    }else{
        MSHRs[hex_addr_remote] = std::list<Transaction>{Transaction(hex_addr, is_write)};
        LSQ.emplace_back(RemoteRequest(false, hex_addr_remote,
                                   granularity / 64, 0));
    }
}

void DirectMap::WriteBackData(Tag tag_, uint64_t hex_addr_cache) {
    uint64_t index = hex_addr_cache / granularity;
    uint64_t hex_addr = ((tag_.tag * Meta_SRAM.size())+index)*granularity;
    LSQ.emplace_back(
            RemoteRequest(true,
                          hex_addr,
                          granularity / 64,
                          0
            )
    );
}

void DirectMap::HashReadCallBack(uint64_t req_id) {
    //std::cout<<"Cache response "<<std::hex<<pending_req_to_cache[req_id]<<"\n";
    std::cerr << "$DRAM read without destination" << std::endl;
    AbruptExit(__FILE__, __LINE__);
}
}