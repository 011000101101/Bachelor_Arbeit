#include <set>

#include "timing_graph_builder.h"
#include "vpr_error.h"
#include "vpr_utils.h"
#include "atom_netlist.h"
#include "vtr_log.h"
#include "loop_detect.hpp"

using tatum::TimingGraph;
using tatum::FixedDelayCalculator;
using tatum::NodeId;
using tatum::NodeType;
using tatum::EdgeId;
using tatum::Time;

template<class K, class V>
tatum::util::linear_map<K,V> remap_valid(const tatum::util::linear_map<K,V>& data, const tatum::util::linear_map<K,K>& id_map) {
    tatum::util::linear_map<K,V> new_data;

    for(size_t i = 0; i < data.size(); ++i) {
        tatum::EdgeId old_edge(i);
        tatum::EdgeId new_edge = id_map[old_edge];

        if(new_edge) {
            new_data.insert(new_edge, data[old_edge]);
        }
    }

    return new_data;
}

TimingGraph TimingGraphBuilder::timing_graph() {
    tg_.levelize();
    tg_.validate();
    return std::move(tg_);
}

ClusteringDelayCalculator TimingGraphBuilder::clustering_delay_calculator(float inter_cluster_net_delay) {
    mark_clustering_net_delays(inter_cluster_net_delay);

    return ClusteringDelayCalculator(max_edge_delays_, setup_times_);
}

void TimingGraphBuilder::build() {
    for(AtomBlockId blk : netlist_.blocks()) {

        AtomBlockType blk_type = netlist_.block_type(blk);

        if(blk_type == AtomBlockType::INPAD || blk_type == AtomBlockType::OUTPAD) {
            add_io_to_timing_graph(blk);

        } else if (blk_type == AtomBlockType::COMBINATIONAL) {
            add_comb_block_to_timing_graph(blk);

        } else if (blk_type == AtomBlockType::SEQUENTIAL) {
            add_seq_block_to_timing_graph(blk);
        } else {
            VPR_THROW(VPR_ERROR_TIMING, "Unrecognized atom block type while constructing timing graph");
        }
    }

    for(AtomNetId net : netlist_.nets()) {
        add_net_to_timing_graph(net);
    }

    fix_comb_loops();
}

void TimingGraphBuilder::add_io_to_timing_graph(const AtomBlockId blk) {

    NodeType node_type;
    AtomPinId pin;
    if(netlist_.block_type(blk) == AtomBlockType::INPAD) {
        node_type = NodeType::SOURCE;

        if(netlist_.block_pins(blk).size() == 1) {
            pin = *netlist_.block_pins(blk).begin();
        } else {
            //Un-swept disconnected input
            VTR_ASSERT(netlist_.block_pins(blk).size() == 0);
            return;
        }

    } else {
        VTR_ASSERT(netlist_.block_type(blk) == AtomBlockType::OUTPAD);
        node_type = NodeType::SINK;

        if(netlist_.block_pins(blk).size() == 1) {
            pin = *netlist_.block_pins(blk).begin();
        } else {
            //Un-swept disconnected output
            VTR_ASSERT(netlist_.block_pins(blk).size() == 0);
            return;
        }
    }

    NodeId tnode = tg_.add_node(node_type);

    netlist_map_.pin_tnode.insert(pin, tnode);
}

void TimingGraphBuilder::add_comb_block_to_timing_graph(const AtomBlockId blk) {
    VTR_ASSERT(netlist_.block_type(blk) == AtomBlockType::COMBINATIONAL);
    VTR_ASSERT(netlist_.block_clock_pins(blk).size() == 0);

    //Track the mapping from pb_graph_pin to output pin id
    std::unordered_map<const t_pb_graph_pin*,AtomPinId> output_pb_graph_pin_to_pin_id;

    //Create the output pins first (so we can make edges from the inputs)
    for(AtomPinId output_pin : netlist_.block_output_pins(blk)) {
        NodeId tnode = tg_.add_node(NodeType::OPIN);

        netlist_map_.pin_tnode.insert(output_pin, tnode);

        const t_pb_graph_pin* pb_gpin = find_pb_graph_pin(output_pin);

        output_pb_graph_pin_to_pin_id[pb_gpin] = output_pin;
    }

    for(AtomPinId input_pin : netlist_.block_input_pins(blk)) {
        NodeId tnode = tg_.add_node(NodeType::IPIN);

        netlist_map_.pin_tnode.insert(input_pin, tnode);

        const t_pb_graph_pin* pb_gpin = find_pb_graph_pin(input_pin);

        //Add the edges from input to outputs
        for(int i = 0; i < pb_gpin->num_pin_timing; ++i) {
            const t_pb_graph_pin* sink_pb_gpin = pb_gpin->pin_timing[i]; 

            auto iter = output_pb_graph_pin_to_pin_id.find(sink_pb_gpin);
            //Some blocks may have disconnected outputs (which are not reflected 
            //in the pg graph), we only add edges to sink pins that exist
            if(iter != output_pb_graph_pin_to_pin_id.end()) { 
                //Sink pin exists, add the edge
                AtomPinId sink_pin = iter->second;
                VTR_ASSERT(sink_pin);

                NodeId sink_tnode = netlist_map_.pin_tnode[sink_pin];
                VTR_ASSERT(sink_tnode);
                
                EdgeId edge = tg_.add_edge(tnode, sink_tnode);

                max_edge_delays_.insert(edge, Time(pb_gpin->pin_timing_del_max[i]));
            }
        }
    }
}

void TimingGraphBuilder::add_seq_block_to_timing_graph(const AtomBlockId blk) {
    VTR_ASSERT(netlist_.block_type(blk) == AtomBlockType::SEQUENTIAL);
    VTR_ASSERT(netlist_.block_clock_pins(blk).size() >= 1);

    //Track the mapping from clock gpin's to pin ID's (used to add edges between clock
    //and source/sink nodes)
    std::unordered_map<const t_pb_graph_pin*,AtomPinId> clock_pb_graph_pin_to_pin_id;

    for(AtomPinId clock_pin : netlist_.block_clock_pins(blk)) {
        NodeId tnode = tg_.add_node(NodeType::CPIN);

        netlist_map_.pin_tnode.insert(clock_pin, tnode);

        const t_pb_graph_pin* pb_gpin = find_pb_graph_pin(clock_pin);
        VTR_ASSERT(pb_gpin->type == PB_PIN_CLOCK);
        clock_pb_graph_pin_to_pin_id[pb_gpin] = clock_pin;
    }

    for(AtomPinId input_pin : netlist_.block_input_pins(blk)) {
        NodeId tnode = tg_.add_node(NodeType::SINK);

        netlist_map_.pin_tnode.insert(input_pin, tnode);

        //Add the edges from the clock to inputs
        const t_pb_graph_pin* gpin = find_pb_graph_pin(input_pin);
        VTR_ASSERT(gpin->type == PB_PIN_SEQUENTIAL);

        const t_pb_graph_pin* clock_gpin = find_associated_clock_pin(input_pin);
        VTR_ASSERT(clock_gpin->type == PB_PIN_CLOCK);

        auto iter = clock_pb_graph_pin_to_pin_id.find(clock_gpin);
        VTR_ASSERT(iter != clock_pb_graph_pin_to_pin_id.end());
        AtomPinId clock_pin = iter->second;
        NodeId clock_tnode = netlist_map_.pin_tnode[clock_pin];

        EdgeId edge = tg_.add_edge(clock_tnode, tnode);

        //Tsu
        setup_times_.insert(edge, Time(gpin->tsu_tco));
    }

    for(AtomPinId output_pin : netlist_.block_output_pins(blk)) {
        NodeId tnode = tg_.add_node(NodeType::SOURCE);

        netlist_map_.pin_tnode.insert(output_pin, tnode);

        AtomPortId port = netlist_.pin_port(output_pin);
        const t_model_ports* port_model = netlist_.port_model(port);

        if(port_model->is_clock && port_model->dir == OUT_PORT) {
            //Clock source
            
            //Pass, nothing else to do, we treat clock sources as
            //primary inputs with no incoming edges
        } else {
            //Regular output

            //Add the edges from the clock to the output
            const t_pb_graph_pin* gpin = find_pb_graph_pin(output_pin);
            VTR_ASSERT(gpin->type == PB_PIN_SEQUENTIAL);

            const t_pb_graph_pin* clock_gpin = find_associated_clock_pin(output_pin);
            VTR_ASSERT(clock_gpin->type == PB_PIN_CLOCK);

            auto iter = clock_pb_graph_pin_to_pin_id.find(clock_gpin);
            VTR_ASSERT(iter != clock_pb_graph_pin_to_pin_id.end());
            AtomPinId clock_pin = iter->second;
            NodeId clock_tnode = netlist_map_.pin_tnode[clock_pin];

            EdgeId edge = tg_.add_edge(clock_tnode, tnode);

            //Tcq
            max_edge_delays_.insert(edge, Time(gpin->tsu_tco));
        }
    }
}

void TimingGraphBuilder::add_net_to_timing_graph(const AtomNetId /*net*/) {
    //No-op
}

void TimingGraphBuilder::fix_comb_loops() {
    auto sccs = tatum::identify_combinational_loops(tg_);

    //For non-simple loops (i.e. SCCs with multiple loops) we may need to break
    //multiple edges, so repeatedly break edges until there are no SCCs left
    while(!sccs.empty()) {
        vtr::printf_warning(__FILE__, __LINE__, "Detected %zu strongly connected component(s) forming combinational loop(s) in timing graph\n", sccs.size());
        for(auto scc : sccs) {
            EdgeId edge_to_break = find_scc_edge_to_break(scc);
            VTR_ASSERT(edge_to_break);

            tg_.disable_edge(edge_to_break);
        }

        sccs = tatum::identify_combinational_loops(tg_);
    }
}

tatum::EdgeId TimingGraphBuilder::find_scc_edge_to_break(std::vector<tatum::NodeId> scc) {
    //Find an edge which is part of the SCC and remove it from the graph,
    //in the hope of breaking the SCC
    std::set<tatum::NodeId> scc_set(scc.begin(), scc.end());
    for(tatum::NodeId src_node : scc) {
        AtomPinId src_pin = netlist_map_.pin_tnode[src_node];
        for(tatum::EdgeId edge : tg_.node_out_edges(src_node)) {
            if(tg_.edge_disabled(edge)) continue;

            tatum::NodeId sink_node = tg_.edge_sink_node(edge);
            AtomPinId sink_pin = netlist_map_.pin_tnode[sink_node];

            if(scc_set.count(sink_node)) {
                vtr::printf_warning(__FILE__, __LINE__, "Arbitrarily disabling timing graph edge %zu (%s -> %s) to break combinational loop\n", 
                                                         edge, netlist_.pin_name(src_pin).c_str(), netlist_.pin_name(sink_pin).c_str());
                return edge;
            }
        }
    }
    return tatum::EdgeId::INVALID();
}

void TimingGraphBuilder::remap_ids(const tatum::GraphIdMaps& id_mapping) {
    //Remap the delays
    max_edge_delays_ = remap_valid(max_edge_delays_, id_mapping.edge_id_map);
    setup_times_ = remap_valid(setup_times_, id_mapping.edge_id_map);

    //Update the pin-tnode mapping
    vtr::bimap<AtomPinId,tatum::NodeId> new_pin_tnode;

    for(auto kv : netlist_map_.pin_tnode) {
        AtomPinId pin = kv.first;
        tatum::NodeId old_tnode = kv.second;
        tatum::NodeId new_tnode = id_mapping.node_id_map[old_tnode];

        new_pin_tnode.insert(pin, new_tnode); 
    }

    netlist_map_.pin_tnode = new_pin_tnode;
}

const t_pb_graph_pin* TimingGraphBuilder::find_pb_graph_pin(const AtomPinId pin) {
    AtomBlockId blk = netlist_.pin_block(pin);

    auto iter = blk_to_pb_gnode_.find(blk);
    VTR_ASSERT(iter != blk_to_pb_gnode_.end());

    const t_pb_graph_node* pb_gnode = iter->second;

    AtomPortId port = netlist_.pin_port(pin);
    const t_model_ports* model_port = netlist_.port_model(port);
    int ipin = netlist_.pin_port_bit(pin);

    const t_pb_graph_pin* gpin = get_pb_graph_node_pin_from_model_port_pin(model_port, ipin, pb_gnode);
    VTR_ASSERT(gpin);

    return gpin;
}

const t_pb_graph_pin* TimingGraphBuilder::find_associated_clock_pin(const AtomPinId io_pin) {
    const t_pb_graph_pin* io_gpin = find_pb_graph_pin(io_pin);

    const t_pb_graph_pin* clock_gpin = io_gpin->associated_clock_pin; 

    if(!clock_gpin) {
        AtomBlockId blk = netlist_.pin_block(io_pin);
        const t_model* model = netlist_.block_model(blk);
        VPR_THROW(VPR_ERROR_TIMING, "Failed to find clock pin associated with pin '%s' (model '%s')", netlist_.pin_name(io_pin).c_str(), model->name);
    }
    return clock_gpin;
}

void TimingGraphBuilder::mark_clustering_net_delays(float inter_cluster_net_delay) {

    //Mark every driver->sink net connection with the inter_cluster_net_delay
    for(AtomNetId net : netlist_.nets()) {
        AtomPinId driver = netlist_.net_driver(net);

        tatum::NodeId driver_tnode = netlist_map_.pin_tnode[driver];

        for(tatum::EdgeId edge : tg_.node_out_edges(driver_tnode)) {
            max_edge_delays_[edge] = Time(inter_cluster_net_delay);
        }
    }

}
