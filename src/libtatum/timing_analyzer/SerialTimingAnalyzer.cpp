#include <fstream>
#include <algorithm>
#include "SerialTimingAnalyzer.hpp"
#include "TimingGraph.hpp"

#include "sta_util.hpp"

ta_runtime SerialTimingAnalyzer::calculate_timing(const TimingGraph& timing_graph) {
    struct timespec start_times[4];
    struct timespec end_times[4];

#ifdef SAVE_LEVEL_TIMES
    fwd_start_ = std::vector<struct timespec>(timing_graph.num_levels());
    fwd_end_ = std::vector<struct timespec>(timing_graph.num_levels());
    bck_start_ = std::vector<struct timespec>(timing_graph.num_levels());
    bck_end_ = std::vector<struct timespec>(timing_graph.num_levels());
#endif
    clock_gettime(CLOCK_MONOTONIC, &start_times[0]);
    pre_traversal(timing_graph);
    clock_gettime(CLOCK_MONOTONIC, &end_times[0]);

    clock_gettime(CLOCK_MONOTONIC, &start_times[1]);
    forward_traversal(timing_graph);
    clock_gettime(CLOCK_MONOTONIC, &end_times[1]);

    clock_gettime(CLOCK_MONOTONIC, &start_times[2]);
    backward_traversal(timing_graph);
    clock_gettime(CLOCK_MONOTONIC, &end_times[2]);

    ta_runtime traversal_times;
    traversal_times.pre_traversal = time_sec(start_times[0], end_times[0]);
    traversal_times.fwd_traversal = time_sec(start_times[1], end_times[1]);
    traversal_times.bck_traversal = time_sec(start_times[2], end_times[2]);

    return traversal_times;
}

SerialTimingAnalyzer::~SerialTimingAnalyzer() {
    for(TimingTags& tags : arr_tags_) {
        tags.clear();
    }
    for(TimingTags& tags : req_tags_) {
        tags.clear();
    }
}

void SerialTimingAnalyzer::pre_traversal(const TimingGraph& timing_graph) {

    //Pre-Allocate data structures
    arr_tags_ = std::vector<TimingTags>(timing_graph.num_nodes());
    req_tags_ = std::vector<TimingTags>(timing_graph.num_nodes());

    /*
     * The pre-traversal sets up the timing graph for propagating arrival
     * and required times.
     * Steps performed include:
     *   - Initialize arrival times on primary inputs
     *   - Propogating clock delay to all clock pins
     *   - Initialize required times on primary outputs
     */
    const std::vector<NodeId>& primary_inputs = timing_graph.level(0);
    for(size_t i = 0; i < primary_inputs.size(); i++) {
        pre_traverse_node(timing_graph, primary_inputs[i]);
    }

    const std::vector<NodeId>& primary_outputs = timing_graph.primary_outputs();
    for(size_t i = 0; i < primary_outputs.size(); i++) {
        pre_traverse_node(timing_graph, primary_outputs[i]);
    }
}

void SerialTimingAnalyzer::forward_traversal(const TimingGraph& timing_graph) {
    //Forward traversal (arrival times)
    for(int level_idx = 1; level_idx < timing_graph.num_levels(); level_idx++) {
#ifdef SAVE_LEVEL_TIMES
        clock_gettime(CLOCK_MONOTONIC, &fwd_start_[level_idx]);
#endif
        for(NodeId node_id : timing_graph.level(level_idx)) {
            forward_traverse_node(timing_graph, node_id);
        }
#ifdef SAVE_LEVEL_TIMES
        clock_gettime(CLOCK_MONOTONIC, &fwd_end_[level_idx]);
#endif
    }
}
    
void SerialTimingAnalyzer::backward_traversal(const TimingGraph& timing_graph) {
    //Backward traversal (required times)
    for(int level_idx = timing_graph.num_levels() - 2; level_idx >= 0; level_idx--) {
#ifdef SAVE_LEVEL_TIMES
        clock_gettime(CLOCK_MONOTONIC, &bck_start_[level_idx]);
#endif
        for(NodeId node_id : timing_graph.level(level_idx)) {
            backward_traverse_node(timing_graph, node_id);
        }
#ifdef SAVE_LEVEL_TIMES
        clock_gettime(CLOCK_MONOTONIC, &bck_end_[level_idx]);
#endif
    }
}

void SerialTimingAnalyzer::pre_traverse_node(const TimingGraph& tg, const NodeId node_id) {
    if(tg.num_node_in_edges(node_id) == 0) { //Primary Input
        //Initialize with zero arrival time
        arr_tags_[node_id].add_tag(tag_pool_, Time(0), tg.node_clock_domain(node_id), node_id);
    }

    if(tg.num_node_out_edges(node_id) == 0) { //Primary Output

        //Initialize required time
        //FIXME Currently assuming:
        //   * A single clock
        //   * At fixed frequency
        //   * Non-propogated (i.e. no clock delay/skew)
        req_tags_[node_id].add_tag(tag_pool_, Time(DEFAULT_CLOCK_PERIOD), tg.node_clock_domain(node_id), node_id);
    }
}

void SerialTimingAnalyzer::forward_traverse_node(const TimingGraph& tg, const NodeId node_id) {
    //From upstream sources to current node

    TimingTags& arr_tags = arr_tags_[node_id];
    for(int edge_idx = 0; edge_idx < tg.num_node_in_edges(node_id); edge_idx++) {
        EdgeId edge_id = tg.node_in_edge(node_id, edge_idx);

        int src_node_id = tg.edge_src_node(edge_id);

        const Time& edge_delay = tg.edge_delay(edge_id);

        const TimingTags& src_arr_tags = arr_tags_[src_node_id];

        for(const TimingTag& src_tag : src_arr_tags) {
            arr_tags.max_tag(tag_pool_, src_tag.time() + edge_delay, src_tag.clock_domain(), src_tag.launch_node());
        }
    }
}

void SerialTimingAnalyzer::backward_traverse_node(const TimingGraph& tg, const NodeId node_id) {
    //From downstream sinks to current node

    //We must use the req_tags by reference so we don't accidentally wipe-out any
    //set required times on primary outputs
    TimingTags& req_tags = req_tags_[node_id];

    //Each back-edge from down stream node
    for(int edge_idx = 0; edge_idx < tg.num_node_out_edges(node_id); edge_idx++) {
        EdgeId edge_id = tg.node_out_edge(node_id, edge_idx);

        int sink_node_id = tg.edge_sink_node(edge_id);

        const Time& edge_delay = tg.edge_delay(edge_id);

        const TimingTags& sink_req_tags = req_tags_[sink_node_id];

        for(const TimingTag& sink_tag : sink_req_tags) {
            req_tags.min_tag(tag_pool_, sink_tag.time() - edge_delay, sink_tag.clock_domain(), sink_tag.launch_node());
        }
    }
}

void SerialTimingAnalyzer::save_level_times(const TimingGraph& timing_graph, std::string filename) {
#ifdef SAVE_LEVEL_TIMES
    std::ofstream f(filename.c_str());
    f << "Level," << "Width," << "Fwd_Time," << "Bck_Time" << std::endl;
    for(int i = 0; i < timing_graph.num_levels(); i++) {
        f << i << "," << timing_graph.level(i).size() << "," << time_sec(fwd_start_[i], fwd_end_[i]) << "," << time_sec(bck_start_[i], bck_end_[i]) << std::endl;
    }
#endif
}

const TimingTags& SerialTimingAnalyzer::arrival_tags(NodeId node_id) const {
    return arr_tags_[node_id];
}

const TimingTags& SerialTimingAnalyzer::required_tags(NodeId node_id) const {
    return req_tags_[node_id];
}

void SerialTimingAnalyzer::dump(const TimingGraph& tg) {
    std::cout << "Analyzer Dump: " << std::endl;
    for(int node_id = 0; node_id < (int) tg.num_nodes(); node_id++) {
        std::cout << "Node: " << node_id << " Type: " << tg.node_type(node_id) << std::endl;
        int i = 0;
        for(const TimingTag& tag : arrival_tags(node_id)) {
            std::cout << "\t" << "Arr Tag " << i << ": " << tag.time().value() << std::endl;
            i++;
        }
        int j = 0;
        for(const TimingTag& tag : required_tags(node_id)) {
            std::cout << "\t" << "Req Tag " << j << ": " << tag.time().value() << std::endl;
            j++;
        }
    }
}
