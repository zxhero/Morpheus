//
// Created by zhangxu on 3/25/23.
//


#include "trace_seg.h"
#include "trace.h"
#include "./../ext/headers/args.hxx"
#include <iomanip>
namespace dramsim3 {

uint64_t seg::length() {
    return eid - sid + 1;
}

std::istream& operator>>(std::istream& is, seg& tmp){
    std::string line;
    while(getline(is, line)){
        if((line[line.find_first_not_of(" \t\r\n")]) != '#'){
            std::istringstream lstream(line);
            lstream >>tmp.pid>>tmp.process_num
                    >>tmp.sid>>tmp.eid;
            return is;
        }
    }
    tmp.sid = 0;
    tmp.eid = 0;
    return is;
}

std::ostream& operator<<(std::ostream& os, seg& tmp){
    os<<std::setw(15)<<tmp.pid
            <<std::setw(15)<<tmp.process_num
            <<std::setw(15)<<tmp.sid
            <<std::setw(15)<<tmp.eid
            <<std::setw(15)<<tmp.length()
            <<std::setw(15)<<tmp.time_span
            <<std::setw(15)<<tmp.pages_num
            <<std::setw(15)<<tmp.r_num
            <<std::setw(15)<<tmp.w_num
            <<std::setw(15)<<tmp.kernel_num;
    os<<"\n"<<std::flush;
    return os;
}
}

using namespace dramsim3;

int main(int argc, const char **argv){
    args::ArgumentParser parser(
        "Trace Segmentation.",
        "Examples: \n."
        "./build/trace_seg /mnt/hmtt/ligra_bfs -p 1 \n");
    args::HelpFlag help(parser, "help", "Display the help menu", {'h', "help"});
    args::ValueFlag<uint64_t> num_process_arg(parser, "number_of_process",
                                             "The number of process to profile",
                                             {'p'}, 16);
    args::Positional<std::string> trace_arg(
        parser, "trace", "The trace file name (mandatory)");
    args::Positional<int> pid_arg(
        parser, "pid", "The pid of parent process (mandatory)");

    try {
        parser.ParseCLI(argc, argv);
    } catch (args::Help) {
        std::cout << parser;
        return 0;
    } catch (args::ParseError e) {
        std::cerr << e.what() << std::endl;
        std::cerr << parser;
        return 1;
    }

    std::string trace_file = args::get(trace_arg);
    if (trace_file.empty()) {
        std::cerr << parser;
        return 1;
    }

    uint64_t process = args::get(num_process_arg);
    int ppid = args::get(pid_arg);
    std::unordered_map<int, uint64_t> num_traces_for_each_p;

    trace_init((trace_file+".trace").c_str(), (trace_file+".kt").c_str());
    HMTTTransaction tmp;
    std::list<seg> segs;
    uint64_t id = 0, sid = 0;
    uint64_t lasting = 0;
    uint64_t r_num = 0, w_num = 0;
    std::unordered_set<uint64_t> total_ppns;

    uint64_t max_period = 1000000000, period = 0;
    std::unordered_set<uint64_t> ppns;
    unsigned long max_ppns = 0;

    GetNextHMTT(tmp, ppid, process);
    period = tmp.added_ns;
    while(tmp.valid){
        if(tmp.added_ns > 1000000 && id > 0){
            segs.emplace_back(seg(sid, id - 1));
            segs.back().time_span = lasting;
            segs.back().w_num = w_num;
            segs.back().r_num = r_num;
            segs.back().kernel_num = nonpte;
            segs.back().pages_num = total_ppns.size();
            segs.back().pid = ppid;
            segs.back().process_num = process;
            nonpte = 0;
            sid = id;
            lasting = 0;
            w_num = 0;
            r_num = 0;
            total_ppns.clear();
        }else{
            lasting += tmp.added_ns;
            r_num += tmp.r_w;
            w_num += (1 - tmp.r_w);
            total_ppns.insert(tmp.vaddr >> 12);
        }

        if(!tmp.is_kernel){
            if(num_traces_for_each_p.find(tmp.pid) == num_traces_for_each_p.end()){
                num_traces_for_each_p[tmp.pid] = 1;
            }else{
                num_traces_for_each_p[tmp.pid] ++;
            }
        }

        id++;
        GetNextHMTT(tmp, ppid, process);

        period += tmp.added_ns;
        if(period < max_period){
            ppns.insert(tmp.vaddr >> 12);
        }
        else{
            period = 0;
            if(ppns.size() > max_ppns)
                max_ppns = ppns.size();
            ppns.clear();
        }
    }

    trace_finish();

    std::cout<<"#"<<std::setw(15)<<"ppid"
            <<std::setw(15)<<"#proc"
            <<std::setw(15)<<"start id"
            <<std::setw(15)<<"end id"
            <<std::setw(15)<<"length"
            <<std::setw(15)<<"ns"
            <<std::setw(15)<<"#pages"
            <<std::setw(15)<<"#read"
            <<std::setw(15)<<"#write"
            <<std::setw(15)<<"#kernel\n";
    for (auto i = segs.begin(); i != segs.end(); ++i) {
        if(i->length() > 1000000 && (i->length()- i->kernel_num > 1000000)){
            std::cout<<*i;
        }
    }
    std::cout<<"#"<<std::setw(15)<<"#pages"
            <<std::setw(15)<<"Bytes\n"
            <<std::dec<<std::setw(15)<<max_ppns
            <<std::setw(15)<<max_ppns*4096
            <<"\n";
    for (auto i = num_traces_for_each_p.begin(); i != num_traces_for_each_p.end(); ++i) {
        std::cout<<"#"<<i->first<<": "<<i->second<<"\n";
    }
}