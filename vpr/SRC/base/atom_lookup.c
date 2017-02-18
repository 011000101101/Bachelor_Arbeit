#include "vtr_assert.h"
#include "vtr_log.h"

#include "atom_lookup.h"
/*
 * PB
 */
const t_pb* AtomLookup::atom_pb(const AtomBlockId blk_id) const {
    auto iter = atom_to_pb_.find(blk_id);
    if(iter == atom_to_pb_.end()) {
        //Not found
        return nullptr;
    }
    return iter->second;
}

AtomBlockId AtomLookup::pb_atom(const t_pb* pb) const {
    auto iter = atom_to_pb_.find(pb);
    if(iter == atom_to_pb_.inverse_end()) {
        //Not found
        return AtomBlockId::INVALID();
    }
    return iter->second;
}

const t_pb_graph_node* AtomLookup::atom_pb_graph_node(const AtomBlockId blk_id) const {
    const t_pb* pb = atom_pb(blk_id);
    if(pb) {
        //Found
        return pb->pb_graph_node;
    }
    return nullptr;
}

void AtomLookup::set_atom_pb(const AtomBlockId blk_id, const t_pb* pb) {
    //If either of blk_id or pb are not valid, 
    //remove any mapping

    if(!blk_id) {
        vtr::printf("Setting atom -> pb: INVALID -> %p\n", pb);
    } else {
        vtr::printf("Setting atom -> pb: %zu -> %p\n", size_t(blk_id), pb);
    }

    if(!blk_id && pb) {
        //Remove
        atom_to_pb_.erase(pb);
    } else if(blk_id && !pb) {
        //Remove
        atom_to_pb_.erase(blk_id);
    } else if(blk_id && pb) {
        //If both are valid store the mapping
        atom_to_pb_.update(blk_id, pb);
    }
}

/*
 * PB Pins
 */
const t_pb_graph_pin* AtomLookup::atom_pin_pb_graph_pin(AtomPinId atom_pin) const {
    return atom_pin_to_pb_graph_pin_[atom_pin];
}

void AtomLookup::set_atom_pin_pb_graph_pin(AtomPinId atom_pin, const t_pb_graph_pin* gpin) {
    atom_pin_to_pb_graph_pin_.insert(atom_pin, gpin);
}

/*
 * Blocks
 */
int AtomLookup::atom_clb(const AtomBlockId blk_id) const {
    auto iter = atom_to_clb_.find(blk_id);
    if(iter == atom_to_clb_.end()) {
        return NO_CLUSTER;
    }

    return *iter;
}

void AtomLookup::set_atom_clb(const AtomBlockId blk_id, const int clb_index) {
    VTR_ASSERT(blk_id);


    if(!blk_id) {
        vtr::printf("Setting atom -> clb: INVALID -> %d\n", clb_index);
    } else {
        vtr::printf("Setting atom -> clb: %zu -> %d\n", size_t(blk_id), clb_index);
    }

    //If both are valid store the mapping
    atom_to_clb_.update(blk_id, clb_index);
}

/*
 * Nets
 */
AtomNetId AtomLookup::atom_net(const int clb_net_index) const {
    auto iter = atom_net_to_clb_net_.find(clb_net_index);
    if(iter == atom_net_to_clb_net_.inverse_end()) {
        //Not found
        return AtomNetId::INVALID();
    }
    return iter->second;
}

int AtomLookup::clb_net(const AtomNetId net_id) const {
    auto iter = atom_net_to_clb_net_.find(net_id);
    if(iter == atom_net_to_clb_net_.end()) {
        //Not found
        return OPEN;
    }
    return iter->second;

}


void AtomLookup::set_atom_clb_net(const AtomNetId net_id, const int clb_net_index) {
    VTR_ASSERT(net_id);
    //If either are invalid remove any mapping
    if(!net_id && clb_net_index != OPEN) {
        //Remove
        atom_net_to_clb_net_.erase(clb_net_index);
    } else if(net_id && clb_net_index == OPEN) {
        //Remove
        atom_net_to_clb_net_.erase(net_id);
    } else if (net_id && clb_net_index != OPEN) {
        //Store
        atom_net_to_clb_net_.update(net_id, clb_net_index);
    }
}

/*
 * Classic Timing nodes
 */
AtomPinId AtomLookup::classic_tnode_atom_pin(const int tnode_index) const {
    auto iter = atom_pin_to_classic_tnode_.find(tnode_index);
    if(iter == atom_pin_to_classic_tnode_.inverse_end()) {
        //Not found
        return AtomPinId::INVALID();
    }
    return iter->second;
}

int AtomLookup::atom_pin_classic_tnode(const AtomPinId pin_id) const {
    auto iter = atom_pin_to_classic_tnode_.find(pin_id);
    if(iter == atom_pin_to_classic_tnode_.end()) {
        //Not found
        return OPEN;
    }
    return iter->second;

}


void AtomLookup::set_atom_pin_classic_tnode(const AtomPinId pin_id, const int tnode_index) {
    VTR_ASSERT(pin_id);
    //If either are invalid remove any mapping
    if(!pin_id && tnode_index != OPEN) {
        //Remove
        atom_pin_to_classic_tnode_.erase(tnode_index);
    } else if(pin_id && tnode_index == OPEN) {
        //Remove
        atom_pin_to_classic_tnode_.erase(pin_id);
    } else if(pin_id && tnode_index != OPEN) {
        //Store
        atom_pin_to_classic_tnode_.update(pin_id, tnode_index);
    }
}

/*
 * Timing Nodes
 */
tatum::NodeId AtomLookup::atom_pin_tnode(const AtomPinId pin) const {
    return pin_tnode_[pin];
}

AtomPinId AtomLookup::tnode_atom_pin(const tatum::NodeId tnode) const {
    return pin_tnode_[tnode];
}

AtomLookup::pin_tnode_range AtomLookup::atom_pin_tnodes() const {
    return vtr::make_range(pin_tnode_.begin(), pin_tnode_.end());
}

void AtomLookup::set_atom_pin_tnode(const AtomPinId pin, const tatum::NodeId node) {
    pin_tnode_.update(pin, node);
}
