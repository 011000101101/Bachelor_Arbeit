template<class AnalysisType, class DelayCalcType, class TagPoolType>
SerialTimingAnalyzer<AnalysisType,DelayCalcType,TagPoolType>::SerialTimingAnalyzer(const TimingGraph& tg, const TimingConstraints& tc, const DelayCalcType& dc)
    : tg_(tg)
    , tc_(tc)
    , dc_(dc)
    //Need to give the size of the object to allocate
    , tag_pool_(sizeof(TimingTag)) {
    AnalysisType::initialize_traversal(tg_);
}

template<class AnalysisType, class DelayCalcType, class TagPoolType>
ta_runtime SerialTimingAnalyzer<AnalysisType,DelayCalcType,TagPoolType>::calculate_timing() {
    struct timespec start_times[4];
    struct timespec end_times[4];

    clock_gettime(CLOCK_MONOTONIC, &start_times[0]);
    pre_traversal(tg_, tc_);
    clock_gettime(CLOCK_MONOTONIC, &end_times[0]);

    clock_gettime(CLOCK_MONOTONIC, &start_times[1]);
    forward_traversal(tg_, tc_);
    clock_gettime(CLOCK_MONOTONIC, &end_times[1]);

    clock_gettime(CLOCK_MONOTONIC, &start_times[2]);
    backward_traversal(tg_);
    clock_gettime(CLOCK_MONOTONIC, &end_times[2]);

    ta_runtime traversal_times;
    traversal_times.pre_traversal = time_sec(start_times[0], end_times[0]);
    traversal_times.fwd_traversal = time_sec(start_times[1], end_times[1]);
    traversal_times.bck_traversal = time_sec(start_times[2], end_times[2]);

    return traversal_times;
}

template<class AnalysisType, class DelayCalcType, class TagPoolType>
void SerialTimingAnalyzer<AnalysisType,DelayCalcType,TagPoolType>::reset_timing() {
    //Release the memory allocated to tags
    tag_pool_.purge_memory();

    AnalysisType::initialize_traversal(tg_);
}

template<class AnalysisType, class DelayCalcType, class TagPoolType>
void SerialTimingAnalyzer<AnalysisType,DelayCalcType,TagPoolType>::pre_traversal(const TimingGraph& timing_graph, const TimingConstraints& timing_constraints) {
    /*
     * The pre-traversal sets up the timing graph for propagating arrival
     * and required times.
     * Steps performed include:
     *   - Initialize arrival times on primary inputs
     */
    for(NodeId node_id : timing_graph.primary_inputs()) {
        AnalysisType::pre_traverse_node(tag_pool_, timing_graph, timing_constraints, node_id);
    }
}

template<class AnalysisType, class DelayCalcType, class TagPoolType>
void SerialTimingAnalyzer<AnalysisType,DelayCalcType,TagPoolType>::forward_traversal(const TimingGraph& timing_graph, const TimingConstraints& timing_constraints) {
    //Forward traversal (arrival times)
    for(int level_idx = 1; level_idx < timing_graph.num_levels(); level_idx++) {
        for(NodeId node_id : timing_graph.level(level_idx)) {
            forward_traverse_node(tag_pool_, timing_graph, timing_constraints, node_id);
        }
    }
}

template<class AnalysisType, class DelayCalcType, class TagPoolType>
void SerialTimingAnalyzer<AnalysisType,DelayCalcType,TagPoolType>::backward_traversal(const TimingGraph& timing_graph) {
    //Backward traversal (required times)
    for(int level_idx = timing_graph.num_levels() - 2; level_idx >= 0; level_idx--) {
        for(NodeId node_id : timing_graph.level(level_idx)) {
            backward_traverse_node(timing_graph, node_id);
        }
    }
}

template<class AnalysisType, class DelayCalcType, class TagPoolType>
void SerialTimingAnalyzer<AnalysisType,DelayCalcType,TagPoolType>::forward_traverse_node(TagPoolType& tag_pool, const TimingGraph& tg, const TimingConstraints& tc, const NodeId node_id) {
    //Pull from upstream sources to current node
    for(int edge_idx = 0; edge_idx < tg.num_node_in_edges(node_id); edge_idx++) {
        EdgeId edge_id = tg.node_in_edge(node_id, edge_idx);

        AnalysisType::forward_traverse_edge(tag_pool, tg, dc_, node_id, edge_id);
    }

    AnalysisType::forward_traverse_finalize_node(tag_pool, tg, tc, node_id);
}

template<class AnalysisType, class DelayCalcType, class TagPoolType>
void SerialTimingAnalyzer<AnalysisType,DelayCalcType,TagPoolType>::backward_traverse_node(const TimingGraph& tg, const NodeId node_id) {
    //Pull from downstream sinks to current node

    TN_Type node_type = tg.node_type(node_id);

    //We don't propagate required times past FF_CLOCK nodes,
    //since anything upstream is part of the clock network
    //
    //TODO: if performing optimization on a clock network this may actually be useful
    if(node_type == TN_Type::FF_CLOCK) {
        return;
    }

    //Each back-edge from down stream node
    for(int edge_idx = 0; edge_idx < tg.num_node_out_edges(node_id); edge_idx++) {
        EdgeId edge_id = tg.node_out_edge(node_id, edge_idx);

        AnalysisType::backward_traverse_edge(tg, dc_, node_id, edge_id);
    }
}

