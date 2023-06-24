//
// Created by zhangxu on 4/25/23.
//

#include "our.h"

namespace dramsim3{

SRAMCache::SRAMCache(uint64_t hex_base, JedecDRAMSystem *dram_, uint64_t sz, uint64_t capacity,
                            std::vector<PTentry> *data_backup_)
    : hex_dram_base_addr(hex_base), dType_sz(sz),
    assoc(64 / sz), data_backup(data_backup_), dram(dram_){
    data.resize(capacity / sz / assoc);
    for (int i = 0; i < data.size(); ++i) {
        data[i].resize(assoc);
    }
    tags.resize(capacity / sz / assoc, Tag(0, false, false, 0));
    hit_ = 0;
    miss_ = 0;
}

bool SRAMCache::AddTransaction(uint64_t hex_offset, bool is_write, uint64_t offset, PTentry dptr) {
    uint64_t hex_offset_g = hex_offset / assoc;
    uint64_t index = hex_offset_g % tags.size();
    uint64_t tag = hex_offset_g / tags.size();

    bool hit = tags[index].tag == tag && tags[index].valid;
    if(is_write && !hit){
        (*data_backup)[hex_offset_g * assoc + offset] = dptr;
        //we do not send write if there is already a read
        uint64_t hex_dram_addr = hex_dram_base_addr + (hex_offset_g * assoc) * dType_sz;
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
                (hex_offset_g * assoc + offset) * dType_sz;
        pending_req_to_dram.push_back(std::make_pair(hex_dram_addr, true));
        return true;
    }

    if(is_write && hit){
        (*data_backup)[hex_offset_g * assoc + offset] = dptr;
        data[index][offset] = dptr;
        tags[index].dirty = true;
    }

    //read
    if(!hit){
        //if miss already, we do not resend dram read
        uint64_t hex_dram_addr = hex_dram_base_addr + (hex_offset_g * assoc) * dType_sz;
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
                    (pending_req_to_dram.front().first - hex_dram_base_addr) / dType_sz / assoc;
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
                ((tags[index].tag * tags.size() + index) * assoc) * dType_sz;
            pending_req_to_dram.push_back(std::make_pair(hex_dram_addr, true));
        }
        tags[index] = Tag(tag, true, false, 0);
        for (int i = 0; i < assoc; ++i) {
            data[index][i] = (*data_backup)[waiting_resp_from_dram[req_id] * assoc + i];
        }
        waiting_resp_from_dram.erase(req_id);
        return true;
    }

    return false;
}

PTentry SRAMCache::GetData(uint64_t hex_offset, uint64_t &offset) {
    uint64_t hex_offset_g = hex_offset / assoc;
    uint64_t index = hex_offset_g % tags.size();
    for (int i = 0; i < assoc; ++i) {
        if(!data[index][i].valid){
            offset = i;
            return data[index][i];
        }
    }
    offset = rand() % assoc;
    return data[index][offset];
}

bool SRAMCache::GetData(uint64_t hex_offset, uint64_t tag, PTentry &pte) {
    uint64_t hex_offset_g = hex_offset / assoc;
    uint64_t index = hex_offset_g % tags.size();
    for (int i = 0; i < assoc; ++i) {
        if(data[index][i].valid && data[index][i].hex_addr_aligned == tag){
            pte = data[index][i];
            return true;
        }
    }
    return false;
}

std::vector<PTentry> SRAMCache::GetDataGroup(uint64_t hex_offset) {
    uint64_t hex_offset_g = hex_offset / assoc;
    uint64_t index = hex_offset_g % tags.size();
    return data[index];
}

void SRAMCache::WarmUPWrite(uint64_t hex_offset, uint64_t offset, PTentry dptr) {
    uint64_t hex_offset_g = hex_offset / assoc;
    uint64_t index = hex_offset_g % tags.size();
    uint64_t tag = hex_offset_g / tags.size();

    (*data_backup)[hex_offset_g * assoc + offset] = dptr;
    for (int i = 0; i < assoc; ++i) {
        data[index][i] = (*data_backup)[hex_offset_g * assoc + i];
    }
    tags[index] = Tag(tag, true, true, 0);
}

std::vector<PTentry> SRAMCache::WarmUpRead(uint64_t hex_offset) {
    uint64_t hex_offset_g = hex_offset / assoc;
    uint64_t index = hex_offset_g % tags.size();
    uint64_t tag = hex_offset_g / tags.size();
    bool hit = tags[index].tag == tag && tags[index].valid;

    if(!hit){
        tags[index] = Tag(tag, true, false, 0);
        for (int i = 0; i < assoc; ++i) {
            data[index][i] = (*data_backup)[hex_offset_g * assoc + i];
        }
    }

    return data[index];
}

our::our(std::string output_dir, JedecDRAMSystem *cache, Config &config):
    CacheFrontEnd(output_dir, cache, config),
    hashmap_hex_addr(Meta_SRAM.size()*4096),
    pte_size(16),
    hpt_sz_ratio(config.hpt_ratio),
    mwl(config.mwl),
    tlb(hashmap_hex_addr, cache, pte_size, 64 * 1024, &hash_page_table),
    rtlb(hashmap_hex_addr + Meta_SRAM.size() * hpt_sz_ratio * pte_size, cache, 8, Meta_SRAM.size(), &pte_addr_table),
    hashmap_hex_addr_block_region(hashmap_hex_addr + Meta_SRAM.size() * hpt_sz_ratio * pte_size + Meta_SRAM.size() * 8),
    tlb_block_region(hashmap_hex_addr_block_region, cache, pte_size, 1 * 1024 * 1024, &hpt_block_region),
    rtlb_block_region(hashmap_hex_addr_block_region + Meta_SRAM.size() * 16 * hpt_sz_ratio * pte_size, cache, 8,
                      Meta_SRAM.size() * 16, &rpt_block_region){
    std::cout<<"our frontend\n";
    std::string name_suffix = std::to_string(config.ratio)+"_"+std::to_string(hpt_sz_ratio)+"_"+std::to_string(mwl);
    utilization_file.open(output_dir+"/utilization_our_"+name_suffix);
    if (utilization_file.fail()) {
        std::cerr << "utilization file does not exist" << std::endl;
        AbruptExit(__FILE__, __LINE__);
    }
    searching_file.open(output_dir+"/searching_"+name_suffix);
    if (searching_file.fail()) {
        std::cerr << "utilization file does not exist" << std::endl;
        AbruptExit(__FILE__, __LINE__);
    }
    capacity_file.open(output_dir+"/capacity_"+name_suffix);
    if (capacity_file.fail()) {
        std::cerr << "capacity file does not exist" << std::endl;
        AbruptExit(__FILE__, __LINE__);
    }
    v_hex_addr_cache = 0;
    uint32_t virtual_cache_page_num = Meta_SRAM.size();
    hash_page_table.resize(virtual_cache_page_num * hpt_sz_ratio);
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
    padding_to_page = 0;
    wasted_block = 0;
    for (int i = 0; i < 16; ++i) {
        region_capacity_ratio[i] = 0;
    }

    tracker_ = new OurTracker(this);

    v_hex_addr_cache_br = hashmap_hex_addr - 256;
    meta_block_region.resize(Meta_SRAM.size() * 16, Tag(0, false, false, 256));
    hpt_block_region.resize(Meta_SRAM.size() * 16 * hpt_sz_ratio);
    rpt_block_region.resize(Meta_SRAM.size() * 16);
    std::cout<<"HPT in block region size is "<<hpt_block_region.size()<<"\n"
            <<"tlb size is "<<tlb_block_region.data.size()<<"\n"
            <<"hash page table base: "<<hashmap_hex_addr_block_region<<"\n";

    PROMOTION_T = 0;
    PADDING_T = 2.0 * config.rtt / config.tCK;
    last_status.emplace_back(threshold(PROMOTION_T, PADDING_T, 1.0 * PADDING_T, 0));
    hit_in_br = 0;
    hit_in_pr = 0;
    miss_in_both = 0;
    roll_back_times = 0;
    idole_time = 0;
    for (int i = 0; i <= (4096/256); ++i) {
        line_utility_partial[i] = 0;
    }
}

void our::Sample(SimpleStats::HistoCount &hist, uint64_t key) {
    if(hist.find(key) == hist.end()){
        hist[key] = 1;
    }else{
        hist[key] ++;
    }
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
    bool is_sent = false;
    uint64_t hex_addr_page = hex_addr / 4096 * 4096;
    uint64_t hex_addr_block = hex_addr / 256 * 256;
    uint32_t value[128/32];
    MurmurHash3_x64_128(&(hex_addr_page), sizeof(uint64_t), 0, value);

    //auto mshr_ptr = std::find_if(CacheFrontEnd::MSHRs.begin(), CacheFrontEnd::MSHRs.end(),
    //                             [hex_addr_page, hex_addr_block](std::pair<uint64_t, std::list<Transaction>> a){
    //    return a.first == hex_addr_page || a.first == hex_addr_block;
    //});
    auto mshr_ptr_1 = std::find_if(MSHRs.begin(), MSHRs.end(),
                                 [hex_addr_page](std::pair<uint64_t, MSHR_g> a){
        return a.first == hex_addr_page;
    });
    auto mshr_ptr_2 = std::find_if(fetch_engine_q.begin(), fetch_engine_q.end(), [hex_addr_page](MSHR_g a){
        return a.hex_addr_page == hex_addr_page;
    });
    auto mshr_ptr_3 = std::find_if(send_page_q.begin(), send_page_q.end(), [hex_addr_page](MSHR_g a){
        return a.hex_addr_page == hex_addr_page;
    });

    is_first = mshr_ptr_1 == MSHRs.end() && mshr_ptr_2 == fetch_engine_q.end() && mshr_ptr_3 == send_page_q.end();
    is_sent = mshr_ptr_1 != MSHRs.end();

    if(is_first){
        //
        MSHR_g m = MSHR_g(Transaction(hex_addr, is_write), value[0]);
        fetch_engine_q.emplace_back(m);
        return;
    }

    if(is_sent){
        mshr_ptr_1->second.blocks.insert(hex_addr_block);
        mshr_ptr_1->second.reqs.emplace_back(Transaction(hex_addr, is_write));
        uint64_t interval = GetCLK() - mshr_ptr_1->second.clock;
        if(mshr_ptr_1->second.is_page_region){
            CacheFrontEnd::MSHRs[hex_addr_page].emplace_back(Transaction(hex_addr, is_write));
        }else if(interval < PADDING_T){
            padding_to_page ++;
            Sample(padding_interval, interval);
            send_page_q.emplace_back(mshr_ptr_1->second);
            MSHRs.erase(mshr_ptr_1);
        }else if(CacheFrontEnd::MSHRs.find(hex_addr_block) == CacheFrontEnd::MSHRs.end()){
            CacheFrontEnd::MSHRs[hex_addr_block] = std::list<Transaction>{Transaction(hex_addr, is_write)};
            LSQ.emplace_back(RemoteRequest(false, hex_addr_block, 256 / 64, 0));
            mshr_ptr_1->second.send_time[hex_addr_block] = GetCLK();
            mshr_ptr_1->second.clock = GetCLK();
        }else{
            CacheFrontEnd::MSHRs[hex_addr_block].emplace_back(Transaction(hex_addr, is_write));
            mshr_ptr_1->second.clock = GetCLK();
        }
        return;
    }

    //not send and not the first
    if(mshr_ptr_2 != fetch_engine_q.end()){
        mshr_ptr_2->reqs.emplace_back(Transaction(hex_addr, is_write));
        mshr_ptr_2->blocks.insert(hex_addr_block);
    }else{
        mshr_ptr_3->reqs.emplace_back(Transaction(hex_addr, is_write));
        mshr_ptr_3->blocks.insert(hex_addr_block);
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
    uint64_t hex_addr_page = req_id / 4096 * 4096;
    if(MSHRs.find(hex_addr_page) != MSHRs.end()){
        if(MSHRs[hex_addr_page].is_page_region){
            if(req_id != hex_addr_page){
                //the req has been promoted to page region
                wasted_block ++;
                return;
            }

            pending_req_to_PT.emplace_back(intermediate_data(MSHRs[req_id].pt_index % hash_page_table.size(), req_id));
            Sample(miss_penalty, GetCLK() - MSHRs[req_id].send_time[req_id]);
            MSHRs.erase(req_id);
        }else{
            pending_req_to_PT_br.emplace_back(intermediate_data(MSHRs[hex_addr_page].pt_index % hpt_block_region.size(),
                                                                req_id));
            for (auto i = MSHRs[hex_addr_page].reqs.begin(); i != MSHRs[hex_addr_page].reqs.end();) {
                if(i->addr / 256 * 256 == req_id){
                    i = MSHRs[hex_addr_page].reqs.erase(i);
                }else
                    i++;
            }
            MSHRs[hex_addr_page].blocks.erase(req_id);
            MSHRs[hex_addr_page].adjacent_blocks ++;
            Sample(miss_penalty, GetCLK() - MSHRs[hex_addr_page].send_time[req_id]);
            if(MSHRs[hex_addr_page].blocks.size() == 0)
                MSHRs.erase(hex_addr_page);
        }
    }else{
        //std::cerr<<"we fetch page after fetch the first block in the page "<<req_id<<"\n";
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

        capacity_file<<GetCLK()<<"\t"<<ratio<<"\n";
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

    if(GetCLK() % mwl == 0){
        double miss_rate = 1.0 * miss_in_both / (hit_in_pr + hit_in_br + miss_in_both);
        double avg_miss_penalty = miss_rate * SimpleStats::GetHistoAvg(miss_penalty);
        std::cout<<"hit in block region "<<hit_in_br<<" "<<1.0*hit_in_br / (hit_in_br + miss_in_both)<<"\n"
                    <<"hit in page region "<<hit_in_pr<<" "<<1.0*hit_in_pr / (hit_in_pr + miss_in_both)<<"\n"
                    <<"miss in both "<<miss_in_both<<"\n"
                    <<"miss rate: "<<miss_rate<<"\n"
                    <<"miss penalty: "<<avg_miss_penalty<<"\n";

        std::vector<uint64_t> partial_sum(4, 0);
        partial_sum[0] = line_utility_partial[1] + line_utility_partial[2];
        std::cout<<"1-2 sum of utilization: "<<partial_sum[0]<<"\n";
        partial_sum[1] = line_utility_partial[3] + line_utility_partial[4];
        std::cout<<"3-4 sum of utilization: "<<partial_sum[1]<<"\n";
        for (int i = 5; i <= 8; ++i) {
            partial_sum[2] += line_utility_partial[i];
        }
        std::cout<<"5-8 sum of utilization: "<<partial_sum[2]<<"\n";
        for (int i = 9; i <= 16; ++i) {
            partial_sum[3] += line_utility_partial[i];
        }
        std::cout<<"9-16 sum of utilization: "<<partial_sum[3]<<"\n";
        double sparsity_ratio = 1.0*(partial_sum[0] + partial_sum[1] + partial_sum[2])/
                (partial_sum[0] + partial_sum[1] + partial_sum[2] + partial_sum[3]);
        std::cout<<"ratio: "<<sparsity_ratio<<"\n";

        uint64_t promotion_ops = promotion_to_page - padding_to_page;
        std::cout<<"padding operations: "<<padding_to_page<<"\n"
                <<"promotion operations: "<<promotion_ops<<"\n";
        std::cout<<"average padding interval: "<<SimpleStats::GetHistoAvg(padding_interval)<<"\n";
        searching_file<<PROMOTION_T<<"\t"<<PADDING_T<<"\n";

        if(idole_time  == 0){
            bool roll_back = false;
            bool increase_promotion = false;
            bool decrease_padding = false;
            bool do_nothing = false;
            bool try_last_state = false;

            while(1){
                bool enable_roll_back = !(last_status.empty() || last_status.back().miss_rate > avg_miss_penalty);
                bool enable_promotion = PROMOTION_T < (64 / pte_size - 1) && sparsity_ratio > 0.5 && promotion_ops >= padding_to_page;
                bool enable_padding = PADDING_T > 0 && sparsity_ratio > 0.5 && promotion_ops <= padding_to_page;
                bool enable_try_last = !last_status.empty() && !(last_status.back().PADDING_T == PADDING_T &&
                        last_status.back().PROMOTION_T == PROMOTION_T);
                //if(!(enable_padding || enable_roll_back || enable_promotion)){
                //    std::cout<<"we do nothing\n";
                //    break;
                //}

                uint64_t option = rand() % 5;
                if(option == 0 && enable_roll_back){
                    roll_back = true;
                    break;
                }

                if(option == 1 && enable_promotion){
                    increase_promotion = true;
                    break;
                }

                if(option == 2 && enable_padding){
                    decrease_padding = true;
                    break;
                }

                if(option == 3){
                    std::cout<<"we do nothing: "<<PROMOTION_T<<" "<<PADDING_T<<"\n";
                    do_nothing = true;
                    break;
                }

                if(option == 4 && enable_try_last){
                    try_last_state = true;
                    break;
                }
            }

            if(roll_back){
                PROMOTION_T = last_status.back().PROMOTION_T;
                PADDING_T = last_status.back().PADDING_T;
                roll_back_times = last_status.back().roll_back_times + 1;
                idole_time = roll_back_times;
                last_status.pop_back();
                std::cout<<"roll back to "<<PROMOTION_T<<" "<<PADDING_T
                <<" and idole for "<<idole_time<<"\n";
                if(last_status.empty())
                    std::cout<<"there is no last status\n";
                else
                    std::cout<<"last status is "<<last_status.back().PROMOTION_T<<" "<<
                    last_status.back().PADDING_T<<"\n";
            }

            if(increase_promotion){
                if(last_status.empty() || avg_miss_penalty < last_status.back().miss_rate)
                    last_status.emplace_back(threshold(PROMOTION_T, PADDING_T, avg_miss_penalty, roll_back_times));
                PROMOTION_T ++;
                std::cout<<"increase threshold to "<<PROMOTION_T<<"\n";
                roll_back_times = 0;
                idole_time = 0;
            }

            if(decrease_padding){
                if(last_status.empty() || avg_miss_penalty < last_status.back().miss_rate)
                    last_status.emplace_back(threshold(PROMOTION_T, PADDING_T, avg_miss_penalty, roll_back_times));
                PADDING_T = SimpleStats::GetHistoAvg(padding_interval);
                std::cout<<"decrease threshold to "<<PADDING_T<<"\n";
                roll_back_times = 0;
                idole_time = 0;
            }

            if(!do_nothing){
                hit_in_br = 0;
                hit_in_pr = 0;
                miss_in_both = 0;
                for (int i = 0; i <= (4096/256); ++i) {
                    line_utility_partial[i] = 0;
                }
                padding_interval.clear();
                padding_to_page = 0;
                promotion_to_page = 0;
                miss_penalty.clear();
            }

            if(try_last_state){
                threshold t = last_status.back();
                last_status.pop_back();
                std::cout<<"we try last state: "<<t.PROMOTION_T<<" "<<t.PADDING_T<<"\n";
                if(avg_miss_penalty < t.miss_rate)
                    last_status.emplace_back(threshold(PROMOTION_T, PADDING_T, avg_miss_penalty, roll_back_times));
                PROMOTION_T = t.PROMOTION_T;
                PADDING_T = t.PADDING_T;
                roll_back_times = t.roll_back_times;
                idole_time = 0;
            }
        }else{
            idole_time --;
        }
    }

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
        uint32_t pt_index_br = fetch_engine_q.front().pt_index % hpt_block_region.size();
        uint32_t pt_index = fetch_engine_q.front().pt_index % hash_page_table.size();
        if(tlb_block_region.AddTransaction(pt_index_br, false)){
            MSHR_g m = std::ref(fetch_engine_q.front());
            std::vector<PTentry> pte_g = tlb_block_region.GetDataGroup(pt_index_br);
            for (int i = 0; i < pte_g.size(); ++i) {
                if(!(pte_g[i].valid &&
                    (pte_g[i].hex_addr_aligned / 4096 * 4096 == m.hex_addr_page)))
                    continue;

                m.adjacent_blocks ++;
            }

            //check returned adjacent blocks
            for (auto i = pending_req_to_PT_br.begin(); i != pending_req_to_PT_br.end(); ++i) {
                if(i->req_id / 4096 * 4096 == m.hex_addr_page){
                    m.adjacent_blocks ++;
                }
            }

            if(m.adjacent_blocks > PROMOTION_T || m.reqs.size() > 1){
                send_page_q.emplace_back(m);
            }else{
                for (auto i = m.blocks.begin(); i != m.blocks.end(); ++i) {
                    CacheFrontEnd::MSHRs[*i] = std::list<Transaction>{};
                    LSQ.emplace_back(RemoteRequest(false, *i, 256 / 64, 0));
                    m.send_time[*i] = GetCLK();
                }
                for (auto i = m.reqs.begin(); i != m.reqs.end(); ++i) {
                    CacheFrontEnd::MSHRs[i->addr / 256 * 256].emplace_back(*i);
                }
                m.clock = GetCLK();
                MSHRs[m.hex_addr_page] = m;
            }
            fetch_engine_q.pop_front();
        }
    }

    if(!send_page_q.empty()){
        uint32_t pt_index_br = send_page_q.front().pt_index % hpt_block_region.size();
        if(tlb_block_region.AddTransaction(pt_index_br, false)){
            MSHR_g m = std::ref(send_page_q.front());
            std::vector<PTentry> pte_g = tlb_block_region.GetDataGroup(pt_index_br);

            //cancel the data if returned
            for (auto i = pending_req_to_PT_br.begin(); i != pending_req_to_PT_br.end();) {
                if(i->req_id / 4096 * 4096 == m.hex_addr_page){
                    m.adjacent_blocks --;
                    for (auto j = CacheFrontEnd::MSHRs[i->req_id].begin(); j != CacheFrontEnd::MSHRs[i->req_id].end(); ++j) {
                        m.reqs.emplace_back(*j);
                    }
                    m.blocks.insert(i->req_id);
                    i = pending_req_to_PT_br.erase(i);
                }else
                    i++;
            }

            //remove old mshr if exist
            for (auto i = CacheFrontEnd::MSHRs.begin(); i != CacheFrontEnd::MSHRs.end();) {
                if(i->first / 4096 * 4096 == m.hex_addr_page){
                    i = CacheFrontEnd::MSHRs.erase(i);
                }else
                    ++i;
            }

            //remove ptes in block region
            for (int i = 0; i < pte_g.size(); ++i) {
                if(!(pte_g[i].valid && (pte_g[i].hex_addr_aligned / 4096 * 4096 == m.hex_addr_page)))
                    continue;

                m.pte_br.emplace_back(pte_g[i]);
                m.is_dirty |= meta_block_region[pte_g[i].hex_cache_addr / 256].dirty;
                tlb_block_region.AddTransaction(pt_index_br, true, i, PTentry());
                rtlb_block_region.AddTransaction(pte_g[i].hex_cache_addr / 256, true, RPTentry());
            }

            //send a page
            m.is_page_region = true;
            //send a req to fetch page
            LSQ.emplace_back(RemoteRequest(false, m.hex_addr_page, 4096 / 64, 0));
            //set up new MSHR
            m.send_time[m.hex_addr_page] = GetCLK();
            m.clock = GetCLK();
            MSHRs[m.hex_addr_page] = m;
            CacheFrontEnd::MSHRs[m.hex_addr_page] = m.reqs;
            promotion_to_page ++;
            send_page_q.pop_front();
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
            hit_in_br += is_hit_br;
            miss_in_both += (1 - is_hit_br);
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
            hit_in_pr += is_hit;

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
            //TODO: write back dirty data if necessary
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
    if((*meta_ptr)[index].valid && (*meta_ptr)[index].granularity > 256){
        line_utility_partial[(*meta_ptr)[index].utilized()] ++;
    }
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
                    <<"# times of refilling to block: "<<refill_to_block<<"\n"
                    <<"# num of wasted block: "<<wasted_block<<"\n"
                    <<"# times of go back to head: "<<go_back_to_head<<"\n";

    utilization_file<<"# Final threshold status: "<<PROMOTION_T<<" "<<PADDING_T<<"\n";

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