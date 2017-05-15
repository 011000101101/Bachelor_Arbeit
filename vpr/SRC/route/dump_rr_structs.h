#ifndef DUMP_RR_STRUCTS_H
#define DUMP_RR_STRUCTS_H



/**** Functions ****/
/* The main function for dumping rr structs to a specified file. The structures dumped are:
	- rr nodes (device_ctx.rr_nodes)
	- rr switches (device_ctx.rr_switch_inf)
	- the grid (grid)
	- physical block types (device_ctx.block_types)
	- node index lookups (rr_node_indices) */
void dump_rr_structs( const char *filename );


#endif /* DUMP_RR_STRUCTS_H */
