/**
 * Author: Jason Luu
 * Date: May 2009
 * 
 * Read a circuit netlist in XML format and populate the netlist data structures for VPR
 */

#ifndef READ_NETLIST_H
#define READ_NETLIST_H

#include "vpr_types.h"

void read_netlist(const char *net_file, 
		const t_arch *arch,
        bool verify_file_digests, 
		t_block *block_list[],
		ClusteredNetlist* clustered_nlist);

#endif

