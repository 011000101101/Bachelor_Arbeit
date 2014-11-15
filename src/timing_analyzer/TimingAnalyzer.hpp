#ifndef TimingAnalyzer_hpp
#define TimingAnalyzer_hpp

class TimingGraph; //Forward declaration

class TimingAnalyzer {
    public:
        virtual void calculate_timing(TimingGraph& timing_graph) = 0;
        virtual void reset_timing(TimingGraph& timing_graph) = 0;
};

#endif
