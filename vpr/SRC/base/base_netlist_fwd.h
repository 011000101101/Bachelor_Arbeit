#ifndef BASE_NETLIST_FWD_H
#define BASE_NETLIST_FWD_H
#include "vtr_strong_id.h"
/*
* This header forward declares the BaseNetlist class, and defines common types by it
*/

/*
* Ids
*
* The BaseNetlist uses unique IDs to identify any component of the netlist.
* To avoid type-conversion errors (e.g. passing a PinId where a NetId
* was expected), we use vtr::StrongId's to disallow such conversions. See
* vtr_strong_id.h for details.
*/

//Type tags for Ids
struct block_id_tag;
struct net_id_tag;
struct port_id_tag;
struct pin_id_tag;

//A unique identifier for a block in the base netlist
typedef vtr::StrongId<block_id_tag> BlockId;

//A unique identifier for a net in the base netlist
typedef vtr::StrongId<net_id_tag> NetId;

//A unique identifier for a port in the base netlist
typedef vtr::StrongId<port_id_tag> PortId;

//A unique identifier for a pin in the base netlist
typedef vtr::StrongId<pin_id_tag> PinId;

//A signal index in a port
typedef unsigned BitIndex;

//The type of a port in the BaseNetlist
enum class PortType : char {
	INPUT,  //The port is a data-input
	OUTPUT, //The port is an output (usually data, but potentially a clock)
	CLOCK   //The port is an input clock
};

enum class PinType : char {
	DRIVER,	//The pin drives a net
	SINK,	//The pin is a net sink
	OPEN	//The pin is an open connection (undecided)
};

#endif