//
// Created by zhangxu on 4/25/23.
//

#ifndef DRAMSIM3_OUR_H
#define DRAMSIM3_OUR_H
#include "cache_frontend.h"
#include "../../util/murmur3/murmur3.h"

//#define PROMOTION_T 2

namespace dramsim3 {
class PTentry{
    public:
    uint64_t hex_addr_aligned;
    uint32_t hex_cache_addr;
    bool valid;
    PTentry(){valid = false;};
    PTentry(uint64_t hex_addr_aligned_, uint32_t hex_cache_addr_):
        hex_addr_aligned(hex_addr_aligned_), hex_cache_addr(hex_cache_addr_){valid = true;};
};

class RPTentry{
    public:
    /*
     * we actually store hex_addr_aligned in RPT.
     * we store pt_index to accelerate simulation.
     * */
    uint64_t pt_index;
    uint64_t offset;
    bool valid;
    RPTentry(){valid = false;};
    RPTentry(uint64_t pt_index_, uint64_t offset_): pt_index(pt_index_), offset(offset_){valid = true;};
};

/*
 * multi-port cache
 * data only has temporary locality
 * read: direct mapped
 * write has higher priority than read to same address
 * */
class SRAMCache{
  private:
    const uint64_t hex_dram_base_addr;
    const uint64_t dType_sz;
    std::vector<std::vector<PTentry>> data;
    std::vector<PTentry> *data_backup;
    std::vector<Tag> tags;
    JedecDRAMSystem *dram;
    //std::function<void(uint64_t req_id)> read_callback_;
    std::list<std::pair<CacheAddr, bool>> pending_req_to_dram;
    std::unordered_map<CacheAddr, uint64_t> waiting_resp_from_dram;
    std::unordered_map<CacheAddr, uint64_t> droped_reqs;
    friend class our;

    uint64_t hit_;
    uint64_t miss_;
    std::set<uint64_t> last_req;
    uint64_t last_hit_req;
  public:
    SRAMCache(uint64_t hex_base, JedecDRAMSystem *dram_, uint64_t sz, uint64_t capacity,
              std::vector<PTentry> *data_backup_);
    //SRAMCache(){};
    ~SRAMCache(){};
    //hit return true
    bool AddTransaction(uint64_t hex_offset, bool is_write, uint64_t offset = 0, PTentry dptr = PTentry());
    void Drained();
    bool DRAMReadBack(CacheAddr req_id);
    //return a free pte or randomly chosen one from pte group
    PTentry GetData(uint64_t hex_offset, uint64_t &offset);
    //return a pte whose vaddr == tag or false
    bool GetData(uint64_t hex_offset, uint64_t tag, PTentry &pte);
    //return pte group at hex_offset
    std::vector<PTentry> GetDataGroup(uint64_t hex_offset);
    std::vector<PTentry> WarmUpRead(uint64_t hex_offset);
    void WarmUPWrite(uint64_t hex_offset, uint64_t offset, PTentry dptr);
};

/*
 * one-port cache
 * take advantage of stream accessing pattern of reversed page table
 * all-connected cache with 4 entry
 * write through if miss
 * */
class RPTCache {
  private:
    const CacheAddr RPT_hex_addr;
    const uint32_t rpte_size;
    const CacheAddr RPT_hex_addr_h;
    std::vector<RPTentry> *data_backup;
    JedecDRAMSystem *dram;
    std::list<std::pair<CacheAddr, bool>> pending_req_to_RPT;
    std::unordered_set<CacheAddr> waiting_resp_RPT;
    std::list<CacheAddr> tag;
    uint32_t max_tag_sz;

    uint64_t hit_;
    uint64_t miss_;
    friend class our;

  public:
    RPTCache(uint64_t hex_base, JedecDRAMSystem *dram_, uint64_t sz, uint64_t capacity,
              std::vector<RPTentry> *data_backup_);
    ~RPTCache(){};
    //hit return true
    void AddTransaction(uint64_t hex_offset, bool is_write, RPTentry dptr = RPTentry());
    bool WillAcceptTransaction();
    void Drained();
    bool DRAMReadBack(CacheAddr req_id);
    RPTentry GetData(uint64_t hex_offset);
    RPTentry WarmUp(uint64_t hex_offset, bool is_write, RPTentry dptr = RPTentry());
};

class our : public CacheFrontEnd{
  private:
    //const uint64_t granularity;
    const CacheAddr hashmap_hex_addr;
    const uint32_t pte_size;
    class intermediate_data{
        public:
        uint32_t rpt_index;
        PTentry pte;
        uint32_t pt_index;
        uint64_t offset;
        bool valid;
        bool is_collision = false;
        //used when decided to refill to page region
        uint64_t req_id;
        bool to_page_region;
        //intermediate_data(uint32_t rpt_index_, PTentry pte_, uint32_t pt_index_):
        //rpt_index(rpt_index_), pte(pte_), pt_index(pt_index_){ valid = true; free_br = false;};
        intermediate_data(uint32_t pt_index_, uint64_t req_id_): pt_index(pt_index_), req_id(req_id_)
        {valid = true; to_page_region = true;};
        intermediate_data(){ valid = false;};
    };
    class intermediate_req{
        public:
        Tag *t;
        uint64_t hex_addr_cache;
        uint64_t hex_addr;
        bool is_write;
        bool is_hit;
        uint32_t pt_index_br;
        intermediate_req(Tag *t_, uint64_t hex_addr_cache_, uint64_t hex_addr_, bool is_write_, bool is_hit_):
        t(t_), hex_addr_cache(hex_addr_cache_), hex_addr(hex_addr_), is_write(is_write_), is_hit(is_hit_){};
        intermediate_req(uint64_t hex_addr_, bool is_write_, uint32_t pt_index_br_):
        hex_addr(hex_addr_), is_write(is_write_),pt_index_br(pt_index_br_){};
        intermediate_req(){};
    };
    std::vector<PTentry> hash_page_table;
    SRAMCache tlb;
    std::vector<RPTentry> pte_addr_table;
    RPTCache rtlb;
    uint64_t v_hex_addr_cache;
    std::list<intermediate_req> pending_req_to_Meta;
    std::list<intermediate_data> pending_req_to_PT;
    void InsertRemotePage(intermediate_data tmp);
    void AllocCPage(intermediate_data tmp);
    bool GetTag(uint64_t hex_addr, Tag *&tag_, uint64_t &hex_addr_cache, uint32_t pt_index, bool is_page_region);
    void RefillToRegion(intermediate_data tmp);
    void RefillToRegionWarmUp(intermediate_data tmp, bool is_write);
    void ProceedToNextFree(bool is_page_region);
    //void ProceedToNextFreeBR();

    /*
     * structure for block region
     * they can be merged with meta of page region
     * we temporarily separate them for simulation
     * */
    const CacheAddr hashmap_hex_addr_block_region;
    std::vector<Tag> meta_block_region;
    std::vector<PTentry> hpt_block_region;
    SRAMCache tlb_block_region;
    std::vector<RPTentry> rpt_block_region;
    RPTCache rtlb_block_region;
    uint64_t v_hex_addr_cache_br;
    std::list<intermediate_data> pending_req_to_PT_br;
    std::list<intermediate_req> front_q_br;

    class MSHR_g{
        public:
        std::list<Transaction> reqs;
        std::set<uint64_t> blocks;
        uint32_t pt_index;
        bool is_page_region;
        uint64_t adjacent_blocks;
        uint64_t hex_addr_page;
        //uint32_t pt_index;
        //the slot to be promoted
        std::vector<PTentry> pte_br;
        bool is_dirty;
        uint64_t clock;
        std::unordered_map<uint64_t, uint64_t> send_time;
        MSHR_g(Transaction t_, uint32_t pt_index_): MSHR_g(){
            reqs.emplace_back(t_);
            pt_index = pt_index_;
            hex_addr_page = t_.addr / 4096 * 4096;
            blocks.insert(t_.addr / 256 * 256);
        };
        MSHR_g(){is_dirty = false; is_page_region = false; adjacent_blocks = 0;};
    };
    std::unordered_map<uint64_t, MSHR_g> MSHRs;
    std::list<MSHR_g> fetch_engine_q;
    std::list<MSHR_g> send_page_q;

    uint64_t collision_times;
    uint64_t non_collision_times;
    uint64_t refill_to_page;
    uint64_t refill_to_block;
    uint64_t go_back_to_head;
    uint64_t promotion_to_page;
    uint64_t wasted_block;
    SimpleStats::HistoCount region_capacity_ratio;
    void CheckHashPT(bool is_page_region);
    uint64_t hit_in_br;
    uint64_t hit_in_pr;
    uint64_t miss_in_both;
    uint64_t PROMOTION_T;
    uint64_t PADDING_T;
    class threshold{
        public:
        uint64_t PROMOTION_T;
        uint64_t PADDING_T;
        double miss_rate;
        threshold(uint64_t PROMOTION_T_, uint64_t PADDING_T_, double miss_rate_):
        PROMOTION_T(PROMOTION_T_), PADDING_T(PADDING_T_), miss_rate(miss_rate_){};
    };
    std::list<threshold> last_status;
    SimpleStats::HistoCount line_utility_partial;
    SimpleStats::HistoCount padding_interval;
    SimpleStats::HistoCount miss_penalty;
    void Sample(SimpleStats::HistoCount &hist, uint64_t key);

  protected:
    bool GetTag(uint64_t hex_addr, Tag *&tag_, uint64_t &hex_addr_cache) override;
    uint64_t GetHexTag(uint64_t hex_addr) override;
    uint64_t AllocCPage(uint64_t hex_addr, Tag *&tag_) override;
    void MissHandler(uint64_t hex_addr, bool is_write) override;
    void WriteBackData(Tag tag_, uint64_t hex_addr_cache) override;
    void HashReadCallBack(uint64_t req_id) override;
    bool CheckOtherBuffer(uint64_t hex_addr, bool is_write) override;

  public:
    our(std::string output_dir, JedecDRAMSystem *cache, Config &config);
    ~our(){};
    void Refill(uint64_t req_id) override;
    void Drained() override;
    void WarmUp(uint64_t hex_addr, bool is_write) override;
    void PrintStat() override;
};
}
#endif //DRAMSIM3_OUR_H
