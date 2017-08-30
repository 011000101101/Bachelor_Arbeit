#include <algorithm>
#include <unordered_set>
#include <queue>
#include <numeric>

#include "atom_netlist.h"

#include "vtr_assert.h"
#include "vtr_log.h"
#include "vpr_error.h"


/*
 *
 *
 * AtomNetlist Class Implementation
 *
 *
 */
AtomNetlist::AtomNetlist(std::string name, std::string id)
    : Netlist<AtomBlockId, AtomPortId, AtomPinId, AtomNetId>(name, id) {}

/*
 *
 * Blocks
 *
 */
AtomBlockType AtomNetlist::block_type (const AtomBlockId id) const {
	const t_model* blk_model = block_model(id);

	AtomBlockType type = AtomBlockType::BLOCK;
	if (blk_model->name == std::string("input")) {
		type = AtomBlockType::INPAD;
	}
	else if (blk_model->name == std::string("output")) {
		type = AtomBlockType::OUTPAD;
	}
	else {
		type = AtomBlockType::BLOCK;
	}
	return type;
}

const t_model* AtomNetlist::block_model(const AtomBlockId id) const {
	VTR_ASSERT(valid_block_id(id));

	return block_models_[id];
}

const AtomNetlist::TruthTable& AtomNetlist::block_truth_table (const AtomBlockId id) const {
    VTR_ASSERT(valid_block_id(id));

    return block_truth_tables_[id];
}

/*
 *
 * Ports
 *
 */
const t_model_ports* AtomNetlist::port_model(const AtomPortId id) const {
	VTR_ASSERT(valid_port_id(id));

	return port_models_[id];
}


/*
 *
 * Pins
 *
 */
PinType AtomNetlist::pin_type(const AtomPinId id) const {
	VTR_ASSERT(valid_pin_id(id));

	AtomPortId port_id = pin_port(id);
	PinType type;
	switch (port_type(port_id)) {
		case PortType::INPUT: /*fallthrough */;
		case PortType::CLOCK: type = PinType::SINK; break;
		case PortType::OUTPUT: type = PinType::DRIVER; break;
		default: VTR_ASSERT_MSG(false, "Valid atom port type");
	}
	return type;
}


/*
*
* Lookups
*
*/
AtomPortId AtomNetlist::find_atom_port(const AtomBlockId blk_id, const t_model_ports* model_port) const {
	VTR_ASSERT(valid_block_id(blk_id));
	VTR_ASSERT(model_port);

	//We can tell from the model port the set of ports
	//the port can be found in
	port_range range = (model_port->dir == IN_PORT) ?
		(model_port->is_clock) ? block_clock_ports(blk_id) : block_input_ports(blk_id)
		: (block_output_ports(blk_id));

	for (auto port_id : range) {
		if (port_name(port_id) == model_port->name) {
			return port_id;
		}
	}

	return AtomPortId::INVALID();
}

/*
 *
 * Mutators
 *
 */
AtomBlockId AtomNetlist::create_block(const std::string name, const t_model* model, const TruthTable truth_table) {
	AtomBlockId blk_id = Netlist::create_block(name);
	
    //Initialize the data
	block_models_.push_back(model);
    block_truth_tables_.push_back(truth_table);

	//Check post-conditions: size
	VTR_ASSERT(validate_block_sizes());

	//Check post-conditions: values
	VTR_ASSERT(block_model(blk_id) == model);
    VTR_ASSERT(block_truth_table(blk_id) == truth_table);

    return blk_id;
}

AtomPortId AtomNetlist::create_port(const AtomBlockId blk_id, const t_model_ports* model_port) {
	AtomPortId port_id = find_port(blk_id, model_port->name);

	PortType type = PortType::INPUT;
	//Determine the PortType
	if (model_port->dir == IN_PORT) {
		if (model_port->is_clock) {
			type = PortType::CLOCK;
		}
		else {
			type = PortType::INPUT;
		}
	}
	else if (model_port->dir == OUT_PORT) {
		type = PortType::OUTPUT;
	}
	else {
		VPR_THROW(VPR_ERROR_ATOM_NETLIST, "Unrecognized model port type");
	}

	if (!port_id) {
		port_id = Netlist::create_port(blk_id, model_port->name, model_port->size, type);

		port_models_.push_back(model_port);
		associate_port_with_block(port_id, port_type(port_id), blk_id);
	}

	//Check post-conditions: size
	VTR_ASSERT(validate_port_sizes());

	//Check post-conditions: values
	VTR_ASSERT(port_name(port_id) == model_port->name);
	VTR_ASSERT(port_width(port_id) == (unsigned)model_port->size);
	VTR_ASSERT(port_model(port_id) == model_port);
	VTR_ASSERT(port_type(port_id) == type);
	VTR_ASSERT_SAFE(find_port(blk_id, model_port->name) == port_id);
	VTR_ASSERT_SAFE(find_atom_port(blk_id, model_port) == port_id);

	return port_id;
}

AtomPinId AtomNetlist::create_pin(const AtomPortId port_id, BitIndex port_bit, const AtomNetId net_id, const PinType pin_type_, bool is_const) {
	AtomPinId pin_id = Netlist::create_pin(port_id, port_bit, net_id, pin_type_, is_const);
	
	//Check post-conditions: size
	VTR_ASSERT(validate_pin_sizes());

	VTR_ASSERT(pin_type(pin_id) == pin_type_);
	VTR_ASSERT(pin_port(pin_id) == port_id);
	VTR_ASSERT(pin_port_type(pin_id) == port_type(port_id));

	return pin_id;
}

AtomNetId AtomNetlist::create_net(const std::string name) {
	AtomNetId net_id = Netlist::create_net(name);

	//Check post-conditions: size
	VTR_ASSERT(validate_net_sizes());

	return net_id;
}

AtomNetId AtomNetlist::add_net(const std::string name, AtomPinId driver, std::vector<AtomPinId> sinks) {
	return Netlist::add_net(name, driver, sinks);
}

void AtomNetlist::remove_block_impl(const AtomBlockId blk_id) {
	VTR_ASSERT(blk_id);
}

void AtomNetlist::remove_port_impl(const AtomPortId port_id) {
	VTR_ASSERT(port_id);
}

void AtomNetlist::remove_pin_impl(const AtomPinId pin_id) {
	VTR_ASSERT(pin_id);
}

void AtomNetlist::remove_net_impl(const AtomNetId net_id) {
	VTR_ASSERT(net_id);
}

/*
*
* Internal utilities
*
*/

void AtomNetlist::clean_blocks_impl(const vtr::vector_map<AtomBlockId,AtomBlockId>& block_id_map) {
    //Update all the block_models and block
    block_models_ = clean_and_reorder_values(block_models_, block_id_map);
    block_truth_tables_ = clean_and_reorder_values(block_truth_tables_, block_id_map);
}

void AtomNetlist::clean_ports_impl(const vtr::vector_map<AtomPortId,AtomPortId>& port_id_map) {
    //Update all port_models_ values
    port_models_ = clean_and_reorder_values(port_models_, port_id_map);
}

void AtomNetlist::clean_pins_impl(const vtr::vector_map<AtomPinId, AtomPinId>& pin_id_map) {
	unsigned unused(pin_id_map.size()); //Remove unused parameter warning
	unused = unused;
}

void AtomNetlist::clean_nets_impl(const vtr::vector_map<AtomNetId, AtomNetId>& net_id_map) {
	unsigned unused(net_id_map.size()); //Remove unused parameter warning
	unused = unused;
}

void AtomNetlist::shrink_to_fit_impl() {
    //Block data
    block_models_.shrink_to_fit();

    //Port data
    port_models_.shrink_to_fit();
}

/*
 *
 * Sanity Checks
 *
 */
bool AtomNetlist::validate_block_sizes_impl() const {
	if (block_truth_tables_.size() != block_ids_.size()
		|| block_models_.size() != block_ids_.size()) {
        VPR_THROW(VPR_ERROR_ATOM_NETLIST, "Inconsistent block data sizes");
    }

	return true;
}

bool AtomNetlist::validate_port_sizes_impl() const {
	if (port_models_.size() != port_ids_.size()) {
		VPR_THROW(VPR_ERROR_ATOM_NETLIST, "Inconsistent port data sizes");
	}
	return true;
}

bool AtomNetlist::validate_pin_sizes_impl() const {
	return true;
}

bool AtomNetlist::validate_net_sizes_impl() const {
	return true;
}
