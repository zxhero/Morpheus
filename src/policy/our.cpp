//
// Created by zhangxu on 4/25/23.
//

#include "our.h"

namespace dramsim3{

SRAMCache::SRAMCache(uint64_t hex_base, JedecDRAMSystem *dram_, uint64_t sz, uint64_t capacity,
                            std::vector<PTentry> *data_backup_)
    : hex_dram_base_addr(hex_base), dType_sz(sz), data_backup(data_backup_), dram(dram_){
    data.resize(capacity);
    for (int i = 0; i < data.size(); ++i) {
        data[i].resize(8);
    }
    tags.resize(capacity, Tag(0, false, false, 0));
    hit_ = 0;
    miss_ = 0;
}

bool SRAMCache::AddTransaction(uint64_t hex_offset, bool is_write, uint64_t offset, PTentry dptr) {
    uint64_t hex_offset_g = hex_offset / 8;
    uint64_t index = hex_offset_g % tags.size();
    uint64_t tag = hex_offset_g / tags.size();

    bool hit = tags[index].tag == tag && tags[index].valid;
    if(is_write && !hit){
        (*data_backup)[hex_offset_g * 8 + offset] = dptr;
        //we do not send write if there is already a read
        uint64_t hex_dram_addr = hex_dram_base_addr + (hex_offset_g * 8) * dType_sz;
        auto ptr = std::find_if(pending_req_to_dram.begin(), pending_req_to_dram.end(),
                        [hex_dram_addr](std::pair<CacheAddr, bool> a){
            return a.first == hex_dram_addr;
        });
        if(ptr != pending_req_to_dram.end()){
            return true;
        }
        //check in-flight read to same offset
        if(waiting_resp_from_dram.find(hex_dram_addr) != waiting_resp_from_dram.end()){
            return true;
        }

        //we write through the data in other situations
        hex_dram_addr = hex_dram_base_addr +
                (hex_offset_g * 8 + offset) * dType_sz;
        pending_req_to_dram.push_back(std::make_pair(hex_dram_addr, true));
        return true;
    }

    if(is_write && hit){
        (*data_backup)[hex_offset_g * 8 + offset] = dptr;
        data[index][offset] = dptr;
        tags[index].dirty = true;
    }

    //read
    if(!hit){
        //if miss already, we do not resend dram read
        uint64_t hex_dram_addr = hex_dram_base_addr + (hex_offset_g * 8) * dType_sz;
        if(waiting_resp_from_dram.find(hex_dram_addr) == waiting_resp_from_dram.end()
        && std::find_if(pending_req_to_dram.begin(), pending_req_to_dram.end(),
                        [hex_dram_addr](std::pair<CacheAddr, bool> a){
            return a.first == hex_dram_addr && !a.second;
        }) == pending_req_to_dram.end()){
            pending_req_to_dram.push_back(std::make_pair(hex_dram_addr, false));
        }
    }

    if(hit){
        if(last_req.find(hex_offset) != last_req.end())
            last_req.erase(hex_offset);
        else
            hit_++;
    }
    else if(last_req.find(hex_offset) == last_req.end()){
        miss_++;
        last_req.insert(hex_offset);
    }
    return hit;
}

void SRAMCache::Drained() {
    if(!pending_req_to_dram.empty()){
        if(dram->JedecDRAMSystem::WillAcceptTransaction(pending_req_to_dram.front().first,
                                                        pending_req_to_dram.front().second)){
            dram->JedecDRAMSystem::AddTransaction(pending_req_to_dram.front().first,
                                                  pending_req_to_dram.front().second);
            if(!pending_req_to_dram.front().second)
                waiting_resp_from_dram[pending_req_to_dram.front().first] =
                    (pending_req_to_dram.front().first - hex_dram_base_addr) / dType_sz / 8;
            pending_req_to_dram.pop_front();
        }
    }
}

bool SRAMCache::DRAMReadBack(CacheAddr req_id) {
    if(waiting_resp_from_dram.find(req_id) != waiting_resp_from_dram.end()){
        uint64_t index = waiting_resp_from_dram[req_id] % tags.size();
        uint64_t tag = waiting_resp_from_dram[req_id] / tags.size();
        if(tags[index].valid && tags[index].dirty){
            uint64_t hex_dram_addr = hex_dram_base_addr +
                ((tags[index].tag * tags.size() + index) * 8) * dType_sz;
            pending_req_to_dram.push_back(std::make_pair(hex_dram_addr, true));
        }
        tags[index] = Tag(tag, true, false, 0);
        for (int i = 0; i < 8; ++i) {
            data[index][i] = (*data_backup)[waiting_resp_from_dram[req_id] * 8 + i];
        }
        waiting_resp_from_dram.erase(req_id);
        return true;
    }

    return false;
}

PTentry SRAMCache::GetData(uint64_t hex_offset, uint64_t &offset) {
    uint64_t hex_offset_g = hex_offset / 8;
    uint64_t index = hex_offset_g % tags.size();
    for (int i = 0; i < 8; ++i) {
        if(!data[index][i].valid){
            offset = i;
            return data[index][i];
        }
    }
    offset = rand() % 8;
    return data[index][offset];
}

bool SRAMCache::GetData(uint64_t hex_offset, uint64_t tag, PTentry &pte) {
    uint64_t hex_offset_g = hex_offset / 8;
    uint64_t index = hex_offset_g % tags.size();
    for (int i = 0; i < 8; ++i) {
        if(data[index][i].valid && data[index][i].hex_addr_aligned == tag){
            pte = data[index][i];
            return true;
        }
    }
    return false;
}

std::vector<PTentry> SRAMCache::GetDataGroup(uint64_t hex_offset) {
    uint64_t hex_offset_g = hex_offset / 8;
    uint64_t index = hex_offset_g % tags.size();
    return data[index];
}

void SRAMCache::WarmUPWrite(uint64_t hex_offset, uint64_t offset, PTentry dptr) {
    uint64_t hex_offset_g = hex_offset / 8;
    uint64_t index = hex_offset_g % tags.size();
    uint64_t tag = hex_offset_g / tags.size();

    (*data_backup)[hex_offset_g * 8 + offset] = dptr;
    for (int i = 0; i < 8; ++i) {
        data[index][i] = (*data_backup)[hex_offset_g * 8 + i];
    }
    tags[index] = Tag(tag, true, true, 0);
}

std::vector<PTentry> SRAMCache::WarmUpRead(uint64_t hex_offset) {
    uint64_t hex_offset_g = hex_offset / 8;
    uint64_t index = hex_offset_g % tags.size();
    uint64_t tag = hex_offset_g / tags.size();
    bool hit = tags[index].tag == tag && tags[index].valid;

    if(!hit){
        tags[index] = Tag(tag, true, false, 0);
        for (int i = 0; i < 8; ++i) {
            data[index][i] = (*data_backup)[hex_offset_g * 8 + i];
        }
    }

    return data[index];
}

our::our(std::string output_dir, JedecDRAMSystem *cache, Config &config):
    CacheFrontEnd(output_dir, cache, config),
    hashmap_hex_addr(Meta_SRAM.size()*4096),
    pte_size(8),
    tlb(hashmap_hex_addr, cache, pte_size, Meta_SRAM.size() * 2 / 64, &hash_page_table),
    rtlb(hashmap_hex_addr + Meta_SRAM.size() * 2 * pte_size, cache, 8, Meta_SRAM.size(), &pte_addr_table),
    hashmap_hex_addr_block_region(hashmap_hex_addr + Meta_SRAM.size() * 2 * pte_size + Meta_SRAM.size() * 8),
    tlb_block_region(hashmap_hex_addr_block_region, cache, pte_size, Meta_SRAM.size() * 16 * 2 / 64, &hpt_block_region),
    rtlb_block_region(hashmap_hex_addr_block_region + Meta_SRAM.size() * 16 * 2 * pte_size, cache, 8,
                      Meta_SRAM.size() * 16, &rpt_block_region){
    std::cout<<"our frontend\n";
    utilization_file.open(output_dir+"/utilization_our_v4_t"+std::to_string(PROMOTION_T));
    if (utilization_file.fail()) {
        std::cerr << "utilization file does not exist" << std::endl;
        AbruptExit(__FILE__, __LINE__);
    }
    v_hex_addr_cache = 0;
    uint32_t virtual_cache_page_num = Meta_SRAM.size();
    hash_page_table.resize(virtual_cache_page_num * 2);
    pte_addr_table.resize(Meta_SRAM.size());
    std::cout<<"hash page table size is "<<hash_page_table.size()<<"\n"
            <<"tlb size is "<<tlb.data.size()<<"\n"
            <<"hash page table base: "<<hashmap_hex_addr<<"\n";

    collision_times = 0;
    non_collision_times = 0;
    refill_to_block = 0;
    refill_to_page = 0;
    go_back_to_head = 0;
    promotion_to_page = 0;
    wasted_block = 0;
    for (int i = 0; i < 16; ++i) {
        region_capacity_ratio[i] = 0;
    }

    tracker_ = new OurTracker(this);

    v_hex_addr_cache_br = hashmap_hex_addr - 256;
    meta_block_region.resize(Meta_SRAM.size() * 16, Tag(0, false, false, 256));
    hpt_block_region.resize(Meta_SRAM.size() * 16 * 2);
    rpt_block_region.resize(Meta_SRAM.size() * 16);
    std::cout<<"HPT in block region size is "<<hpt_block_region.size()<<"\n"
            <<"tlb size is "<<tlb_block_region.data.size()<<"\n"
            <<"hash page table base: "<<hashmap_hex_addr_block_region<<"\n";
}

void our::HashReadCallBack(uint64_t req_id) {
    if(rtlb.DRAMReadBack(req_id)){
        return;
    }

    if(tlb.DRAMReadBack(req_id))
        return;

    if(rtlb_block_region.DRAMReadBack(req_id))
        return;

    if(tlb_block_region.DRAMReadBack(req_id))
        return;

    std::cerr << "$DRAM read without destination "<<req_id << std::endl;
    AbruptExit(__FILE__, __LINE__);
}

//we get here only after hit in tlb
bool our::GetTag(uint64_t hex_addr, Tag *&tag_, uint64_t &hex_addr_cache, uint32_t pt_index, bool is_page_region) {
    uint64_t granularity_ = 256;
    SRAMCache *tlb_ptr = &tlb_block_region;
    std::vector<Tag> *meta_ptr = &meta_block_region;
    if(is_page_region){
        granularity_ = 4096;
        tlb_ptr = &tlb;
        meta_ptr = &Meta_SRAM;
    }

    uint64_t hex_addr_aligned = (hex_addr / granularity_) * granularity_;
    PTentry pte;

    //PT hit
    //TODO: we use tag and hex_addr_aligned as tag for now
    if(tlb_ptr->GetData(pt_index, hex_addr_aligned, pte)) {
        uint64_t index = pte.hex_cache_addr / granularity_;
        uint64_t tag = hex_addr_aligned;
        //cache hit
        if((*meta_ptr)[index].valid && (*meta_ptr)[index].tag == tag){
            tag_ = &(*meta_ptr)[index];
            hex_addr_cache = index * granularity_ + hex_addr % granularity_;
            return true;
        }else{
            std::cerr<<"we do not have PT hit but cache miss for now "<<hex_addr<<"\n";
            AbruptExit(__FILE__, __LINE__);
        }
    }

    //PT miss
    return false;
}

bool our::GetTag(uint64_t hex_addr, Tag *&tag_, uint64_t &hex_addr_cache) {
    std::cerr << "we will never get there GetTag" << std::endl;
    AbruptExit(__FILE__, __LINE__);
    return false;
}

/*
 * We currently read page from remote
 * */
void our::MissHandler(uint64_t hex_addr, bool is_write) {
    MSHR_sz ++;
    bool is_first = false;
    uint64_t hex_addr_page = hex_addr / 4096 * 4096;
    uint64_t hex_addr_block = hex_addr / 256 * 256;
    auto mshr_ptr = std::find_if(CacheFrontEnd::MSHRs.begin(), CacheFrontEnd::MSHRs.end(),
                                 [hex_addr_page](std::pair<uint64_t, std::list<Transaction>> a){
        return a.first / 4096 * 4096 == hex_addr_page;
    });
    if(mshr_ptr == CacheFrontEnd::MSHRs.end()){
        is_first = true;
    }

    uint32_t value[128/32];
    MurmurHash3_x64_128(&(hex_addr_page), sizeof(uint64_t), 0, value);
    if(is_first){
        CacheFrontEnd::MSHRs[hex_addr_block] = std::list<Transaction>{Transaction(hex_addr, is_write)};
        fetch_engine_q.emplace_back(intermediate_req(hex_addr, is_write, value[0]));
        return;
    }

    auto mshr_ptr_2 = std::find_if(MSHRs.begin(), MSHRs.end(),
                                 [hex_addr_page](std::pair<uint64_t, MSHR> a){
        return a.first / 4096 * 4096 == hex_addr_page;
    });

    if(mshr_ptr_2 != MSHRs.end() && mshr_ptr_2->second.is_page_region){
        CacheFrontEnd::MSHRs[hex_addr_page].emplace_back(Transaction(hex_addr, is_write));
    }else if(mshr_ptr_2 != MSHRs.end()){
        //send 256
        /*
        * the remote page is fetched and refill to page region
        * if more than 1 reqs waiting in MSHR
        * */
        //resend a req to fetch page
        LSQ.emplace_back(RemoteRequest(false, hex_addr_page, 4096 / 64, 0));
        auto reqs = mshr_ptr->second;
        //remove old MSHR
        CacheFrontEnd::MSHRs.erase(mshr_ptr);
        MSHRs.erase(mshr_ptr_2);
        //set up new MSHR
        MSHRs[hex_addr_page] = MSHR(true, value[0] % hash_page_table.size());
        reqs.emplace_back(Transaction(hex_addr, is_write));
        CacheFrontEnd::MSHRs[hex_addr_page] = reqs;
    }else{
        //the block data has already returned
        if(std::find_if(pending_req_to_PT_br.begin(), pending_req_to_PT_br.end(), [hex_addr_page](intermediate_data d){
            return d.req_id / 4096 * 4096 == hex_addr_page;
        }) != pending_req_to_PT_br.end()){
            std::cerr<<"the adjacent block data has already returned "<<hex_addr<<"\n";
            //AbruptExit(__FILE__, __LINE__);
        }
        //not send
        mshr_ptr->second.emplace_back(Transaction(hex_addr, is_write));
    }
}

void our::WriteBackData(Tag tag_, uint64_t hex_addr_cache) {
    LSQ.emplace_back(
        RemoteRequest(true,
                      tag_.tag,
                      tag_.granularity / 64,
                      0
        )
    );
}

bool our::CheckOtherBuffer(uint64_t hex_addr, bool is_write) {
    uint64_t hex_addr_remote = hex_addr / 4096 * 4096;
    uint64_t hex_addr_block = hex_addr / 256 * 256;
    if(MSHRs.find(hex_addr_remote) != MSHRs.end()){
        for (auto i = MSHRs[hex_addr_remote].pte_br.begin(); i != MSHRs[hex_addr_remote].pte_br.end(); ++i) {
            if(i->valid && i->hex_addr_aligned == hex_addr_block){
                MSHRs[hex_addr_remote].is_dirty |= is_write;
                return true;
            }
        }
    }

    //we set commit point for returned data as modification on MSHR or MissHandler
    if(std::find_if(pending_req_to_PT_br.begin(), pending_req_to_PT_br.end(), [hex_addr_block](intermediate_data a){
        return a.req_id == hex_addr_block;
    }) != pending_req_to_PT_br.end()){
        return true;
    }

    if(std::find_if(pending_req_to_PT.begin(), pending_req_to_PT.end(), [hex_addr_remote](intermediate_data a){
        return a.req_id == hex_addr_remote;
    }) != pending_req_to_PT.end()){
        return true;
    }

    return false;
}

void our::Refill(uint64_t req_id) {
    if(MSHRs.find(req_id) != MSHRs.end()){
        if(MSHRs[req_id].is_page_region){
            pending_req_to_PT.emplace_back(intermediate_data(MSHRs[req_id].pt_index, req_id));
            //update fetched page if data is dirty
            //if(MSHRs[req_id].is_dirty && MSHRs[req_id].pte_br.valid){
            //    CacheFrontEnd::MSHRs[req_id].emplace_back(
            //            Transaction(MSHRs[req_id].pte_br.hex_addr_aligned, true));
            //}
        }else{
            pending_req_to_PT_br.emplace_back(intermediate_data(MSHRs[req_id].pt_index, req_id));
        }
        MSHRs.erase(req_id);
    }else{
        //the req has been promoted to page region
        wasted_block ++;
    }
}

void our::RefillToRegion(intermediate_data tmp) {
    uint64_t granularity = 256;
    uint64_t cpage_head = v_hex_addr_cache_br;
    SRAMCache *tlb_ptr = &tlb_block_region;
    if(tmp.to_page_region){
        granularity = 4096;
        cpage_head = v_hex_addr_cache;
        tlb_ptr = &tlb;
    }
    PTentry pte = tlb_ptr->GetData(tmp.pt_index, tmp.offset);
    //hash collision
    if(pte.valid){
        collision_times ++;
        tmp.is_collision = true;
        tmp.rpt_index = pte.hex_cache_addr / granularity;
        tmp.pte = pte;
        tmp.pte.hex_addr_aligned = tmp.req_id;
        //in case the collided cpage is at the head of fifo
        if(cpage_head == tmp.pte.hex_cache_addr){
            ProceedToNextFree(tmp.to_page_region);
        }
        InsertRemotePage(tmp);
    }else{
        non_collision_times ++;
        AllocCPage(tmp);
    }
}

void our::ProceedToNextFree(bool is_page_region) {
    if(is_page_region)
        v_hex_addr_cache += (4096);
    else
        v_hex_addr_cache_br -= 256;
    if(v_hex_addr_cache_br < (v_hex_addr_cache + 4096)){
        uint64_t page_region_sz = v_hex_addr_cache;
        int ratio = page_region_sz / (hashmap_hex_addr / (uint64_t)16);
        region_capacity_ratio[ratio] ++;

        go_back_to_head++;
        v_hex_addr_cache_br = hashmap_hex_addr - 256;
        v_hex_addr_cache = 0;
    }
}

void our::Drained() {
    tlb.Drained();
    rtlb.Drained();
    tlb_block_region.Drained();
    rtlb_block_region.Drained();

    if(!pending_req_to_PT.empty() && rtlb.WillAcceptTransaction()){
        uint32_t pt_index = pending_req_to_PT.front().pt_index;
        if(tlb.AddTransaction(pt_index, false)){
            refill_to_page++;
            intermediate_data data_ = pending_req_to_PT.front();
            RefillToRegion(data_);
            pending_req_to_PT.pop_front();
        }
    }

    if(!pending_req_to_PT_br.empty() && rtlb_block_region.WillAcceptTransaction()){
        uint32_t pt_index_br = pending_req_to_PT_br.front().pt_index;
        if(tlb_block_region.AddTransaction(pt_index_br, false)){
            refill_to_block ++;
            intermediate_data data_ = pending_req_to_PT_br.front();
            data_.to_page_region = false;
            RefillToRegion(data_);
            pending_req_to_PT_br.pop_front();
        }
    }

    if(!fetch_engine_q.empty()){
        uint32_t pt_index_br = fetch_engine_q.front().pt_index_br % hpt_block_region.size();
        uint32_t pt_index = fetch_engine_q.front().pt_index_br % hash_page_table.size();
        if(tlb_block_region.AddTransaction(pt_index_br, false)){
            std::vector<PTentry> pte_g = tlb_block_region.GetDataGroup(pt_index_br);
            uint64_t hex_addr_page = fetch_engine_q.front().hex_addr / 4096 * 4096;
            uint64_t hex_addr_block = fetch_engine_q.front().hex_addr / 256 * 256;
            /*
            * the remote page is fetched and refill to page region
            * if #adjacent block > 2
            * */
            bool send_page = false;
            bool promoted = false;

            if(CacheFrontEnd::MSHRs[hex_addr_block].size() > 1){
                send_page = true;
            }else{
                uint64_t num = std::count_if(pte_g.begin(), pte_g.end(), [hex_addr_page](PTentry a){
                    if(a.valid && (a.hex_addr_aligned / 4096 * 4096 == hex_addr_page))
                        return true;
                    return false;
                });
                if(num > PROMOTION_T){
                    promotion_to_page ++;
                    send_page = true;
                    promoted = true;
                }
            }

            if(send_page){
                //send a req to fetch page
                LSQ.emplace_back(RemoteRequest(false, hex_addr_page, 4096 / 64, 0));
                auto reqs = CacheFrontEnd::MSHRs[hex_addr_block];
                //remove old MSHR
                CacheFrontEnd::MSHRs.erase(hex_addr_block);
                //set up new MSHR
                MSHRs[hex_addr_page] = MSHR(true, pt_index);
                CacheFrontEnd::MSHRs[hex_addr_page] = reqs;
            }

            if(send_page && promoted){
                //invalidate data in block region
                for (int i = 0; i < pte_g.size(); ++i) {
                    if(!(pte_g[i].valid && (pte_g[i].hex_addr_aligned / 4096 * 4096 == hex_addr_page)))
                        continue;

                    MSHRs[hex_addr_page].pte_br.emplace_back(pte_g[i]);
                    MSHRs[hex_addr_page].is_dirty |= meta_block_region[pte_g[i].hex_cache_addr / 256].dirty;
                    tlb_block_region.AddTransaction(pt_index_br, true, i, PTentry());
                    rtlb_block_region.AddTransaction(pte_g[i].hex_cache_addr / 256, true, RPTentry());
                }
            }

            if(!send_page){
                MSHRs[hex_addr_block] = MSHR(false, pt_index_br);
                LSQ.emplace_back(RemoteRequest(false, hex_addr_block, 256 / 64, 0));
            }
            fetch_engine_q.pop_front();
        }
    }

    if(!pending_req_to_Meta.empty()){
        if(CacheFrontEnd::ProcessOneReq(pending_req_to_Meta.front().hex_addr, pending_req_to_Meta.front().is_write,
                                        pending_req_to_Meta.front().t, pending_req_to_Meta.front().hex_addr_cache,
                                        pending_req_to_Meta.front().is_hit)){
            pending_req_to_Meta.pop_front();
        }
    }

    if(!front_q_br.empty()){
        uint32_t pt_index_br = front_q_br.front().pt_index_br;
        if(tlb_block_region.AddTransaction(pt_index_br, false)){
            Tag *t_br = NULL;
            uint64_t hex_addr_cache_br;
            bool is_hit_br = GetTag(front_q_br.front().hex_addr, t_br, hex_addr_cache_br, pt_index_br, false);
            pending_req_to_Meta.emplace_back(intermediate_req(t_br, hex_addr_cache_br,
                                                              front_q_br.front().hex_addr, front_q_br.front().is_write,
                                                              is_hit_br));
            front_q_br.pop_front();
        }
    }

    if(!front_q.empty()){
        uint64_t hex_addr = (front_q.front().first / 4096) * 4096;
        uint32_t value[128/32];
        MurmurHash3_x64_128(&hex_addr, sizeof(uint64_t), 0, value);
        uint32_t pt_index = value[0] % hash_page_table.size();
        uint32_t pt_index_br = value[0] % hpt_block_region.size();
        if(tlb.AddTransaction(pt_index, false)){
            Tag *t = NULL;
            uint64_t hex_addr_cache;
            bool is_hit = GetTag(front_q.front().first, t, hex_addr_cache, pt_index, true);

            if(is_hit){
                pending_req_to_Meta.emplace_back(intermediate_req(t, hex_addr_cache,
                                                              front_q.front().first, front_q.front().second, is_hit));
                //std::cout<<"hit in page region "<<front_q.front().first<<"\n";
            }
            else
                front_q_br.emplace_back(intermediate_req(front_q.front().first, front_q.front().second, pt_index_br));
            front_q.erase(front_q.begin());
        }
    }

    CacheFrontEnd::ProcessRefillReq();
}

void our::AllocCPage(intermediate_data tmp) {
    uint64_t cpage_head = v_hex_addr_cache_br;
    uint64_t granularity = 256;
    RPTCache *rtlb_ptr = &rtlb_block_region ;
    if(tmp.to_page_region){
        cpage_head = v_hex_addr_cache;
        granularity = 4096;
        rtlb_ptr = &rtlb;
    }
    uint64_t index = cpage_head / granularity;
    tmp.rpt_index = index;
    tmp.pte = PTentry(tmp.req_id, cpage_head);

    rtlb_ptr->AddTransaction(index, false);
    InsertRemotePage(tmp);
    ProceedToNextFree(tmp.to_page_region);
}

uint64_t our::AllocCPage(uint64_t hex_addr, Tag *&tag_) {
    std::cerr << "we will never get there " << std::endl;
    AbruptExit(__FILE__, __LINE__);
    return 0;
};

void our::CheckHashPT(bool is_page_region) {
    std::vector<PTentry> *hpt = &hpt_block_region;
    if(is_page_region)
        hpt = &hash_page_table;

    uint64_t valid_count = 0;
    for (auto i = hpt->begin(); i != hpt->end(); ++i) {
        if(i->valid)
            valid_count++;
        if(valid_count > hpt->size() / 2){
            std::cerr<<"we forget to release some pte of "<<(is_page_region ? "page region\n" : "block region\n");
            AbruptExit(__FILE__, __LINE__);
        }
    }
}

void our::InsertRemotePage(intermediate_data tmp) {
    //std::cout<<"refill "<<tmp.pte.hex_addr_aligned<<"to "<<(tmp.to_page_region ? "page region" : "block region")
   //         <<"at "<<tmp.pte.hex_cache_addr<<"\n";
    uint64_t granularity = 256;
    RPTCache *rtlb_ptr = &rtlb_block_region;
    SRAMCache *tlb_ptr = &tlb_block_region;
    std::vector<Tag> *meta_ptr = &meta_block_region;
    if(tmp.to_page_region){
        granularity = 4096;
        rtlb_ptr = &rtlb;
        tlb_ptr = &tlb;
        meta_ptr = &Meta_SRAM;
    }

    //invalidate old pte
    if(!tmp.is_collision){
        RPTentry rpte = rtlb_ptr->GetData(tmp.rpt_index);
        if(rpte.valid){
            tlb_ptr->AddTransaction(rpte.pt_index, true, rpte.offset, PTentry());
        }else{
            //free page occupied by page region
            if(!tmp.to_page_region){
                uint64_t rpt_index_page = tmp.rpt_index / (4096 / 256);
                rtlb.AddTransaction(rpt_index_page, false);
                rpte = rtlb.GetData(rpt_index_page);
                if(rpte.valid){
                    tlb.AddTransaction(rpte.pt_index, true, rpte.offset, PTentry());
                    rtlb.AddTransaction(rpt_index_page, true, RPTentry());
                }
            }

            //free blocks occupied by block region
            if(tmp.to_page_region){
                for (int i = 0; i < (4096 / 256); ++i) {
                    uint64_t rpt_index_block = tmp.rpt_index * (4096 / 256) + i;
                    rtlb_block_region.AddTransaction(rpt_index_block, false);
                    rpte = rtlb_block_region.GetData(rpt_index_block);
                    if(rpte.valid){
                        tlb_block_region.AddTransaction(rpte.pt_index, true, rpte.offset, PTentry());
                        rtlb_block_region.AddTransaction(rpt_index_block, true, RPTentry());
                    }
                }
            }
        }
        //CheckHashPT(tmp.to_page_region);
    }

    //update RPT
    rtlb_ptr->AddTransaction(tmp.rpt_index, true, RPTentry(tmp.pt_index, tmp.offset));

    //write page table
    tlb_ptr->AddTransaction(tmp.pt_index, true, tmp.offset, tmp.pte);
    uint64_t index = tmp.pte.hex_cache_addr / granularity;
    uint64_t hex_addr_remote = tmp.pte.hex_addr_aligned;
    CacheFrontEnd::DoRefill( hex_addr_remote, (*meta_ptr)[index], tmp.pte.hex_cache_addr, tmp.pte.hex_addr_aligned);
}

uint64_t our::GetHexTag(uint64_t hex_addr) {
    return hex_addr;
}

void our::RefillToRegionWarmUp(intermediate_data tmp, bool is_write) {
    uint64_t granularity = 256;
    uint64_t cpage_head = v_hex_addr_cache_br;
    RPTCache *rtlb_ptr = &rtlb_block_region;
    SRAMCache *tlb_ptr = &tlb_block_region;
    std::vector<Tag> *meta_ptr = &meta_block_region;
    if(tmp.to_page_region){
        granularity = 4096;
        cpage_head = v_hex_addr_cache;
        rtlb_ptr = &rtlb;
        tlb_ptr = &tlb;
        meta_ptr = &Meta_SRAM;
    }

    if(!tmp.pte.valid){
        uint64_t index = cpage_head / granularity;
        tmp.rpt_index = index;
        tmp.pte.hex_cache_addr = cpage_head;
        if(tmp.to_page_region)
            v_hex_addr_cache += (4096);
        else
            v_hex_addr_cache_br -= 256;
        if(v_hex_addr_cache_br < (v_hex_addr_cache + 4096)){
            v_hex_addr_cache_br = hashmap_hex_addr - 256;
            v_hex_addr_cache = 0;
        }

        //clear old pte
        rtlb_ptr->WarmUp(index, false);
        RPTentry rpte = rtlb_ptr->GetData(index);
        if(rpte.valid){
            tlb_ptr->WarmUPWrite(rpte.pt_index, rpte.offset, PTentry());
        }else{
            //free page occupied by page region
            if(!tmp.to_page_region){
                uint64_t rpt_index_page = tmp.rpt_index / (4096 / 256);
                rtlb.WarmUp(rpt_index_page, false);
                rpte = rtlb.GetData(rpt_index_page);
                if(rpte.valid){
                    tlb.WarmUPWrite(rpte.pt_index, rpte.offset, PTentry());
                    rtlb.WarmUp(rpt_index_page, true, RPTentry());
                }
            }

            //free blocks occupied by block region
            if(tmp.to_page_region){
                for (int i = 0; i < (4096 / 256); ++i) {
                    uint64_t rpt_index_block = tmp.rpt_index * (4096 / 256) + i;
                    rtlb_block_region.WarmUp(rpt_index_block, false);
                    rpte = rtlb_block_region.GetData(rpt_index_block);
                    if(rpte.valid){
                        tlb_block_region.WarmUPWrite(rpte.pt_index, rpte.offset, PTentry());
                        rtlb_block_region.WarmUp(rpt_index_block, true, RPTentry());
                    }
                }
            }
        }
    }else{
        tmp.rpt_index = tmp.pte.hex_cache_addr / granularity;
    }

    //update RPT
    rtlb_ptr->WarmUp(tmp.rpt_index, true, RPTentry(tmp.pt_index, tmp.offset));

    //write page table
    tmp.pte.valid = true;
    tmp.pte.hex_addr_aligned = tmp.req_id / granularity * granularity;
    tlb_ptr->WarmUPWrite(tmp.pt_index, tmp.offset, tmp.pte);

    //warm up Meta
    tracker_->ResetTag(((*meta_ptr)[tmp.rpt_index]), tmp.req_id / granularity * granularity);
    tracker_->AddTransaction(tmp.req_id % granularity, is_write, (*meta_ptr)[tmp.rpt_index]);
}

void our::WarmUp(uint64_t hex_addr, bool is_write) {
    uint64_t hex_addr_aligned = hex_addr / 4096 * 4096;
    uint32_t value[128/32];
    MurmurHash3_x64_128(&hex_addr_aligned, sizeof(uint64_t), 0, value);
    uint32_t pt_index = value[0] % hash_page_table.size();
    uint32_t pt_index_br = value[0] % hpt_block_region.size();
    //warm up tlb and page table
    std::vector<PTentry> e =  tlb.WarmUpRead(pt_index);
    std::vector<PTentry> e_br = tlb_block_region.WarmUpRead(pt_index_br);

    //hit in page region
    for (int i = 0; i < e.size(); ++i) {
        if(e[i].valid && e[i].hex_addr_aligned == hex_addr_aligned){
            uint64_t index = e[i].hex_cache_addr / 4096;
            tracker_->AddTransaction(hex_addr % 4096, is_write, Meta_SRAM[index]);
            return;
        }
    }

    //hit in block region
    uint64_t adjacent_blocks = 0;
    for (int i = 0; i < e_br.size(); ++i) {
        if(e_br[i].valid && e_br[i].hex_addr_aligned == (hex_addr / 256 * 256)){
            uint64_t index = e_br[i].hex_cache_addr / 256;
            tracker_->AddTransaction(hex_addr % 256, is_write, meta_block_region[index]);
            return;
        }else if(e_br[i].valid && (e_br[i].hex_addr_aligned / 4096) == (hex_addr / 4096)){
            adjacent_blocks ++;
        }
    }

    //miss and choose region to refill
    intermediate_data data_;
    if(adjacent_blocks > PROMOTION_T){
        //free block that moved to page region
        for (int i = 0; i < e_br.size(); ++i) {
            if(e_br[i].valid && (e_br[i].hex_addr_aligned / 4096) == (hex_addr / 4096)){
                tlb_block_region.WarmUPWrite(pt_index_br, i, PTentry());
                rtlb_block_region.WarmUp(e_br[i].hex_cache_addr / 256, true, RPTentry());
            }
        }

        data_.to_page_region = true;
        data_.offset = rand() % e.size();
        data_.pte = e[data_.offset];
        data_.pt_index = pt_index;
    }else{
        data_.to_page_region = false;
        data_.offset = rand() % e_br.size();
        data_.pte = e_br[data_.offset];
        data_.pt_index = pt_index_br;
    }
    data_.req_id = hex_addr;
    data_.valid = true;
    RefillToRegionWarmUp(data_, is_write);
}

void our::PrintStat() {

    utilization_file<<"# hashmap collision time: "<<collision_times<<"\n"
                    <<"# non collision time: "<<non_collision_times<<"\n"
                    <<"# page region TLB hit: "<<tlb.hit_<<"\n"
                    <<"# TLB miss: "<<tlb.miss_<<"\n"
                    <<"# RTLB hit: "<<rtlb.hit_<<"\n"
                    <<"# RTLB miss: "<<rtlb.miss_<<"\n"
                    <<"# block region TLB hit: "<<tlb_block_region.hit_<<"\n"
                    <<"# TLB miss: "<<tlb_block_region.miss_<<"\n"
                    <<"# RTLB hit: "<<rtlb_block_region.hit_<<"\n"
                    <<"# RTLB miss: "<<rtlb_block_region.miss_<<"\n"
                    <<"# times of refilling to page: "<<refill_to_page<<"\n"
                    <<"# times of promotion to page: "<<promotion_to_page<<"\n"
                    <<"# times of refilling to block: "<<refill_to_block<<"\n"
                    <<"# num of wasted block: "<<wasted_block<<"\n"
                    <<"# times of go back to head: "<<go_back_to_head<<"\n";

    std::cout<<"the distribution of the ratio of page region: \n";
    for (int i = 0; i < 16; ++i) {
        std::cout<<i+1<<"\t"<<region_capacity_ratio[i]<<"\n";
    }

    CacheFrontEnd::PrintStat();
}

RPTCache::RPTCache(uint64_t hex_base, JedecDRAMSystem *dram_, uint64_t sz, uint64_t capacity,
                   std::vector<RPTentry> *data_backup_):
                   RPT_hex_addr(hex_base), rpte_size(sz), RPT_hex_addr_h(hex_base + capacity * sz){
    data_backup = data_backup_;
    dram = dram_;
    max_tag_sz = 4;
    hit_ = 0;
    miss_ = 0;
    std::cout<<"RPT base: "<<RPT_hex_addr<<"\n"
            <<"RPT high: "<<RPT_hex_addr_h<<"\n";
}

void RPTCache::AddTransaction(uint64_t hex_offset, bool is_write, RPTentry dptr) {
    uint64_t hex_addr = RPT_hex_addr + hex_offset * rpte_size;
    //if(hex_addr == 67591744){
    //    std::cerr<<"debug_from here\n";
    //}
    if(is_write){
        //update RPT
        (*data_backup)[hex_offset] = dptr;
        if(tag.end() == std::find(tag.begin(), tag.end(), hex_offset * rpte_size / 64))
            pending_req_to_RPT.emplace_back(std::make_pair(hex_addr, true));
    }else{
        if(std::find(tag.begin(), tag.end(), hex_offset * rpte_size / 64) == tag.end()){
            miss_++;
            pending_req_to_RPT.emplace_back(std::make_pair(hex_addr, false));
            if(tag.size() == max_tag_sz){
                CacheAddr hex_addr_cache = RPT_hex_addr + (tag.front()) * 64;
                pending_req_to_RPT.emplace_back(std::make_pair(hex_addr_cache, true));
                tag.pop_front();
            }
            tag.emplace_back(hex_offset * rpte_size / 64);
        }else{
            hit_++;
        }
    }
}

bool RPTCache::WillAcceptTransaction() {
    return pending_req_to_RPT.empty() && waiting_resp_RPT.empty();
}

void RPTCache::Drained() {
    if(!pending_req_to_RPT.empty()){
        if(pending_req_to_RPT.front().second &&
        waiting_resp_RPT.find(pending_req_to_RPT.front().first) != waiting_resp_RPT.end()){
            return;
        }

        if(dram->JedecDRAMSystem::WillAcceptTransaction(pending_req_to_RPT.front().first,
                                                        pending_req_to_RPT.front().second)){
            dram->JedecDRAMSystem::AddTransaction(pending_req_to_RPT.front().first, pending_req_to_RPT.front().second);
            if(!pending_req_to_RPT.front().second)
                waiting_resp_RPT.insert(pending_req_to_RPT.front().first);
            pending_req_to_RPT.pop_front();
        }
    }
}

bool RPTCache::DRAMReadBack(CacheAddr req_id) {
    if(waiting_resp_RPT.find(req_id) != waiting_resp_RPT.end()){
        waiting_resp_RPT.erase(req_id);
        return true;
    }

    return false;
}

RPTentry RPTCache::GetData(uint64_t hex_offset) {
    return (*data_backup)[hex_offset];
}

RPTentry RPTCache::WarmUp(uint64_t hex_offset, bool is_write, RPTentry dptr) {
    if(!is_write){
        if(tag.size() == max_tag_sz)
            tag.pop_front();
        tag.emplace_back(hex_offset * rpte_size / 64);
    }else{
        (*data_backup)[hex_offset] = dptr;
    }
    return (*data_backup)[hex_offset];
}
}