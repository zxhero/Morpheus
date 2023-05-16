//
// Created by zhangxu on 4/3/23.
//

#ifndef DRAMSIM3_KONA_H
#define DRAMSIM3_KONA_H

#include "cache_frontend.h"
namespace dramsim3{

class Kona : public CacheFrontEnd{
  private:
    const uint64_t granularity;
    const uint64_t granularity_mask;
    const uint64_t index_num;
    const CacheAddr hashmap_hex_addr;
    std::list<std::pair<CacheAddr, RemoteRequest>> pending_req_to_hashmap;
    std::unordered_map<CacheAddr, RemoteRequest> waiting_resp_from_hashmap;

  protected:
    bool GetTag(uint64_t hex_addr, Tag *&tag_, uint64_t &hex_addr_cache) override;
    uint64_t GetHexTag(uint64_t hex_addr) override;
    uint64_t AllocCPage(uint64_t hex_addr, Tag *&tag_) override;
    void MissHandler(uint64_t hex_addr, bool is_write) override;
    void WriteBackData(Tag tag_, uint64_t hex_addr_cache) override;
    void HashReadCallBack(uint64_t req_id) override;
    bool CheckOtherBuffer(uint64_t hex_addr, bool is_write) override{return true;};
  public:
    Kona(std::string output_dir, JedecDRAMSystem *cache, Config &config);
    ~Kona(){};
    void Drained() override;
};
}
#endif //DRAMSIM3_KONA_H
