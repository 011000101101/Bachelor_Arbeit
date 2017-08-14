#ifndef CLUSTERED_NETLIST_H
#define CLUSTERED_NETLIST_H
/*
* Summary
* ========
* This file defines the ClusteredNetlist class in the ClusteredContext created during 
* pre-placement stages of the VTR flow (packing & clustering), and used downstream.
*
* Overview
* ========
* The ClusteredNetlist is derived from the Netlist class, and contains some 
* separate information on Blocks, Pins, and Nets. It does not make use of Ports.
*
* Block
* -----
* The pieces of unique block information are:
*		block_pbs_:			Physical block describing the clustering and internal hierarchy
*							structure of each CLB
*		block_types_:		The type of physical block the block is mapped to, e.g. logic
*							block, RAM, DSP (Can be user-defined types)
*		block_nets_:		Based on the block's pins (indexed from [0...num_pins - 1]),
*							lists which pins are used/unused with the net using it
*		block_net_count_:	Counter for the used pins/nets blocks, indicating the "index"
*							Differs from block_nets_
*	
* Differences between block_nets_ & block_net_count_
* --------------------------------------------------
*			+-----------+
*		0-->|			|-->3
*		1-->|   Block	|-->4
*		2-->|			|-->5
*			+-----------+
*
* block_nets_ tracks all pins, and has the ClusterNetId to which a pin is connected to.
* If the pin is unused/open, ClusterNetId::INVALID() is stored.
*
* block_net_count_ tracks whether the nets connected to the block are drivers/receivers of that net.
* Driver/receiver nets are determined by the pin_class of the block's pin.
* A net connected to a driver pin in the block has a 0 is stored. A net connected to a receiver
* has a counter (from [1...num_sinks - 1]).
*
* The net is connected to multiple blocks. Each block_net_count_ has a unique number in that net.
*
* E.g.
*			+-----------+					+-----------+
*		0-->|			|-->3  Net A	0-->|			|-->3
*		1-->|  Block 1	|---4---------->1-->|  Block 2	|-->4
*		2-->|			|-->5			2-->|			|-->5
*			+-----------+				|	+-----------+
*										|
*										|	+-----------+
*										|	|			|-->1
*										0-->|  Block 3	|
*											|			|-->2
*											+-----------+
*
* In the example, Net A is driven by Block 1, and received by Blocks 2 & 3.
* For Block 1, block_net_count_ of pin 4 would then return 0, as it is the driver.
* For Block 2, block_net_count_ of pin 1 returns 1 (or 2), non-zero as it is a receiver.
* For Block 3, block_net_count_ of pin 0 returns 2 (or 1), non-zero as it is also a receiver.
*
*
* Pin
* ---
* The only piece of unique pin information is:
*		pin_index_
*
* Example of pin_index_
* ---------------------
* Given a ClusterPinId, pin_index_ will return the index of the pin within it's block. 
* 
*			+-----------+
*		0-->|X		   X|-->3
*		1-->|O  Block  O|-->4
*		2-->|O		   O|-->5 (e.g. ClusterPinId = 92)
*			+-----------+
* 
* The index skips over unused pins, e.g. CLB has 6 pins (3 in, 3 out, numbered [0...5]), where
* the first two ins, and last two outs are used. Indices [0,1] represent the ins, and [4,5] 
* represent the outs. Indices [2,3] are unused. Therefore, pin_index_[92] = 5.
*
* Nets
* ----
* The pieces of unique net information stored are:
*		net_global_:	Boolean mapping whether the net is global
*		net_fixed_:		Boolean mapping whether the net is fixed (i.e. constant)
*
*  
*
*/
#include "vpr_types.h"
#include "vpr_utils.h"

#include "vtr_util.h"

#include "netlist.h"
#include "clustered_netlist_fwd.h"


class ClusteredNetlist : public Netlist<ClusterBlockId, ClusterPortId, ClusterPinId, ClusterNetId> {
	public:
		//Constructs a netlist
		// name: the name of the netlist (e.g. top-level module)
		// id:   a unique identifier for the netlist (e.g. a secure digest of the input file)
		ClusteredNetlist(std::string name="", std::string id="");

	public: //Public Mutators
		//Create or return an existing block in the netlist
		//  name        : The unique name of the block
		//  pb			: The physical representation of the block
		//  t_type_ptr	: The type of the CLB
		ClusterBlockId create_block(const char *name, t_pb* pb, t_type_ptr type);

		//Sets the block's pin to point to net
		//  blk_id		: The block the pin is associated with
		//  pin_index   : The pin of the block to be changed
		//  net_id		: The changed net
		void set_block_net(const ClusterBlockId blk_id, const int _pin_index, const ClusterNetId net_id);

		//Sets the block's net count
		//  blk_id		: The block the net is associated with
		//	pin_index	: The pin of the block to be changed
		//  net_count	: The net's counter
		void set_block_net_count(const ClusterBlockId blk_id, const int _pin_index, const int count);

		//Create or return an existing port in the netlist
		//  blk_id      : The block the port is associated with
		//  name        : The name of the port (must match the name of a port in the block's model)
		//  width		: The width (number of bits) of the port
		//  type		: The type of the port (INPUT, OUTPUT, or CLOCK)
		ClusterPortId create_port(const ClusterBlockId blk_id, const std::string name, BitIndex width, PortType port_type);

		//Create or return an existing pin in the netlist
		//  port_id    : The port this pin is associated with
		//  port_bit   : The bit index of the pin in the port
		//  net_id     : The net the pin drives/sinks
		//  pin_type   : The type of the pin (driver/sink)
		//  port_type  : The type of the port (input/output/clock)
		//  pin_index  : The index of the pin relative to its block, excluding OPEN pins)
		//  is_const   : Indicates whether the pin holds a constant value (e. g. vcc/gnd)
		ClusterPinId   create_pin(const ClusterPortId port_id, BitIndex port_bit, const ClusterNetId net_id, const PinType pin_type, const PortType port_type, int _pin_index, bool is_const=false);

		//Sets the pin's index in a block
		//  pin_id   : The pin to be set
		//  index    : The new index to set the pin to
		void	set_pin_index(const ClusterPinId pin_id, const int index);

		//Create an empty, or return an existing net in the netlist
		//  name     : The unique name of the net
		ClusterNetId	create_net(const std::string name);

		//Sets the netlist id based on a file digest's string
		void set_netlist_id(std::string id);
		
		//Sets the flag in net_global_ = state
		void set_global(ClusterNetId net_id, bool state);

		//Sets the flag in net_routed_ = state
		void set_routed(ClusterNetId net_id, bool state);

		//Sets the flag in net_fixed_ = state
		void set_fixed(ClusterNetId net_id, bool state);

		//Removes a block from the netlist. This will also remove the associated ports and pins.
		//  blk_id  : The block to be removed
		void remove_block(const ClusterBlockId blk_id);

		//TODO: May want to add remove_net/remove_pin, although this isn't currently used

		//Compresses the netlist, removing any invalid and/or unreferenced
		//blocks/ports/pins/nets.
		//
		//NOTE: this invalidates all existing IDs!
		//		This also does NOT call the Netlist implementation of compress
		void compress();


	public: //Public Accessors
		/*
		* Blocks
		*/
		//Returns the physical block
		t_pb* block_pb(const ClusterBlockId id) const;

		//Returns the type of CLB (Logic block, RAM, DSP, etc.)
		t_type_ptr block_type(const ClusterBlockId id) const;

		//Returns the net of the block attached to the specific pin index
		ClusterNetId block_net(const ClusterBlockId blk_id, const int _pin_index) const;

		//Returns the count on the net of the block attached
		int block_net_count(const ClusterBlockId blk_id, const int _pin_index) const;

		/*
		* Pins
		*/
		//Returns the index of the block which the pin belongs to
		int pin_index(const ClusterPinId id) const;

		//Finds the count'th net_pins (e.g. the 6th pin of the net) and
		//returns the index of the block which that pin belongs to
		//  net_id     : The net to iterate through
		//  count      : The index of the pin in the net
		int pin_index(ClusterNetId net_id, int count) const;

		/*
		* Nets
		*/
		//Returns the block of the net & pin which it's attached to
		ClusterBlockId net_pin_block(const ClusterNetId net_id, int _pin_index) const;

		//Returns the pin's index in the net
		//  net_id		: The net of which the pin belongs to
		//  pin_id		: The matching pin the net connects to
		int net_pin_index(ClusterNetId net_id, ClusterPinId pin_id) const;

		//Returns whether the net is global or fixed
		bool net_global(const ClusterNetId id) const;
		bool net_routed(const ClusterNetId id) const;
		bool net_fixed(const ClusterNetId id) const;

	private: //Private Members
		/*
         * Netlist compression/optimization
         */
        //Removes invalid and reorders blocks
        void clean_blocks(const vtr::vector_map<ClusterBlockId,ClusterBlockId>& block_id_map);

		//Removes invalid and reorders pins
		void clean_pins(const vtr::vector_map<ClusterPinId, ClusterPinId>& pin_id_map);

		//Removes invalid and reorders nets
		void clean_nets(const vtr::vector_map<ClusterNetId, ClusterNetId>& net_id_map);

        //Shrinks internal data structures to required size to reduce memory consumption
        void shrink_to_fit();

		/*
		* Sanity Checks
		*/
		//Verify the internal data structure sizes match
		bool validate_block_sizes() const;
		bool validate_pin_sizes() const;
		bool validate_net_sizes() const;

	private: //Private Data
		
		//Blocks
		vtr::vector_map<ClusterBlockId, t_pb*>							block_pbs_;         //Physical block representing the clustering & internal hierarchy of each CLB
		vtr::vector_map<ClusterBlockId, t_type_ptr>						block_types_;		//The type of physical block this user circuit block is mapped to
		vtr::vector_map<ClusterBlockId, std::vector<ClusterNetId>>		block_nets_;		//Stores which pins are used/unused on the block with the net using it
		vtr::vector_map<ClusterBlockId, std::vector<int>>				block_net_count_;	//The count of the nets related to the block

		//Pins
		vtr::vector_map<ClusterPinId, int>								pin_index_;			//The index of the pins relative to its block
			
		//Nets	
		vtr::vector_map<ClusterNetId, bool>							net_global_;		//Boolean mapping indicating if the net is
		vtr::vector_map<ClusterNetId, bool>							net_routed_;		//Global, routed, or fixed (mutually exclusive).
		vtr::vector_map<ClusterNetId, bool>							net_fixed_;			//TODO: transfer net routing state to RoutingContext
};

#endif
