#ifndef TATUM_COMMON_ANALYSIS_VISITOR_HPP
#define TATUM_COMMON_ANALYSIS_VISITOR_HPP
#include "tatum_error.hpp"
#include "TimingGraph.hpp"
#include "TimingConstraints.hpp"
#include "TimingTags.hpp"

namespace tatum { namespace detail {

/** \file
 *
 * Common analysis functionality for both setup and hold analysis.
 */

/** \class CommonAnalysisVisitor
 *
 * A class satisfying the GraphVisitor concept, which contains common
 * node and edge processing code used by both setup and hold analysis.
 *
 * \see GraphVisitor
 *
 * \tparam AnalysisOps a class defining the setup/hold specific operations
 * \see SetupAnalysisOps
 * \see HoldAnalysisOps
 */
template<class AnalysisOps>
class CommonAnalysisVisitor {
    public:
        CommonAnalysisVisitor(size_t num_tags)
            : ops_(num_tags) { }

        void do_arrival_pre_traverse_node(const TimingGraph& tg, const TimingConstraints& tc, const NodeId node_id);
        void do_required_pre_traverse_node(const TimingGraph& tg, const TimingConstraints& tc, const NodeId node_id);

        template<class DelayCalc>
        void do_arrival_traverse_node(const TimingGraph& tg, const TimingConstraints& tc, const DelayCalc& dc, const NodeId node_id);

        template<class DelayCalc>
        void do_required_traverse_node(const TimingGraph& tg, const TimingConstraints& tc, const DelayCalc& dc, const NodeId node_id);

        void reset() { ops_.reset(); }

    protected:
        AnalysisOps ops_;

    private:
        template<class DelayCalc>
        void do_arrival_traverse_edge(const TimingGraph& tg, const TimingConstraints& tc, const DelayCalc& dc, const NodeId node_id, const EdgeId edge_id);

        template<class DelayCalc>
        void do_required_traverse_edge(const TimingGraph& tg, const DelayCalc& dc, const NodeId node_id, const EdgeId edge_id);

        bool should_propagate_clock_arr(const TimingGraph& tg, const TimingConstraints& tc, const EdgeId edge_id) const;
        bool is_clock_data_launch_edge(const TimingGraph& tg, const EdgeId edge_id) const;
        bool is_clock_data_capture_edge(const TimingGraph& tg, const EdgeId edge_id) const;

};

/*
 * Pre-traversal
 */

template<class AnalysisOps>
void CommonAnalysisVisitor<AnalysisOps>::do_arrival_pre_traverse_node(const TimingGraph& tg, const TimingConstraints& tc, const NodeId node_id) {
    //Logical Input
    TATUM_ASSERT_MSG(tg.node_in_edges(node_id).size() == 0, "Logical input has input edges: timing graph not levelized.");

    NodeType node_type = tg.node_type(node_id);

    if(tc.node_is_constant_generator(node_id)) {
        //We don't propagate any tags from constant generators,
        //since they do not effect the dynamic timing behaviour of the
        //system
        return;
    }

    TATUM_ASSERT(node_type == NodeType::SOURCE);

    if(tc.node_is_clock_source(node_id)) {
        //Generate the appropriate clock tag

        TATUM_ASSERT_MSG(ops_.get_launch_clock_tags(node_id).num_tags() == 0, "Clock source already has launch clock tags");
        TATUM_ASSERT_MSG(ops_.get_capture_clock_tags(node_id).num_tags() == 0, "Clock source already has capture clock tags");

        //Find it's domain
        DomainId domain_id = tc.node_clock_domain(node_id);
        TATUM_ASSERT(domain_id);

        //Initialize a clock tag with zero arrival, invalid required time
        //
        //Note: we assume that edge counting has set the effective period constraint assuming a
        //launch edge at time zero.  This means we don't need to do anything special for clocks
        //with rising edges after time zero.
        TimingTag launch_tag = TimingTag(Time(0.), Time(NAN), domain_id, node_id, TagType::CLOCK_LAUNCH);
        TimingTag capture_tag = TimingTag(Time(0.), Time(NAN), domain_id, node_id, TagType::CLOCK_CAPTURE);

        //Add the tag
        ops_.get_launch_clock_tags(node_id).add_tag(launch_tag);
        ops_.get_capture_clock_tags(node_id).add_tag(capture_tag);

    } else {

        //A standard primary input, generate the appropriate data tag

        TATUM_ASSERT_MSG(ops_.get_data_tags(node_id).num_tags() == 0, "Primary input already has data tags");

        DomainId domain_id = tc.node_clock_domain(node_id);
        TATUM_ASSERT(domain_id);

        float input_constraint = tc.input_constraint(node_id, domain_id);
        TATUM_ASSERT(!isnan(input_constraint));

        //Initialize a data tag based on input delay constraint, invalid required time
        TimingTag input_tag = TimingTag(Time(input_constraint), Time(NAN), domain_id, node_id, TagType::DATA);

        ops_.get_data_tags(node_id).add_tag(input_tag);
    }
}

template<class AnalysisOps>
void CommonAnalysisVisitor<AnalysisOps>::do_required_pre_traverse_node(const TimingGraph& tg, const TimingConstraints& tc, const NodeId node_id) {

    TimingTags& node_data_tags = ops_.get_data_tags(node_id);
    TimingTags& node_clock_tags = ops_.get_capture_clock_tags(node_id);

    /*
     * Calculate required times
     */

    TATUM_ASSERT(tg.node_type(node_id) == NodeType::SINK);

    //Sinks corresponding to FF sinks will have propagated (capturing) clock tags,
    //while those corresponding to outpads will not. To treat them uniformly, we 
    //initialize outpads with capturing clock tags based on the ouput constraints.
    if(node_clock_tags.empty()) {
        //Initialize the clock tags based on the constraints.

        auto output_constraints = tc.output_constraints(node_id);

        if(output_constraints.empty()) {
            //throw tatum::Error("Output unconstrained");
            std::cerr << "Warning: Timing graph " << node_id << " " << tg.node_type(node_id);
            std::cerr << " has no incomming clock tags, and no output constraint. No required time will be calculated\n";
        } else {
            DomainId domain_id = tc.node_clock_domain(node_id);
            TATUM_ASSERT(domain_id);

            float output_constraint = tc.output_constraint(node_id, domain_id);
            TATUM_ASSERT(!isnan(output_constraint));

            for(auto constraint : output_constraints) {
                TimingTag constraint_tag = TimingTag(Time(output_constraint), Time(NAN), constraint.second.domain, node_id, TagType::CLOCK_CAPTURE);
                node_clock_tags.add_tag(constraint_tag);
            }
        }
    }

    //At this point the sink will have a capturing clock tag defined

    //Determine the required time at this sink
    //
    //We need to generate a required time for each clock domain for which there is a data
    //arrival time at this node, while considering all possible clocks that could drive
    //this node (i.e. take the most restrictive constraint accross all clock tags at this
    //node)
    for(TimingTag& node_data_tag : node_data_tags) {
        for(const TimingTag& node_clock_tag : node_clock_tags) {

            //Should we be analyzing paths between these two domains?
            if(tc.should_analyze(node_data_tag.clock_domain(), node_clock_tag.clock_domain())) {

                //We only set a required time if the source domain actually reaches this sink
                //domain.  This is indicated by having a valid arrival time.
                if(node_data_tag.arr_time().valid()) {
                    float clock_constraint = ops_.clock_constraint(tc, node_data_tag.clock_domain(),
                                                                  node_clock_tag.clock_domain());

                    //Update the required time. This will keep the most restrictive constraint.
                    ops_.merge_req_tag(node_data_tag, node_clock_tag.arr_time() + Time(clock_constraint), node_data_tag);
                }
            }
        }
    }
}

/*
 * Arrival Time Operations
 */

template<class AnalysisOps>
template<class DelayCalc>
void CommonAnalysisVisitor<AnalysisOps>::do_arrival_traverse_node(const TimingGraph& tg, const TimingConstraints& tc, const DelayCalc& dc, NodeId node_id) {
    //Do not propagate arrival tags through constant generators
    if(tc.node_is_constant_generator(node_id)) return;

    //Pull from upstream sources to current node
    for(EdgeId edge_id : tg.node_in_edges(node_id)) {
        do_arrival_traverse_edge(tg, tc, dc, node_id, edge_id);
    }
}

template<class AnalysisOps>
template<class DelayCalc>
void CommonAnalysisVisitor<AnalysisOps>::do_arrival_traverse_edge(const TimingGraph& tg, const TimingConstraints& tc, const DelayCalc& dc, const NodeId node_id, const EdgeId edge_id) {
    //We must use the tags by reference so we don't accidentally wipe-out any
    //existing tags
    TimingTags& node_data_tags = ops_.get_data_tags(node_id);

    //Pulling values from upstream source node
    NodeId src_node_id = tg.edge_src_node(edge_id);

    if(should_propagate_clock_arr(tg, tc, edge_id)) {
        /*
         * Clock tags
         */

        //Propagate the clock tags through the clock network

        //The launch tags
        if(!is_clock_data_capture_edge(tg, edge_id)) {
            const TimingTags& src_launch_clk_tags = ops_.get_launch_clock_tags(src_node_id);
            TimingTags& node_launch_clk_tags = ops_.get_launch_clock_tags(node_id);

            const Time& clk_launch_edge_delay = ops_.launch_clock_edge_delay(dc, tg, edge_id);

            for(const TimingTag& src_launch_clk_tag : src_launch_clk_tags) {
                //Standard propagation through the clock network

                Time new_arr = src_launch_clk_tag.arr_time() + clk_launch_edge_delay;
                ops_.merge_arr_tags(node_launch_clk_tags, new_arr, src_launch_clk_tag);

                if(is_clock_data_launch_edge(tg, edge_id)) {
                    //We convert the clock arrival time to a data
                    //arrival time at this node (since the clock's
                    //arrival launches the data).
                    TATUM_ASSERT(tg.node_type(node_id) == NodeType::SOURCE);

                    //Make a copy of the tag
                    TimingTag launch_tag = src_launch_clk_tag;

                    //Update the launch node, since the data is
                    //launching from this node
                    launch_tag.set_launch_node(node_id);

                    //Mark propagated launch time as a DATA tag
                    ops_.merge_arr_tags(node_data_tags, new_arr, launch_tag);
                }
            }
        }

        //The capture tags
        if(!is_clock_data_launch_edge(tg, edge_id)) {
            const TimingTags& src_capture_clk_tags = ops_.get_capture_clock_tags(src_node_id);
            TimingTags& node_capture_clk_tags = ops_.get_capture_clock_tags(node_id);

            const Time& clk_capture_edge_delay = ops_.capture_clock_edge_delay(dc, tg, edge_id);

            for(const TimingTag& src_capture_clk_tag : src_capture_clk_tags) {
                //Standard propagation through the clock network
                ops_.merge_arr_tags(node_capture_clk_tags, src_capture_clk_tag.arr_time() + clk_capture_edge_delay, src_capture_clk_tag);
            }
        }
    }

    /*
     * Data tags
     */

    const TimingTags& src_data_tags = ops_.get_data_tags(src_node_id);

    if(!src_data_tags.empty()) {
        const Time& edge_delay = ops_.data_edge_delay(dc, tg, edge_id);
        TATUM_ASSERT(edge_delay.valid());


        for(const TimingTag& src_data_tag : src_data_tags) {
            //Standard data-path propagation
            ops_.merge_arr_tags(node_data_tags, src_data_tag.arr_time() + edge_delay, src_data_tag);
        }
    }
}

/*
 * Required Time Operations
 */


template<class AnalysisOps>
template<class DelayCalc>
void CommonAnalysisVisitor<AnalysisOps>::do_required_traverse_node(const TimingGraph& tg, const TimingConstraints& tc, const DelayCalc& dc, const NodeId node_id) {
    //Don't propagate required times through the clock network
    if(tg.node_type(node_id) == NodeType::CPIN) return;

    //Do not propagate required tags through constant generators
    //if(tc.node_is_constant_generator(node_id)) return;

    //Pull from downstream sinks to current node
    for(EdgeId edge_id : tg.node_out_edges(node_id)) {
        do_required_traverse_edge(tg, dc, node_id, edge_id);
    }
}

template<class AnalysisOps>
template<class DelayCalc>
void CommonAnalysisVisitor<AnalysisOps>::do_required_traverse_edge(const TimingGraph& tg, const DelayCalc& dc, const NodeId node_id, const EdgeId edge_id) {

    //Pulling values from downstream sink node
    NodeId sink_node_id = tg.edge_sink_node(edge_id);

    const TimingTags& sink_data_tags = ops_.get_data_tags(sink_node_id);

    if(!sink_data_tags.empty()) {
        const Time& edge_delay = ops_.data_edge_delay(dc, tg, edge_id);
        TATUM_ASSERT(edge_delay.valid());

        //We must use the tags by reference so we don't accidentally wipe-out any
        //existing tags
        TimingTags& node_data_tags = ops_.get_data_tags(node_id);

        for(const TimingTag& sink_tag : sink_data_tags) {
            //We only propogate the required time if we have a valid arrival time
            auto matched_tag_iter = node_data_tags.find_tag_by_clock_domain(sink_tag.clock_domain());
            if(matched_tag_iter != node_data_tags.end() && matched_tag_iter->arr_time().valid()) {
                //Valid arrival, update required
                ops_.merge_req_tag(*matched_tag_iter, sink_tag.req_time() - edge_delay, sink_tag);
            }
        }
    }
}

template<class AnalysisOps>
bool CommonAnalysisVisitor<AnalysisOps>::should_propagate_clock_arr(const TimingGraph& tg, const TimingConstraints& tc, const EdgeId edge_id) const {
    //We want to propagate clock tags through the arbitrary nodes making up the clock network until 
    //we hit another source node (i.e. a FF's output source).
    //
    //To allow tags to propagte from the original source (i.e. the input clock pin) we also allow
    //propagation from defined clock sources
    NodeId src_node_id = tg.edge_src_node(edge_id);
    NodeType src_node_type = tg.node_type(src_node_id);
    if (src_node_type != NodeType::SOURCE) {
        //Not a source, allow propagation
        return true;
    } else if (tc.node_is_clock_source(src_node_id)) {
        //Base-case: the source is a clock source
        TATUM_ASSERT_MSG(src_node_type == NodeType::SOURCE, "Only SOURCEs can be clock sources");
        TATUM_ASSERT_MSG(tg.node_in_edges(src_node_id).empty(), "Clock sources should have no incoming edges");
        return true;
    }
    return false;
}

template<class AnalysisOps>
bool CommonAnalysisVisitor<AnalysisOps>::is_clock_data_launch_edge(const TimingGraph& tg, const EdgeId edge_id) const {
    NodeId edge_src_node = tg.edge_src_node(edge_id);
    NodeId edge_sink_node = tg.edge_sink_node(edge_id);

    return (tg.node_type(edge_src_node) == NodeType::CPIN) && (tg.node_type(edge_sink_node) == NodeType::SOURCE);
}

template<class AnalysisOps>
bool CommonAnalysisVisitor<AnalysisOps>::is_clock_data_capture_edge(const TimingGraph& tg, const EdgeId edge_id) const {
    NodeId edge_src_node = tg.edge_src_node(edge_id);
    NodeId edge_sink_node = tg.edge_sink_node(edge_id);

    return (tg.node_type(edge_src_node) == NodeType::CPIN) && (tg.node_type(edge_sink_node) == NodeType::SINK);
}

}} //namepsace

#endif
