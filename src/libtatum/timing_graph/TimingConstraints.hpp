#pragma once
#include <map>
#include <vector>

#include "timing_graph_fwd.hpp"

class TimingConstraints {
    public:
        //Getters
        bool should_analyze(DomainId src_domain, DomainId sink_domain) const;
        float setup_clock_constraint(DomainId src_domain, DomainId sink_domain) const;
        float hold_clock_constraint(DomainId src_domain, DomainId sink_domain) const;
        float input_constraint(NodeId node_id) const;
        float output_constraint(NodeId node_id) const;

        //Setters
        void add_setup_clock_constraint(const DomainId src_domain, const DomainId sink_domain, const float constraint);
        void add_hold_clock_constraint(const DomainId src_domain, const DomainId sink_domain, const float constraint);
        void add_input_constraint(const NodeId node_id, const float constraint);
        void add_output_constraint(const NodeId node_id, const float constraint);

        void remap_nodes(const std::vector<NodeId>& node_map);

        void print();
    private:
        //TODO: determine better data structures for this
        //Clock constraints
        std::map<std::pair<DomainId,DomainId>,float> setup_clock_constraints_;
        std::map<std::pair<DomainId,DomainId>,float> hold_clock_constraints_;
        std::map<NodeId,float> input_constraints_;
        std::map<NodeId,float> output_constraints_;
};

