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
    hit_ = 0;
    miss_ = 0;
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
bool SRAMCache<dType>::DRAMReadBack(CacheAddr req_id) {
    if(droped_reqs.find(req_id) != droped_reqs.end()){
        if(droped_reqs[req_id] == 1)
            droped_reqs.erase(req_id);
        else
            droped_reqs[req_id] --;
        return true;
    }

    if(waiting_resp_from_dram.find(req_id) != waiting_resp_from_dram.end()){
        uint64_t index = waiting_resp_from_dram[req_id] % tags.size();
        uint64_t tag = waiting_resp_from_dram[req_id] / tags.size();
        tags[index] = Tag(tag, true, false, 0);
        data[index] = (*data_backup)[waiting_resp_from_dram[req_id]];
        waiting_resp_from_dram.erase(req_id);
        return true;
    }

    return false;
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
    hashmap_hex_addr(Meta_SRAM.size()*4096),
    pte_size(12),
    tlb(hashmap_hex_addr, cache, pte_size, Meta_SRAM.size() * 2 / 64, &hash_page_table),
    rtlb(hashmap_hex_addr + Meta_SRAM.size() * 2 * pte_size, cache, 8, Meta_SRAM.size(), &pte_addr_table),
    hashmap_hex_addr_block_region(hashmap_hex_addr + Meta_SRAM.size() * 2 * pte_size + Meta_SRAM.size() * 8),
    tlb_block_region(hashmap_hex_addr_block_region, cache, pte_size, Meta_SRAM.size() * 16 * 2 / 64, &hpt_block_region),
    rtlb_block_region(hashmap_hex_addr_block_region + Meta_SRAM.size() * 16 * 2 * pte_size, cache, 8,
                      Meta_SRAM.size() * 16, &rpt_block_region){
    std::cout<<"our frontend\n";
    utilization_file.open(output_dir+"/utilization_our");
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
    SRAMCache<PTentry> *tlb_ptr = &tlb_block_region;
    std::vector<Tag> *meta_ptr = &meta_block_region;
    if(is_page_region){
        granularity_ = 4096;
        tlb_ptr = &tlb;
        meta_ptr = &Meta_SRAM;
    }

    uint64_t hex_addr_aligned = (hex_addr / granularity_) * granularity_;
    PTentry pte = tlb_ptr->GetData(pt_index);

    //PT hit
    //TODO: we use tag and hex_addr_aligned as tag for now
    if(pte.valid && pte.hex_addr_aligned == hex_addr_aligned) {
        uint64_t index = pte.hex_cache_addr / granularity_;
        uint64_t tag = hex_addr_aligned;
        //cache hit
        if((*meta_ptr)[index].valid && (*meta_ptr)[index].tag == tag){
            tag_ = &(*meta_ptr)[index];
            hex_addr_cache = index * granularity_ + hex_addr % granularity_;
            return true;
        }else{
            std::cerr<<"we do not have PT hit but cache miss for now\n";
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
    uint64_t hex_addr_remote = hex_addr / 4096 * 4096;
    if(MSHRs.find(hex_addr_remote) != MSHRs.end()){
        MSHRs[hex_addr_remote].emplace_back(Transaction(hex_addr, is_write));
    }else{
        MSHRs[hex_addr_remote] = std::list<Transaction>{Transaction(hex_addr, is_write)};
        LSQ.emplace_back(RemoteRequest(false, hex_addr_remote,
                                   4096 / 64, 0));
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

void our::Refill(uint64_t req_id) {
    pending_req_to_PT_br.emplace_back(req_id);
}

void our::RefillToRegion(intermediate_data tmp) {
    uint64_t granularity = 256;
    uint64_t cpage_head = v_hex_addr_cache_br;
    SRAMCache<PTentry> *tlb_ptr = &tlb_block_region;
    if(tmp.to_page_region){
        granularity = 4096;
        cpage_head = v_hex_addr_cache;
        tlb_ptr = &tlb;
    }
    PTentry pte = tlb_ptr->GetData(tmp.pt_index);
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
            data_.pt_index = pt_index;
            RefillToRegion(data_);
            pending_req_to_PT.pop_front();
        }
    }

    if(!pending_req_to_PT_br.empty() && rtlb_block_region.WillAcceptTransaction()){
        uint32_t value[128/32];
        MurmurHash3_x64_128(&(*pending_req_to_PT_br.begin()), sizeof(uint64_t), 0, value);
        uint32_t pt_index = value[0] % hash_page_table.size();
        uint32_t pt_index_br = value[0] % hpt_block_region.size();
        if(tlb_block_region.AddTransaction(pt_index_br, false)){
            PTentry pte_br = tlb_block_region.GetData(pt_index_br);
            /*
            * the remote page is refill to page region
            * if adjacent block exist or
            * more than 1 reqs waiting in MSHR
            * */
            if(pte_br.valid && (pte_br.hex_addr_aligned / 4096 * 4096 == pending_req_to_PT_br.front())){
                intermediate_data data_(pt_index, pending_req_to_PT_br.front());
                data_.free_br = true;
                data_.pt_index_br = pt_index_br;
                data_.rpt_index_br = pte_br.hex_cache_addr / 256;
                pending_req_to_PT.emplace_back(data_);
                promotion_to_page ++;
            }else if(MSHRs[pending_req_to_PT_br.front()].size() > 1){
                pending_req_to_PT.emplace_back(intermediate_data(pt_index, pending_req_to_PT_br.front()));
            }else{
                refill_to_block ++;
                intermediate_data data_(pt_index_br, pending_req_to_PT_br.front());
                data_.to_page_region = false;
                data_.req_id = MSHRs[pending_req_to_PT_br.front()].front().addr / 256 * 256;
                RefillToRegion(data_);
            }
            pending_req_to_PT_br.pop_front();
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
            //if(is_hit_br){
            //    std::cout<<"hit in block region "<<front_q_br.front().hex_addr<<"\n";
            //}else{
            //    std::cout<<"miss "<<front_q_br.front().hex_addr<<"\n";
            //}
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

    //v_hex_addr_cache += (granularity);
    //v_hex_addr_cache %= hashmap_hex_addr;
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
    SRAMCache<PTentry> *tlb_ptr = &tlb_block_region;
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
            tlb_ptr->AddTransaction(rpte.pt_index, true, PTentry());
        }else{
            //free page occupied by page region
            if(!tmp.to_page_region){
                uint64_t rpt_index_page = tmp.rpt_index / (4096 / 256);
                rtlb.AddTransaction(rpt_index_page, false);
                rpte = rtlb.GetData(rpt_index_page);
                if(rpte.valid){
                    tlb.AddTransaction(rpte.pt_index, true, PTentry());
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
                        tlb_block_region.AddTransaction(rpte.pt_index, true, PTentry());
                        rtlb_block_region.AddTransaction(rpt_index_block, true, RPTentry());
                    }
                }
            }
        }
        //CheckHashPT(tmp.to_page_region);
    }

    //free block that moved to page region
    if(tmp.free_br){
        tlb_block_region.AddTransaction(tmp.pt_index_br, true, PTentry());
        rtlb_block_region.AddTransaction(tmp.rpt_index_br, true, RPTentry());
    }

    //update RPT
    rtlb_ptr->AddTransaction(tmp.rpt_index, true, RPTentry(tmp.pt_index));

    //write page table
    tlb_ptr->AddTransaction(tmp.pt_index, true, tmp.pte);
    uint64_t index = tmp.pte.hex_cache_addr / granularity;
    uint64_t hex_addr_remote = tmp.pte.hex_addr_aligned / 4096 * 4096;
    CacheFrontEnd::DoRefill( hex_addr_remote, (*meta_ptr)[index], tmp.pte.hex_cache_addr, tmp.pte.hex_addr_aligned);
}

uint64_t our::GetHexTag(uint64_t hex_addr) {
    return hex_addr;
}

void our::RefillToRegionWarmUp(intermediate_data tmp, bool is_write) {
    uint64_t granularity = 256;
    uint64_t cpage_head = v_hex_addr_cache_br;
    RPTCache *rtlb_ptr = &rtlb_block_region;
    SRAMCache<PTentry> *tlb_ptr = &tlb_block_region;
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
        if(v_hex_addr_cache_br <= v_hex_addr_cache){
            v_hex_addr_cache_br = hashmap_hex_addr - 256;
            v_hex_addr_cache = 0;
        }

        //clear old pte
        rtlb_ptr->WarmUp(index, false);
        RPTentry rpte = rtlb_ptr->GetData(index);
        if(rpte.valid){
            tlb_ptr->WarmUp(rpte.pt_index, true, PTentry());
        }else{
            //free page occupied by block region
            if(!tmp.to_page_region){
                uint64_t rpt_index_page = tmp.rpt_index / (4096 / 256);
                rtlb.WarmUp(rpt_index_page, false);
                rpte = rtlb.GetData(rpt_index_page);
                if(rpte.valid){
                    tlb.WarmUp(rpte.pt_index, true, PTentry());
                    rtlb.WarmUp(rpt_index_page, true, RPTentry());
                }
            }

            //free blocks occupied by page region
            if(tmp.to_page_region){
                for (int i = 0; i < (4096 / 256); ++i) {
                    uint64_t rpt_index_block = tmp.rpt_index * (4096 / 256) + i;
                    rtlb_block_region.WarmUp(rpt_index_block, false);
                    rpte = rtlb_block_region.GetData(rpt_index_block);
                    if(rpte.valid){
                        tlb_block_region.WarmUp(rpte.pt_index, true, PTentry());
                        rtlb_block_region.WarmUp(rpt_index_block, true, RPTentry());
                    }
                }
            }
        }
    }else{
        tmp.rpt_index = tmp.pte.hex_cache_addr / granularity;
    }

    //free block that moved to page region
    if(tmp.free_br){
        tlb_block_region.WarmUp(tmp.pt_index_br, true, PTentry());
        rtlb_block_region.WarmUp(tmp.rpt_index_br, true, RPTentry());
    }

    //update RPT
    rtlb_ptr->WarmUp(tmp.rpt_index, true, RPTentry(tmp.pt_index));

    //write page table
    tmp.pte.valid = true;
    tmp.pte.hex_addr_aligned = tmp.req_id / granularity * granularity;
    tlb_ptr->AddTransaction(tmp.pt_index, true, tmp.pte);

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
    PTentry e =  tlb.WarmUp(pt_index, false);
    PTentry e_br = tlb_block_region.WarmUp(pt_index_br, false);

    //hit in page region
    if(e.valid && e.hex_addr_aligned == hex_addr_aligned){
        uint64_t index = e.hex_cache_addr / 4096;
        tracker_->AddTransaction(hex_addr % 4096, is_write, Meta_SRAM[index]);
        return;
    }

    //hit in block region
    if(e_br.valid && e_br.hex_addr_aligned == (hex_addr / 256 * 256)){
        uint64_t index = e_br.hex_cache_addr / 256;
        tracker_->AddTransaction(hex_addr % 256, is_write, meta_block_region[index]);
        return;
    }

    //miss and choose region to refill
    intermediate_data data_;
    if(e_br.valid && (e_br.hex_addr_aligned / 4096) == (hex_addr / 4096)){
        data_.to_page_region = true;
        data_.pte = e;
        data_.pt_index = pt_index;
        data_.req_id = hex_addr;
        data_.pt_index_br = pt_index_br;
        data_.rpt_index_br = e_br.hex_cache_addr / 256;
        data_.free_br = true;
        data_.valid = true;
    }else{
        data_.to_page_region = false;
        data_.pte = e_br;
        data_.pt_index = pt_index_br;
        data_.req_id = hex_addr;
        data_.valid = true;
    }
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
        if(dram->JedecDRAMSystem::WillAcceptTransaction(pending_req_to_RPT.front().first, pending_req_to_RPT.front().second)){
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