#ifndef __CPU_H
#define __CPU_H

#include <fstream>
#include <functional>
#include <random>
#include <string>
#include <list>
#include <utility>
#include "memory_system.h"
#include "trace_seg.h"

namespace dramsim3 {

class CPU {
   public:
    CPU(const std::string& config_file, const std::string& output_dir)
        : memory_system_(
              config_file, output_dir,
              std::bind(&CPU::ReadCallBack, this, std::placeholders::_1),
              std::bind(&CPU::WriteCallBack, this, std::placeholders::_1)),
          clk_(0) {};
    CPU(const std::string& config_file, const std::string& output_dir, std::function<void(uint64_t)> read_callback_)
        : memory_system_(
              config_file, output_dir, read_callback_,
              std::bind(&CPU::WriteCallBack, this, std::placeholders::_1)),
          clk_(0) {};
    virtual void ClockTick() = 0;
    virtual void ReadCallBack(uint64_t addr) { return; }
    void WriteCallBack(uint64_t addr) { return; }
    virtual void PrintStats() { memory_system_.PrintStats(); }

   protected:
    MemorySystem memory_system_;
    uint64_t clk_;
};

class RandomCPU : public CPU {
   public:
    using CPU::CPU;
    void ClockTick() override;

   private:
    uint64_t last_addr_;
    bool last_write_ = false;
    std::mt19937_64 gen;
    bool get_next_ = true;
};

class StreamCPU : public CPU {
   public:
    using CPU::CPU;
    void ClockTick() override;

   private:
    uint64_t addr_a_, addr_b_, addr_c_, offset_ = 0;
    std::mt19937_64 gen;
    bool inserted_a_ = false;
    bool inserted_b_ = false;
    bool inserted_c_ = false;
    const uint64_t array_size_ = 2 << 20;  // elements in array
    const int stride_ = 64;                // stride in bytes
};

class TraceBasedCPU : public CPU {
   public:
    TraceBasedCPU(const std::string& config_file, const std::string& output_dir,
                  const std::string& trace_file);
    TraceBasedCPU(const std::string& config_file, const std::string& output_dir,
                  const std::string& trace_file, std::function<void(uint64_t)> read_callback_);
    ~TraceBasedCPU() { trace_file_.close(); }
    void ClockTick() override;

   protected:
    std::ifstream trace_file_;
    Transaction trans_;
    bool get_next_ = true;
};

class HMTTCPU : public TraceBasedCPU {
   private:
    const int rob_sz ;
    const int mshr_sz = 64;
    //const int skipping = 1000000;
    const int simulating = 10000000;
    //const double clk_ns = 0.5; //2GHz
    HMTTTransaction tmp;
    uint64_t last_req_ns;
    std::list<HMTTTransaction> rob;
    std::list<HMTTTransaction>::iterator wait;
    uint64_t outstanding;
    MemorySystem memory_system_local;

    //Global variables
    std::ifstream seg_file_;
    seg cur_seg;
    uint64_t trace_id;
    bool GetNextSeg();
    void Reset();

    //statics
    uint64_t kernel_trace_count;
    uint64_t app_trace_count;
    uint64_t  wall_clk;
    uint64_t max_outstanding;
    SimpleStats::HistoCount read_latency;

   public:
    HMTTCPU(const std::string& config_file, const std::string& output_dir,
                  const std::string& trace_file, const std::string &seg_file);
    ~HMTTCPU() {seg_file_.close();};
    void ClockTick() override;
    void ReadCallBack(uint64_t addr) override;
    void PrintStats() override;
    bool IsEnd();
    uint64_t GetTraceNum();
    uint64_t GetClk();
    void WarmUp();
    void Drained();
};

}  // namespace dramsim3
#endif
