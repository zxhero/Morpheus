#include "cpu.h"

namespace dramsim3 {

void RandomCPU::ClockTick() {
    // Create random CPU requests at full speed
    // this is useful to exploit the parallelism of a DRAM protocol
    // and is also immune to address mapping and scheduling policies
    memory_system_.ClockTick();
    if (get_next_) {
        last_addr_ = gen();
        last_write_ = (gen() % 3 == 0);
    }
    get_next_ = memory_system_.WillAcceptTransaction(last_addr_, last_write_);
    if (get_next_) {
        memory_system_.AddTransaction(last_addr_, last_write_);
    }
    clk_++;
    return;
}

void StreamCPU::ClockTick() {
    // stream-add, read 2 arrays, add them up to the third array
    // this is a very simple approximate but should be able to produce
    // enough buffer hits

    // moving on to next set of arrays
    memory_system_.ClockTick();
    if (offset_ >= array_size_ || clk_ == 0) {
        addr_a_ = gen();
        addr_b_ = gen();
        addr_c_ = gen();
        offset_ = 0;
    }

    if (!inserted_a_ &&
        memory_system_.WillAcceptTransaction(addr_a_ + offset_, false)) {
        memory_system_.AddTransaction(addr_a_ + offset_, false);
        inserted_a_ = true;
    }
    if (!inserted_b_ &&
        memory_system_.WillAcceptTransaction(addr_b_ + offset_, false)) {
        memory_system_.AddTransaction(addr_b_ + offset_, false);
        inserted_b_ = true;
    }
    if (!inserted_c_ &&
        memory_system_.WillAcceptTransaction(addr_c_ + offset_, true)) {
        memory_system_.AddTransaction(addr_c_ + offset_, true);
        inserted_c_ = true;
    }
    // moving on to next element
    if (inserted_a_ && inserted_b_ && inserted_c_) {
        offset_ += stride_;
        inserted_a_ = false;
        inserted_b_ = false;
        inserted_c_ = false;
    }
    clk_++;
    return;
}

TraceBasedCPU::TraceBasedCPU(const std::string& config_file,
                             const std::string& output_dir,
                             const std::string& trace_file)
    : CPU(config_file, output_dir) {
    trace_file_.open(trace_file);
    if (trace_file_.fail()) {
        std::cerr << "Trace file does not exist" << std::endl;
        AbruptExit(__FILE__, __LINE__);
    }
}

TraceBasedCPU::TraceBasedCPU(const std::string &config_file,
                             const std::string &output_dir,
                             const std::string &trace_file,
                             std::function<void (uint64_t)> read_callback_)
    : CPU(config_file, output_dir, read_callback_){
    trace_file_.open(trace_file);
    if (trace_file_.fail()) {
        std::cerr << "Trace file does not exist" << std::endl;
        AbruptExit(__FILE__, __LINE__);
    }
}

void TraceBasedCPU::ClockTick() {
    memory_system_.ClockTick();
    if (!trace_file_.eof()) {
        if (get_next_) {
            get_next_ = false;
            trace_file_ >> trans_;
        }
        if (trans_.added_cycle <= clk_) {
            get_next_ = memory_system_.WillAcceptTransaction(trans_.addr,
                                                             trans_.is_write);
            if (get_next_) {
                memory_system_.AddTransaction(trans_.addr, trans_.is_write);
            }
        }
    }
    clk_++;
    return;
}

HMTTCPU::HMTTCPU(const std::string &config_file, const std::string &output_dir, const std::string &trace_file)
    : TraceBasedCPU(config_file, output_dir, trace_file,
                    std::bind(&HMTTCPU::ReadCallBack, this, std::placeholders::_1)),
    memory_system_local("configs/DDR4_4Gb_x4_1866.ini", output_dir + "/local",
                        std::bind(&HMTTCPU::ReadCallBack, this, std::placeholders::_1),
                        std::bind(&CPU::WriteCallBack, this, std::placeholders::_1)),
    rob_sz(256 / memory_system_.GetTCK()){

    std::cout<<rob_sz<<"\n";
    last_req_ns = 0;
    wait = rob.end();
    outstanding = 0;
    wall_clk = 0;
    max_outstanding = 0;
    kernel_trace_count = 0;
    app_trace_count = 0;
}

void HMTTCPU::ClockTick() {
    memory_system_.ClockTick();
    memory_system_local.ClockTick();
    if(get_next_){
        if(!trace_file_.eof()){
            trace_file_ >> tmp;
            get_next_ = false;
        }
    }

    if(tmp.valid){
        //std::cout<<"["<<std::dec<<kernel_trace_count + app_trace_count<<"]: "<<tmp.added_ns<<" "<<std::hex<<tmp.addr<<(tmp.is_kernel ? " kernel" : " app")
        //<<(tmp.r_w ? " Read": " Write")<<"\n"<<std::dec;
        if(rob.empty()){

            wall_clk += ceil(tmp.added_ns / memory_system_.GetTCK());
            last_req_ns += tmp.added_ns;
            tmp.added_ns = 0;
            clk_ = ceil(last_req_ns / memory_system_.GetTCK());
            //std::cout<<"fast forwarding to next trace "<<clk_<<" "<<wall_clk<<"\n";
        }

        if((tmp.added_ns + last_req_ns) <= (clk_ * memory_system_.GetTCK())){
            last_req_ns += tmp.added_ns;
            tmp.added_ns = clk_;
            tmp.is_finished = false;
            rob.emplace_back(tmp);
            get_next_ = true;
            if(wait == rob.end()){
                wait--;
            }
        }
    }

    //check data dependency
    bool is_depend = false;
    if(wait != rob.end()){
        for (auto i = rob.begin(); i != wait; ++i) {
            if(i->addr == wait->addr){
                is_depend = true;
                break;
            }
        }
    }

    if(!is_depend && outstanding < mshr_sz && wait != rob.end()){
        MemorySystem *issued_to;
        if(!wait->is_kernel){
            issued_to = &memory_system_;
        }else{
            issued_to = &memory_system_local;
        }

        if(issued_to->WillAcceptTransaction(wait->addr, wait->r_w == 0)){
            issued_to->AddTransaction(wait->addr, wait->r_w == 0);
            //std::cout<<std::hex<<wait->addr<<" is issued\n";

            if(!wait->is_kernel){
                app_trace_count ++;
            }else{
                kernel_trace_count ++;
            }

            if(wait->r_w != 0){
                outstanding ++;
                if(outstanding > max_outstanding)
                    max_outstanding = outstanding;
                wait ++;
            }else{
                if(wait == rob.begin()){
                    wait = rob.erase(rob.begin());
                }else{
                    wait->is_finished = true;
                    wait ++;
                }
            }
        }
    }

    if(rob.empty() || (clk_ < (rob.front().added_ns + rob_sz))){
        clk_++;
        //std::cout<<clk_<<" "<<(rob.front().added_ns + rob_sz)<<"\n";
    }


    if(!trace_file_.eof())
        wall_clk ++;
}

void HMTTCPU::ReadCallBack(uint64_t addr) {
    auto res = std::find_if(rob.begin(), rob.end(), [addr](const HMTTTransaction a){
        return a.addr == addr && a.r_w == 1;
    });
    res->is_finished = true;
    outstanding --;

    for (auto i = rob.begin(); i != wait; ) {
        if(i->is_finished)
            i = rob.erase(i);
        else
            break;
    }
    //std::cout<<std::hex<<addr<<" read finished "<<rob.size()<<" "<<outstanding<<"\n";
}

void HMTTCPU::PrintStats() {
    memory_system_.PrintStats();
    memory_system_local.PrintStats();
    std::cout<<"max outstanding: "<<max_outstanding<<"\n";
    std::cout<<std::dec<<"cpu clock: "<<clk_<<"\n";
    std::cout<<std::dec<<"wall clock: "<<wall_clk<<"\n";
    std::cout<<std::dec<<"kernel traces: "<<kernel_trace_count<<"\n";
    std::cout<<std::dec<<"app traces: "<<app_trace_count<<"\n";
    //if(!trace_file_.eof()){
//
    //}
}

bool HMTTCPU::IsEnd() {
    return trace_file_.eof() || ((kernel_trace_count + app_trace_count) > simulating);
}

uint64_t HMTTCPU::GetTraceNum() {
    return kernel_trace_count + app_trace_count;
}

uint64_t HMTTCPU::GetClk() {
    return wall_clk;
}

void HMTTCPU::WarmUp() {
    for (int i = 0; i < skipping; ++i) {
        trace_file_ >> tmp;
    }
}

void HMTTCPU::Drained() {
    while(outstanding != 0){
        memory_system_local.ClockTick();
        memory_system_.ClockTick();
    }
}
}  // namespace dramsim3
