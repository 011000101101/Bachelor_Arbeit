#ifndef PLACEMENT_DELAY_CALCULATOR_HPP
#define PLACEMENT_DELAY_CALCULATOR_HPP
#include "Time.hpp"
#include "TimingGraph.hpp"

#include "atom_netlist.h"
#include "vpr_utils.h"

#include "atom_delay_calc.h"
#include "clb_delay_calc.h"

class PlacementDelayCalculator {

public:
    PlacementDelayCalculator(const AtomNetlist& netlist, const AtomMap& netlist_map, float** net_delay);

    tatum::Time max_edge_delay(const tatum::TimingGraph& tg, tatum::EdgeId edge_id) const;
    tatum::Time setup_time(const tatum::TimingGraph& tg, tatum::EdgeId edge_id) const;

    tatum::Time min_edge_delay(const tatum::TimingGraph& tg, tatum::EdgeId edge_id) const;
    tatum::Time hold_time(const tatum::TimingGraph& tg, tatum::EdgeId edge_id) const;

private:

    tatum::Time atom_combinational_delay(const tatum::TimingGraph& tg, tatum::EdgeId edge_id) const;
    tatum::Time atom_setup_time(const tatum::TimingGraph& tg, tatum::EdgeId edge_id) const;
    tatum::Time atom_clock_to_q_delay(const tatum::TimingGraph& tg, tatum::EdgeId edge_id) const;
    tatum::Time atom_net_delay(const tatum::TimingGraph& tg, tatum::EdgeId edge_id) const;

    float inter_cluster_delay(const t_net_pin* driver_clb_pin, const t_net_pin* sink_clb_pin) const;

private:
    const AtomNetlist& netlist_;
    const AtomMap& netlist_map_;
    float** net_delay_;

    ClbDelayCalc clb_delay_calc_;
    CachingAtomDelayCalc atom_delay_calc_;
};

#include "PlacementDelayCalculator.tpp"
#endif
