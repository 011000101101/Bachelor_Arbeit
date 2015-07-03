#pragma once

#include "timing_graph_fwd.hpp"
#include "ParallelLevelizedTimingAnalyzer.hpp"

/*
 * DO NOT USE!
 *
 * ParallelNoDependancyTimingAnalyzer implements the TimingGraph interface, but does not produce
 * correct results!
 *
 * This analyzer ignores all dependencies between nodes in the timing graph. This will produce
 * incorrect results, and hence should not be used to do any real timing analysis.
 *
 * Its only use, is providing an upper-bound on the achievable parallel speed-up (since it ignores
 * dependancies it should scale better).
 */
template<class AnalysisType, class DelayCalcType, class TagPoolType=MemoryPool>
class ParallelNoDependancyTimingAnalyzer : public ParallelLevelizedTimingAnalyzer<AnalysisType, DelayCalcType, TagPoolType> {
    public:
        ParallelNoDependancyTimingAnalyzer(const TimingGraph& timing_graph, const TimingConstraints& timing_constraints, const DelayCalcType& delay_calculator): ParallelLevelizedTimingAnalyzer<AnalysisType,DelayCalcType,TagPoolType>(timing_graph, timing_constraints, delay_calculator) {}
    protected:
        /*
         * Propogate arrival times
         */
        void forward_traversal(const TimingGraph& timing_graph, const TimingConstraints& timing_constraints) override;

        /*
         * Propogate required times
         */
        void backward_traversal(const TimingGraph& timing_graph) override;
};

#include "ParallelNoDependancyTimingAnalyzer.tpp"
