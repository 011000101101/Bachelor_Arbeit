#pragma once

#include "timing_graph_fwd.hpp"
#include "SerialTimingAnalyzer.hpp"



template<class AnalysisType, class DelayCalcType, class TagPoolType=MemoryPool>
class ParallelLevelizedTimingAnalyzer : public SerialTimingAnalyzer<AnalysisType, DelayCalcType, TagPoolType> {
    public:
        ParallelLevelizedTimingAnalyzer(const TimingGraph& timing_graph, const TimingConstraints& timing_constraints, const DelayCalcType& delay_calculator);
        ~ParallelLevelizedTimingAnalyzer() override;
    protected:
        /*
         * Setup the timing graph.
         */
        void pre_traversal(const TimingGraph& timing_graph, const TimingConstraints& timing_constraints) override;

        /*
         * Propogate arrival times
         */
        void forward_traversal(const TimingGraph& timing_graph, const TimingConstraints& timing_constraints) override;

        /*
         * Propogate required times
         */
        void backward_traversal(const TimingGraph& timing_graph) override;

        std::vector<MemoryPool*> tag_pools_;

        size_t parallel_threshold_fwd_;
        size_t parallel_threshold_bck_;
};

#include "ParallelLevelizedTimingAnalyzer.tpp"
