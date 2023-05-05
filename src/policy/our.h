//
// Created by zhangxu on 4/25/23.
//

#ifndef DRAMSIM3_OUR_H
#define DRAMSIM3_OUR_H
#include "cache_frontend.h"
#include "../../util/murmur3/murmur3.h"

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
    bool valid;
    RPTentry(){valid = false;};
    RPTentry(uint64_t pt_index_): pt_index(pt_index_){valid = true;};
};

/*
 * multi-port cache
 * data only has temporary locality
 * read: direct mapped
 * write has higher priority than read to same address
 * */
template<class dType>
class SRAMCache{
  private:
    const uint64_t hex_dram_base_addr;
    const uint64_t dType_sz;
    std::vector<dType> data;
    std::vector<dType> *data_backup;
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
              std::vector<dType> *data_backup_);
    ~SRAMCache(){};
    //hit return true
    bool AddTransaction(uint64_t hex_offset, bool is_write, dType dptr = dType());
    void Drained();
    void DRAMReadBack(CacheAddr req_id);
    dType GetData(uint64_t hex_offset);
    dType WarmUp(uint64_t hex_offset, bool is_write, dType dptr = dType());
};

/*
 * one-port cache
 * take advantage of stream accessing pattern of reversed page table
 * read: prefetch 8 RPTe at a time
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
    std::set<CacheAddr> waiting_resp_RPT;
    CacheAddr tag;

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
    const CacheAddr hashmap_hex_addr;
    const uint32_t pte_size;
    class intermediate_data{
        public:
        uint32_t rpt_index;
        PTentry pte;
        uint32_t pt_index;
        bool valid;
        intermediate_data(uint32_t rpt_index_, PTentry pte_, uint32_t pt_index_):
        rpt_index(rpt_index_), pte(pte_), pt_index(pt_index_){ valid = true;};
        intermediate_data(){ valid = false;};
    };
    class intermediate_req{
        public:
        Tag *t;
        uint64_t hex_addr_cache;
        uint64_t hex_addr;
        bool is_write;
        intermediate_req(Tag *t_, uint64_t hex_addr_cache_, uint64_t hex_addr_, bool is_write_):
        t(t_), hex_addr_cache(hex_addr_cache_), hex_addr(hex_addr_), is_write(is_write_){};
        intermediate_req(){};
    };
    std::vector<PTentry> hash_page_table;
    SRAMCache<PTentry> tlb;
    std::vector<RPTentry> pte_addr_table;
    RPTCache rtlb;
    uint64_t v_hex_addr_cache;
    std::list<intermediate_req> pending_req_to_Meta;
    std::list<uint64_t> pending_req_to_PT;
    void InsertRemotePage(intermediate_data tmp);
    void AllocCPage(uint32_t pt_index, uint64_t req_id);

    uint64_t collision_times;
    uint64_t non_collision_times;
    void CheckHashPT();

  protected:
    bool GetTag(uint64_t hex_addr, Tag *&tag_, uint64_t &hex_addr_cache) override;
    uint64_t GetHexTag(uint64_t hex_addr) override;
    uint64_t GetHexAddr(uint64_t hex_tag, uint64_t hex_addr_cache) override;
    uint64_t AllocCPage(uint64_t hex_addr, Tag *&tag_) override;
    void MissHandler(uint64_t hex_addr, bool is_write) override;
    void WriteBackData(Tag tag_, uint64_t hex_addr_cache) override;
    void HashReadCallBack(uint64_t req_id) override;

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
