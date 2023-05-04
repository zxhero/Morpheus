//
// Created by zhangxu on 4/25/23.
//

#include "our.h"

namespace dramsim3{

template<class dType>
SRAMCache<dType>::SRAMCache(uint64_t hex_base, JedecDRAMSystem *dram_, uint64_t sz, uint64_t capacity,
                            std::vector<dType> *data_backup_)
    : hex_dram_base_addr(hex_base), dType_sz(sz), data_backup(data_backup_), dram(dram_){
    data.resize(capacity);
    tags.resize(capacity, Tag(0, false, false, 0));
}

template<class dType>
bool SRAMCache<dType>::AddTransaction(uint64_t hex_offset, bool is_write, dType dptr) {
    uint64_t index = hex_offset % tags.size();
    uint64_t tag = hex_offset / tags.size();
    bool hit = tags[index].tag == tag && tags[index].valid;
    uint64_t hex_dram_addr = hex_dram_base_addr + hex_offset * dType_sz;
    if(is_write){
        bool dirty = tags[index].valid && tags[index].dirty;
        if(!hit){
            tags[index].tag = tag;
            tags[index].valid = true;
        }

        if(!hit && dirty){
            pending_req_to_dram.push_back(std::make_pair(hex_dram_addr, true));
        }

        data[index] = dptr;
        tags[index].dirty = true;
        (*data_backup)[hex_offset] = dptr;
        //cancel any read to same offset
        auto ptr = std::find_if(pending_req_to_dram.begin(), pending_req_to_dram.end(),
                        [hex_dram_addr](std::pair<CacheAddr, bool> a){
            return a.first == hex_dram_addr;
        });
        if(ptr != pending_req_to_dram.end()){
            pending_req_to_dram.erase(ptr);
        }
        //drop any in-flight read to same offset
        if(waiting_resp_from_dram.find(hex_dram_addr) != waiting_resp_from_dram.end()){
            waiting_resp_from_dram.erase(hex_dram_addr);
            //std::cerr << "Drop $DRAM read "<<hex_dram_addr << std::endl;
            if(droped_reqs.find(hex_dram_addr) == droped_reqs.end()){
                droped_reqs[hex_dram_addr] = 1;
            }else{
                droped_reqs[hex_dram_addr] ++;
            }
        }
        return true;
    }

    //read
    if(!hit){
        //if miss already, we do not resend dram read
        if(waiting_resp_from_dram.find(hex_dram_addr) == waiting_resp_from_dram.end()
        && std::find_if(pending_req_to_dram.begin(), pending_req_to_dram.end(),
                        [hex_dram_addr](std::pair<CacheAddr, bool> a){
            return a.first == hex_dram_addr && !a.second;
        }) == pending_req_to_dram.end()){
            pending_req_to_dram.push_back(std::make_pair(hex_dram_addr, false));
        }
    }

    if(hit){
        last_req.erase(hex_offset);
        hit_++;
    }
    else if(last_req.find(hex_offset) == last_req.end()){
        miss_++;
        last_req.insert(hex_offset);
    }
    return hit;
}

template<class dType>
void SRAMCache<dType>::Drained() {
    if(!pending_req_to_dram.empty()){
        if(dram->JedecDRAMSystem::WillAcceptTransaction(pending_req_to_dram.front().first,
                                                        pending_req_to_dram.front().second)){
            dram->JedecDRAMSystem::AddTransaction(pending_req_to_dram.front().first,
                                                  pending_req_to_dram.front().second);
            if(!pending_req_to_dram.front().second)
                waiting_resp_from_dram[pending_req_to_dram.front().first] =
                    (pending_req_to_dram.front().first - hex_dram_base_addr) / dType_sz;
            pending_req_to_dram.pop_front();
        }
    }
}

template<class dType>
void SRAMCache<dType>::DRAMReadBack(CacheAddr req_id) {
    if(droped_reqs.find(req_id) != droped_reqs.end()){
        if(droped_reqs[req_id] == 1)
            droped_reqs.erase(req_id);
        else
            droped_reqs[req_id] --;
        return;
    }

    if(waiting_resp_from_dram.find(req_id) != waiting_resp_from_dram.end()){
        uint64_t index = waiting_resp_from_dram[req_id] % tags.size();
        uint64_t tag = waiting_resp_from_dram[req_id] / tags.size();
        tags[index] = Tag(tag, true, false, 0);
        data[index] = (*data_backup)[waiting_resp_from_dram[req_id]];
        waiting_resp_from_dram.erase(req_id);
        return;
    }
    std::cerr << "$DRAM read without destination "<<req_id << std::endl;
    AbruptExit(__FILE__, __LINE__);
}

template<class dType>
dType SRAMCache<dType>::GetData(uint64_t hex_offset) {
    uint64_t index = hex_offset % tags.size();
    return data[index];
}

template<class dType>
dType SRAMCache<dType>::WarmUp(uint64_t hex_offset, bool is_write, dType dptr) {
    uint64_t index = hex_offset % tags.size();
    uint64_t tag = hex_offset / tags.size();
    bool hit = tags[index].tag == tag && tags[index].valid;

    if(is_write){
        data[index] = dptr;
        tags[index] = Tag(tag, true, true, 0);
        (*data_backup)[hex_offset] = dptr;
    }else if(!hit){
        tags[index] = Tag(tag, true, false, 0);
        data[index] = (*data_backup)[hex_offset];
    }

    return data[index];
}

our::our(std::string output_dir, JedecDRAMSystem *cache, Config &config):
    CacheFrontEnd(output_dir, cache, config),
    hashmap_hex_addr(Meta_SRAM.size()*granularity),
    pte_size(12),
    RPT_hex_addr(hashmap_hex_addr + Meta_SRAM.size() * 2 * pte_size ),
    rpte_size(8),
    tlb(hashmap_hex_addr, cache, pte_size, Meta_SRAM.size() * 2 / 64, &hash_page_table){
    std::cout<<"our frontend\n";
    utilization_file.open(output_dir+"/utilization_our_"+std::to_string(granularity));
    if (utilization_file.fail()) {
        std::cerr << "utilization file does not exist" << std::endl;
        AbruptExit(__FILE__, __LINE__);
    }
    v_hex_addr_cache = 0;
    uint32_t virtual_cache_page_num = Meta_SRAM.size();
    hash_page_table.resize(virtual_cache_page_num * 2);
    pte_addr_table.resize(Meta_SRAM.size());
    std::cout<<"hash page table size is "<<hash_page_table.size()<<"\n";
    std::cout<<"tlb size is "<<tlb.data.size()<<"\n";

    collision_times = 0;
    non_collision_times = 0;
}

void our::HashReadCallBack(uint64_t req_id) {
    if(waiting_resp_RPT.find(req_id) != waiting_resp_RPT.end()){
        InsertRemotePage(waiting_resp_RPT[req_id]);
        waiting_resp_RPT.erase(req_id);
    }else
        tlb.DRAMReadBack(req_id);
}

//we get here only after hit in tlb
bool our::GetTag(uint64_t hex_addr, Tag *&tag_, uint64_t &hex_addr_cache) {
    uint64_t hex_addr_aligned = (hex_addr / granularity) * granularity;
    uint32_t value[128/32];
    MurmurHash3_x64_128(&hex_addr_aligned, sizeof(uint64_t), 0, value);
    uint32_t pt_index = value[0] % hash_page_table.size();
    PTentry pte = tlb.GetData(pt_index);

    //PT hit
    //TODO: we use tag and hex_addr_aligned as tag for now
    if(pte.valid && pte.hex_addr_aligned == hex_addr_aligned) {
        uint64_t index = pte.hex_cache_addr / granularity;
        uint64_t tag = hex_addr_aligned;
        //cache hit
        if(Meta_SRAM[index].valid && Meta_SRAM[index].tag == tag){
            tag_ = &Meta_SRAM[index];
            hex_addr_cache = index * granularity + hex_addr % granularity;
            return true;
        }else{
            std::cerr<<"we do not have PT hit but cache miss for now\n";
            AbruptExit(__FILE__, __LINE__);
        }
    }

    //PT miss
    return false;
}

uint64_t our::GetHexAddr(uint64_t hex_tag, uint64_t hex_addr_cache) {
    return hex_tag;
}

void our::MissHandler(uint64_t hex_addr, bool is_write) {
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

void our::WriteBackData(Tag tag_, uint64_t hex_addr_cache) {
    LSQ.emplace_back(
        RemoteRequest(true,
                      tag_.tag,
                      granularity / 64,
                      0
        )
    );
}

void our::Refill(uint64_t req_id) {
    pending_req_to_PT.emplace_back(req_id);
}

void our::Drained() {
    tlb.Drained();
    if(!pending_req_to_PT.empty()){
        uint32_t value[128/32];
        MurmurHash3_x64_128(&(*pending_req_to_PT.begin()), sizeof(uint64_t), 0, value);
        uint32_t pt_index = value[0] % hash_page_table.size();
        if(tlb.AddTransaction(pt_index, false)){
            PTentry pte = tlb.GetData(pt_index);
            //hash collision
            if(pte.valid){
                collision_times ++;
                intermediate_data tmp(&pte_addr_table[pte.hex_cache_addr / granularity], pte, pt_index);
                tmp.pte.hex_addr_aligned = pending_req_to_PT.front();
                uint64_t hex_addr = RPT_hex_addr + pte.hex_cache_addr / granularity * rpte_size;
                pending_write_to_RPT.emplace_back(std::make_pair(hex_addr, tmp));
                //in case the collided cpage is at the head of fifo
                if(v_hex_addr_cache == tmp.pte.hex_cache_addr){
                    v_hex_addr_cache += (granularity);
                    v_hex_addr_cache %= (Meta_SRAM.size() * granularity);
                }
            }else{
                non_collision_times ++;
                AllocCPage(pt_index, pending_req_to_PT.front());
            }
            pending_req_to_PT.pop_front();
        }
    }

    if(!pending_write_to_RPT.empty()){
        if(cache_->JedecDRAMSystem::WillAcceptTransaction(pending_write_to_RPT.front().first, true)){
            cache_->JedecDRAMSystem::AddTransaction(pending_write_to_RPT.front().first, true);
            if(pending_write_to_RPT.front().second.valid)
                InsertRemotePage((pending_write_to_RPT.front().second));
            pending_write_to_RPT.pop_front();
        }
    }

    if(!pending_req_to_RPT.empty()){
        if(cache_->JedecDRAMSystem::WillAcceptTransaction(pending_req_to_RPT.front().first, false)){
            pending_write_to_RPT.emplace_back(std::make_pair(pending_req_to_RPT.front().first - 64, intermediate_data()));
            cache_->JedecDRAMSystem::AddTransaction(pending_req_to_RPT.front().first, false);
            waiting_resp_RPT[pending_req_to_RPT.front().first] = pending_req_to_RPT.front().second;
            pending_req_to_RPT.pop_front();
        }
    }

    if(!front_q.empty()){
        uint64_t hex_addr = (front_q.front().first / granularity) * granularity;
        uint32_t value[128/32];
        MurmurHash3_x64_128(&hex_addr, sizeof(uint64_t), 0, value);
        uint32_t pt_index = value[0] % hash_page_table.size();
        if(tlb.AddTransaction(pt_index, false)){
            if(CacheFrontEnd::ProcessOneReq()){
                front_q.erase(front_q.begin());
            }
        }
    }

    CacheFrontEnd::ProcessRefillReq();
}

void our::AllocCPage(uint32_t pt_index, uint64_t req_id) {
    uint64_t index = v_hex_addr_cache / granularity;
    intermediate_data tmp(&pte_addr_table[v_hex_addr_cache / granularity],
                          PTentry(req_id, v_hex_addr_cache),
                          pt_index);

    bool prefetched = v_hex_addr_cache / granularity * rpte_size % 64 != 0;
    if(Meta_SRAM[index].valid && !prefetched){
        uint64_t hex_addr = RPT_hex_addr + v_hex_addr_cache / granularity * rpte_size;
        pending_req_to_RPT.emplace_back(std::make_pair(hex_addr, tmp));
    }else{
        InsertRemotePage(tmp);
    }
    v_hex_addr_cache += (granularity);
    v_hex_addr_cache %= (Meta_SRAM.size() * granularity);

}

uint64_t our::AllocCPage(uint64_t hex_addr, Tag *&tag_) {
    std::cerr << "we will never get there " << std::endl;
    AbruptExit(__FILE__, __LINE__);
    return 0;
};

void our::CheckHashPT() {
    uint64_t valid_count = 0;
    for (auto i = hash_page_table.begin(); i != hash_page_table.end(); ++i) {
        if(i->valid)
            valid_count++;
        if(valid_count > hash_page_table.size() / 2){
            std::cerr<<"we forget to release some pte\n";
            AbruptExit(__FILE__, __LINE__);
        }
    }
}

void our::InsertRemotePage(intermediate_data tmp) {
    //invalidate old pte
    if(tmp.rpte->valid && tmp.rpte->pt_index != tmp.pt_index){
        PTentry e;
        tlb.AddTransaction(tmp.rpte->pt_index, true, e);
        CheckHashPT();
    }

    //update RPT
    tmp.rpte->pt_index = tmp.pt_index;
    tmp.rpte->valid = true;

    //write page table
    tlb.AddTransaction(tmp.pt_index, true, tmp.pte);
    uint64_t index = tmp.pte.hex_cache_addr / granularity;
    CacheFrontEnd::DoRefill(tmp.pte.hex_addr_aligned, Meta_SRAM[index], tmp.pte.hex_cache_addr);
}

uint64_t our::GetHexTag(uint64_t hex_addr) {
    return hex_addr / granularity * granularity;
}

void our::WarmUp(uint64_t hex_addr, bool is_write) {
    uint64_t hex_addr_aligned = hex_addr / granularity * granularity;
    uint32_t value[128/32];
    MurmurHash3_x64_128(&hex_addr_aligned, sizeof(uint64_t), 0, value);
    uint32_t pt_index = value[0] % hash_page_table.size();
    //warm up tlb and page table
    PTentry e =  tlb.WarmUp(pt_index, false);

    //hit
    if(e.valid && e.hex_addr_aligned == hex_addr_aligned){
        uint64_t index = e.hex_cache_addr / granularity;
        Meta_SRAM[index].dirty |= is_write;
        return;
    }

    //miss and hash collision
    uint64_t index;
    if(e.valid){
        index = e.hex_cache_addr / granularity;
        e.hex_addr_aligned = hex_addr_aligned;
        tlb.WarmUp(pt_index, true, e);
    }else{
        index = v_hex_addr_cache / granularity;
        //clear old pte
        if(pte_addr_table[index].valid){
            e.valid = false;
            e.hex_cache_addr = e.hex_addr_aligned = 0;
            tlb.WarmUp(pte_addr_table[index].pt_index, true, e);
        }

        //set up new pte
        e.valid = true;
        e.hex_addr_aligned = hex_addr_aligned;
        e.hex_cache_addr = v_hex_addr_cache;
        tlb.WarmUp(pt_index, true, e);

        //update RPT
        pte_addr_table[index].pt_index = pt_index;
        pte_addr_table[index].valid = true;

        v_hex_addr_cache += (granularity);
        v_hex_addr_cache %= (Meta_SRAM.size() * granularity);
    }

    //warm up Meta
    Meta_SRAM[index] = Tag(GetHexTag(hex_addr), true, is_write, granularity);
    if(granularity > 256)
        Meta_SRAM[index].accessed[(hex_addr & granularity_mask) / 256] = true;
}

void our::PrintStat() {

    utilization_file<<"hashmap collision time: "<<collision_times<<"\n"
                    <<"non collision time: "<<non_collision_times<<"\n"
                    <<"TLB hit: "<<tlb.hit_<<"\n"
                    <<"TLB miss: "<<tlb.miss_<<"\n";

    CacheFrontEnd::PrintStat();
}
}