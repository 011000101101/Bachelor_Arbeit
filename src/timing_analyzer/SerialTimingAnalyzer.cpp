#include "SerialTimingAnalyzer.hpp"
#include "TimingGraph.hpp"

#define DEFAULT_CLOCK_PERIOD 1.0e-9

void SerialTimingAnalyzer::calculate_timing(TimingGraph& timing_graph) {
    pre_traversal(timing_graph);
    forward_traversal(timing_graph);
    backward_traversal(timing_graph);
}

void SerialTimingAnalyzer::reset_timing(TimingGraph& timing_graph) {
    for(int node_id = 0; node_id < timing_graph.num_nodes(); node_id++) {
        timing_graph.set_node_arr_time(node_id, Time(NAN));
        timing_graph.set_node_req_time(node_id, Time(NAN));
    }
}

void SerialTimingAnalyzer::pre_traversal(TimingGraph& timing_graph) {
    /*
     * The pre-traversal sets up the timing graph for propagating arrival
     * and required times.
     * Steps performed include:
     *   - Initialize arrival times on primary inputs
     *   - Propogating clock delay to all clock pins
     *   - Initialize required times on primary outputs
     */
    //We perform a BFS from primary inputs to initialize the timing graph
    for(int level_idx = 0; level_idx < timing_graph.num_levels(); level_idx++) {
        for(int node_id : timing_graph.level(level_idx)) {
            pre_traverse_node(timing_graph, node_id, level_idx);
        }
    }
}

void SerialTimingAnalyzer::forward_traversal(TimingGraph& timing_graph) {
    //Forward traversal (arrival times)
    for(int level_idx = 1; level_idx < timing_graph.num_levels(); level_idx++) {
        for(NodeId node_id : timing_graph.level(level_idx)) {
            forward_traverse_node(timing_graph, node_id);
        }
    }
}
    
void SerialTimingAnalyzer::backward_traversal(TimingGraph& timing_graph) {
    //Backward traversal (required times)
    for(int level_idx = timing_graph.num_levels() - 2; level_idx >= 0; level_idx--) {
        for(NodeId node_id : timing_graph.level(level_idx)) {
            backward_traverse_node(timing_graph, node_id);
        }
    }
}

void SerialTimingAnalyzer::pre_traverse_node(TimingGraph& tg, NodeId node_id, int level_idx) {
    if(level_idx == 0) { //Primary Input
        //Initialize with zero arrival time
        tg.set_node_arr_time(node_id, Time(0));
    }

    if(tg.num_node_out_edges(node_id) == 0) { //Primary Output

        //Initialize required time
        //FIXME Currently assuming:
        //   * A single clock
        //   * At fixed frequency
        //   * Non-propogated (i.e. no clock delay/skew)
        tg.set_node_req_time(node_id, Time(DEFAULT_CLOCK_PERIOD));
    }
}

void SerialTimingAnalyzer::forward_traverse_node(TimingGraph& tg, NodeId node_id) {
    //From upstream sources to current node

    Time arr_time;
    for(int edge_idx = 0; edge_idx < tg.num_node_in_edges(node_id); edge_idx++) {
        EdgeId edge_id = tg.node_in_edge(node_id, edge_idx);

        int src_node_id = tg.edge_src_node(edge_id);

        const Time& src_arr_time = tg.node_arr_time(src_node_id);
        const Time& edge_delay = tg.edge_delay(edge_id);

        if(!arr_time.valid()) {
            arr_time = src_arr_time + edge_delay;
        } else {
            arr_time.max(src_arr_time + edge_delay);
        }

    }
    tg.set_node_arr_time(node_id, arr_time);
}

void SerialTimingAnalyzer::backward_traverse_node(TimingGraph& tg, NodeId node_id) {
    //From downstream sinks to current node
    Time req_time = tg.node_req_time(node_id);

    for(int edge_idx = 0; edge_idx < tg.num_node_out_edges(node_id); edge_idx++) {
        EdgeId edge_id = tg.node_out_edge(node_id, edge_idx);
        int sink_node_id = tg.edge_sink_node(edge_id);

        const Time& sink_req_time = tg.node_req_time(sink_node_id);
        const Time& edge_delay = tg.edge_delay(edge_id);

        if(!req_time.valid()) {
            req_time = sink_req_time - edge_delay;
        } else {
            req_time.min(sink_req_time - edge_delay);
        }

    }
    tg.set_node_req_time(node_id, Time(req_time));
}
