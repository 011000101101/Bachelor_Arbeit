#include <cmath>
#include "atom_delay_calc.h"
/*
 * AtomDelayCalc
 */
inline AtomDelayCalc::AtomDelayCalc(const AtomNetlist& netlist, const AtomLookup& netlist_lookup)
    : netlist_(netlist)
    , netlist_lookup_(netlist_lookup) {}

inline float AtomDelayCalc::atom_combinational_delay(const AtomPinId src_pin, const AtomPinId sink_pin) const {
    VTR_ASSERT_MSG(netlist_.pin_block(src_pin) == netlist_.pin_block(sink_pin), "Combinational primitive delay must be between pins on the same block");

    VTR_ASSERT_MSG(   netlist_.port_type(netlist_.pin_port(src_pin)) == AtomPortType::INPUT 
                   && netlist_.port_type(netlist_.pin_port(sink_pin)) == AtomPortType::OUTPUT,
                   "Combinational connections must go from primitive input to output");

    //Determine the combinational delay from the pb_graph_pin.
    //  By convention the delay is annotated on the corresponding input pg_graph_pin
    const t_pb_graph_pin* src_gpin = find_pb_graph_pin(src_pin);
    const t_pb_graph_pin* sink_gpin = find_pb_graph_pin(sink_pin);
    VTR_ASSERT(src_gpin->num_pin_timing > 0);

    //Find the annotation on the source that matches the sink
    float delay = NAN;
    for(int i = 0; i < src_gpin->num_pin_timing; ++i) {
        const t_pb_graph_pin* timing_sink_gpin = src_gpin->pin_timing[i]; 

        if(timing_sink_gpin == sink_gpin) {
            delay = src_gpin->pin_timing_del_max[i];
            break;
        }
    }

    VTR_ASSERT_MSG(!std::isnan(delay), "Must have a valid delay");

    return delay;
}

inline float AtomDelayCalc::atom_setup_time(const AtomPinId /*clock_pin*/, const AtomPinId input_pin) const {
    VTR_ASSERT(netlist_.port_type(netlist_.pin_port(input_pin)) == AtomPortType::INPUT);

    const t_pb_graph_pin* gpin = find_pb_graph_pin(input_pin);
    VTR_ASSERT(gpin->type == PB_PIN_SEQUENTIAL);

    return gpin->tsu_tco;
}

inline float AtomDelayCalc::atom_clock_to_q_delay(const AtomPinId /*clock_pin*/, const AtomPinId output_pin) const {
    VTR_ASSERT(netlist_.port_type(netlist_.pin_port(output_pin)) == AtomPortType::OUTPUT);

    const t_pb_graph_pin* gpin = find_pb_graph_pin(output_pin);
    VTR_ASSERT(gpin->type == PB_PIN_SEQUENTIAL);

    return gpin->tsu_tco;
}

inline const t_pb_graph_pin* AtomDelayCalc::find_pb_graph_pin(const AtomPinId atom_pin) const {
    const t_pb_graph_pin* gpin = netlist_lookup_.atom_pin_pb_graph_pin(atom_pin);
    VTR_ASSERT(gpin != nullptr);

    return gpin;
}

