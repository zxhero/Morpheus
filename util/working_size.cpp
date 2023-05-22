//
// Created by zhangxu on 4/21/23.
//

#include "./../ext/headers/args.hxx"
#include <iomanip>
#include "../src/working_size.h"

using namespace dramsim3;

const uint64_t simulation = 10000000;
int main(int argc, const char **argv){
    args::ArgumentParser parser(
        "Trace Segmentation.",
        "Examples: \n."
        "./build/working_size /mnt/hmtt/ligra_bfs ./output/ligra_bfs \n");
    args::HelpFlag help(parser, "help", "Display the help menu", {'h', "help"});
    args::Positional<std::string> trace_arg(
        parser, "trace", "The trace file name (mandatory)");
    args::Positional<std::string> seg_arg(
        parser, "segment", "The segment file name (mandatory)");
    args::ValueFlag<int64_t> start_arg(parser, "start",
                                             "start No. of trace",
                                             {'s'}, -1);

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

    int64_t start=args::get(start_arg);


    std::string trace_file = args::get(trace_arg);
    if (trace_file.empty()) {
        std::cerr << parser;
        return 1;
    }

    std::string seg_file = args::get(seg_arg);
    seg_file += ".seg";
    if (seg_file.empty()) {
        std::cerr << parser;
        return 1;
    }

    std::string workingset_file = args::get(seg_arg);
    workingset_file += ".ws";
    std::ofstream workingset_file_(workingset_file);

    trace_init((trace_file+".trace").c_str(), (trace_file+".kt").c_str());
    std::ifstream seg_file_(seg_file);
    if (seg_file_.fail()) {
        std::cerr << "Trace file does not exist" << std::endl;
        AbruptExit(__FILE__, __LINE__);
    }

    seg cur_seg(0, 0);
    uint64_t id = 0;
    uint64_t last_id = 0;
    HMTTTransaction tmp;
    std::vector<WorkingSet> wsets;

    while(seg_file_>>cur_seg){
        std::unordered_set<uint64_t> ppns;
        unsigned long max_ppns = 0;
        std::pair<uint64_t, uint64_t> max_id;
        uint64_t lasting = 0;

        if(start != -1 ){
            if(cur_seg.eid < start || cur_seg.sid > start){
                continue;
            }
        }

        uint64_t sid = cur_seg.sid;
        uint64_t eid = cur_seg.eid;
        if(start != -1){
            sid = start;
            eid = start + simulation;
        }

        if(cur_seg.length() > simulation){
            std::cout<<std::dec<<"Next segment is at "<<sid<<"\n";

            while(id < sid){
                GetNextHMTT(tmp, cur_seg.pid, cur_seg.process_num);
                id++;
            }

            last_id = id;
            lasting = 0;

            while(id <= eid){
                GetNextHMTT(tmp, cur_seg.pid, cur_seg.process_num);
                if((id - last_id) < simulation){
                    ppns.insert(tmp.addr >> 12);
                    lasting += tmp.added_ns;
                }else{
                    if(ppns.size() > max_ppns){
                        max_id.first = last_id;
                        max_id.second = id;
                        max_ppns = ppns.size();
                    }
                    wsets.emplace_back(WorkingSet(last_id, id, ppns.size()));
                    wsets.back().time = lasting;
                    lasting = 0;
                    ppns.clear();
                    last_id = id;
                }
                id++;
            }

            std::cout<<"#"<<std::setw(15)<<"#pages"
            <<std::setw(15)<<"Bytes"
            <<std::setw(15)<<"start id\n"
            <<std::dec<<std::setw(15)<<max_ppns
            <<std::setw(15)<<max_ppns*4096
            <<std::setw(15)<<max_id.first
            <<"\n";
        }
    }

    std::stable_sort(wsets.begin(), wsets.end(), std::greater<WorkingSet>());
    workingset_file_<<wsets.size()<<"\n";
    for (auto i = wsets.begin(); i != wsets.end() ; ++i) {
        workingset_file_<<*i;
    }
    std::cout<<"1/2 position: "<<wsets[wsets.size()/2];
}