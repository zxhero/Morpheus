#include <iostream>
#include "./../ext/headers/args.hxx"
#include "cpu.h"
#include <fstream>

using namespace dramsim3;

int main(int argc, const char **argv) {
    args::ArgumentParser parser(
        "DRAM Simulator.",
        "Examples: \n."
        "./build/dramsim3main configs/DDR4_8Gb_x8_3200.ini -c 100 -t "
        "sample_trace.txt\n"
        "./build/dramsim3main configs/DDR4_8Gb_x8_3200.ini -s random -c 100");
    args::HelpFlag help(parser, "help", "Display the help menu", {'h', "help"});
    args::ValueFlag<uint64_t> num_cycles_arg(parser, "num_cycles",
                                             "Number of cycles to simulate",
                                             {'c', "cycles"}, 100000);
    args::ValueFlag<std::string> output_dir_arg(
        parser, "output_dir", "Output directory for stats files",
        {'o', "output-dir"}, ".");
    args::ValueFlag<std::string> stream_arg(
        parser, "stream_type", "address stream generator - (random), stream",
        {'s', "stream"}, "");
    args::ValueFlag<std::string> trace_file_arg(
        parser, "trace",
        "Trace file, setting this option will ignore -s option",
        {'t', "trace"});
    args::ValueFlag<std::string> seg_file_arg(
        parser, "segment",
        "segment file",
        {'S', "seg"});
    args::Positional<std::string> config_arg(
        parser, "config", "The config file name (mandatory)");

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

    std::string config_file = args::get(config_arg);
    if (config_file.empty()) {
        std::cerr << parser;
        return 1;
    }

    uint64_t cycles = args::get(num_cycles_arg);
    std::string output_dir = args::get(output_dir_arg);
    std::string trace_file = args::get(trace_file_arg);
    std::string stream_type = args::get(stream_arg);
    std::string seg_file = args::get(seg_file_arg);
    std::string pid_file = output_dir + "/pid";
    std::ifstream pid_file_(pid_file);
    if (pid_file_.fail()) {
        std::cerr << "pid file does not exist" << std::endl;
        AbruptExit(__FILE__, __LINE__);
    }
    int ppid; uint64_t num_p;
    pid_file_>>ppid>>num_p;
    pid_file_.close();


    HMTTCPU *cpu;
    if(!trace_file.empty() && !seg_file.empty()){
        std::cout<<seg_file<<"\n";
        cpu = new HMTTCPU(config_file, output_dir, trace_file, seg_file, ppid, num_p);
    } else {
        std::cerr << "Trace file and segment file does not provided" << std::endl;
        AbruptExit(__FILE__, __LINE__);
    }

    cpu->WarmUp();
    for (uint64_t clk = 0; clk < cycles && (!(cpu)->IsEnd()); clk++) {
        cpu->ClockTick();
        if(clk % 1000000 == 0){
            std::cout<<"processing "<<std::dec<<clk<<" clks and "<<(cpu)->GetTraceNum()<<" traces "
            <<(cpu)->GetClk()<<" wall clks\n"<<std::flush;
        }
    }
    (cpu)->Drained();
    cpu->PrintStats();

    delete (cpu);

    return 0;
}
