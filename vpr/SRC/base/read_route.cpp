#include <iostream>
#include <fstream>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <sstream>
#include <string>
#include <unordered_set>
using namespace std;

#include "blifparse.hpp"
#include "atom_netlist.h"
#include "atom_netlist_utils.h"
#include "rr_graph.h"
#include "vtr_assert.h"
#include "vtr_util.h"
#include "tatum/echo_writer.hpp"
#include "vtr_list.h"
#include "vtr_log.h"
#include "vtr_logic.h"
#include "vtr_time.h"
#include "vtr_digest.h"
#include "check_route.h"
#include "route_common.h"
#include "vpr_types.h"
#include "globals.h"
#include "vpr_api.h"
#include "read_place.h"
#include "vpr_types.h"
#include "vpr_utils.h"
#include "vpr_error.h"
#include "globals.h"
#include "place_and_route.h"
#include "place.h"
#include "read_place.h"
#include "route_export.h"
#include "rr_graph.h"
#include "path_delay.h"
#include "net_delay.h"
#include "timing_place.h"
#include "read_xml_arch_file.h"
#include "echo_files.h"
#include "route_common.h"
#include "place_macro.h"
#include "RoutingDelayCalculator.h"
#include "timing_info.h"
#include "read_route.h"

static void process_route(ifstream &fp, int inet, string name, std::vector<std::string> input_tokens);
static void format_coordinates(int &x, int &y, string coord);
static void format_pin_info(string &pb_name, string & port_name, int & pb_pin_num, string input);
static string format_name(string name);

void read_route(const char* placement_file, const char* route_file, t_vpr_setup& vpr_setup, const t_arch& Arch) {

    /* Prints out the routing to file route_file.  */
    auto& device_ctx = g_vpr_ctx.mutable_device();
    auto& cluster_ctx = g_vpr_ctx.clustering();

    vpr_init_pre_place_and_route(vpr_setup, Arch);

    /* Parse the file */
    vtr::printf_info("Begin loading packed FPGA routing file.\n");

    string header_str;

    ifstream fp;
    fp.open(route_file);

    if (!fp.is_open()) {
        vpr_throw(VPR_ERROR_ROUTE, route_file, __LINE__,
                "Cannot open %s routing file", route_file);
    }

    getline(fp, header_str);
    std::vector<std::string> header = vtr::split(header_str);
    if (header[0] == "Placement_File:" && header[1] != placement_file) {
        vpr_throw(VPR_ERROR_ROUTE, route_file, __LINE__,
                "Placement files %s specified in the routing file does not match given %s", header[1].c_str(), placement_file);
    }

    read_place(vpr_setup.FileNameOpts.NetFile.c_str(), vpr_setup.FileNameOpts.PlaceFile.c_str(), vpr_setup.FileNameOpts.verify_file_digests, device_ctx.nx, device_ctx.ny, cluster_ctx.num_blocks, cluster_ctx.blocks);
    sync_grid_to_blocks();

    post_place_sync(cluster_ctx.num_blocks);

    t_graph_type graph_type;
    if (vpr_setup.RouterOpts.route_type == GLOBAL) {
        graph_type = GRAPH_GLOBAL;
    } else {
        graph_type = (vpr_setup.RoutingArch.directionality == BI_DIRECTIONAL ?
                GRAPH_BIDIR : GRAPH_UNIDIR);
    }
    free_rr_graph();


    init_chan(vpr_setup.RouterOpts.fixed_channel_width, Arch.Chans);

    /* Set up the routing resource graph defined by this FPGA architecture. */
    int warning_count;
    create_rr_graph(graph_type, device_ctx.num_block_types, device_ctx.block_types, device_ctx.nx, device_ctx.ny, device_ctx.grid,
            &device_ctx.chan_width, vpr_setup.RoutingArch.switch_block_type,
            vpr_setup.RoutingArch.Fs, vpr_setup.RoutingArch.switchblocks,
            vpr_setup.RoutingArch.num_segment,
            device_ctx.num_arch_switches, vpr_setup.Segments,
            vpr_setup.RoutingArch.global_route_switch,
            vpr_setup.RoutingArch.delayless_switch,
            vpr_setup.RoutingArch.wire_to_arch_ipin_switch,
            vpr_setup.RouterOpts.base_cost_type,
            vpr_setup.RouterOpts.trim_empty_channels,
            vpr_setup.RouterOpts.trim_obs_channels,
            Arch.Directs, Arch.num_directs,
            vpr_setup.RoutingArch.dump_rr_structs_file,
            &vpr_setup.RoutingArch.wire_to_rr_ipin_switch,
            &device_ctx.num_rr_switches,
            &warning_count,
            vpr_setup.RouterOpts.write_rr_graph_name.c_str(),
            vpr_setup.RouterOpts.read_rr_graph_name.c_str(), false);

    alloc_and_load_rr_node_route_structs();

    t_clb_opins_used clb_opins_used_locally = alloc_route_structs();
    init_route_structs(vpr_setup.RouterOpts.bb_factor);

    getline(fp, header_str);
    header.clear();
    header = vtr::split(header_str);
    if (header[0] == "Array" && header[1] == "size:" && (atoi(header[2].c_str()) != device_ctx.nx || atoi(header[4].c_str()) != device_ctx.ny)) {
        vpr_throw(VPR_ERROR_ROUTE, route_file, __LINE__,
                "Device dimensions %sx%s specified in the routing file does not match given %dx%d ",
                header[2].c_str(), header[4].c_str(), device_ctx.nx, device_ctx.ny);
    }

    string input;
    unsigned inet;
    std::vector<std::string> tokens;
    while (getline(fp, input)) {
        tokens.clear();
        tokens = vtr::split(input);
        if (tokens.empty()) {
            continue; //Skip blank lines
        } else if (tokens[0][0] == '#') {
            continue; //Skip commented lines
        } else if (tokens[0] == "Net") {
            inet = atoi(tokens[1].c_str());
            process_route(fp, inet, tokens[2], tokens);
        }
    }
    tokens.clear();
    fp.close();

    /*Correctly set up the clb opins*/
    recompute_occupancy_from_scratch(clb_opins_used_locally);

    float pres_fac = vpr_setup.RouterOpts.initial_pres_fac;

    /* Avoid overflow for high iteration counts, even if acc_cost is big */
    pres_fac = min(pres_fac, static_cast<float> (HUGE_POSITIVE_FLOAT / 1e5));

    pathfinder_update_cost(pres_fac, vpr_setup.RouterOpts.acc_fac);

    reserve_locally_used_opins(pres_fac, vpr_setup.RouterOpts.acc_fac, true, clb_opins_used_locally);

    check_route(vpr_setup.RouterOpts.route_type, device_ctx.num_rr_switches, clb_opins_used_locally, vpr_setup.Segments);
    get_serial_num();

    if (getEchoEnabled() && isEchoFileEnabled(E_ECHO_ROUTING_SINK_DELAYS)) {
        print_sink_delays(getEchoFileName(E_ECHO_ROUTING_SINK_DELAYS));
    }

    vtr::printf_info("Finished loading route file\n");

}

static void process_route(ifstream &fp, int inet, string name, std::vector<std::string> input_tokens) {
    auto& cluster_ctx = g_vpr_ctx.mutable_clustering();
    auto& device_ctx = g_vpr_ctx.mutable_device();
    auto& route_ctx = g_vpr_ctx.mutable_routing();
    auto& place_ctx = g_vpr_ctx.placement();

    t_trace *tptr = route_ctx.trace_head[inet];

    string block;
    int inode, x, y, x2, y2, ptc, switch_id, offset;
    bool last_node_sink = false;
    int node_count = 0;
    string input;
    streampos oldpos = fp.tellg();
    std::vector<std::string> tokens;

    if (input_tokens.size() > 3 && input_tokens[3] == "global" && input_tokens[4] == "net" && input_tokens[5] == "connecting:") { /* Global net.  Never routed. */
        cluster_ctx.clbs_nlist.net[inet].is_global = true;
        //erase an extra colon for global nets
        name.erase(name.end() - 1);
        name = format_name(name);

        if (cluster_ctx.clbs_nlist.net[inet].name != name) {
            vpr_throw(VPR_ERROR_ROUTE, __FILE__, __LINE__,
                    "Net name %s for net number %d specified in the routing file does not match given %s",
                    name.c_str(), inet, cluster_ctx.clbs_nlist.net[inet].name);
        }

        int pin_counter = 0;
        int bnum;
        string bnum_str;

        oldpos = fp.tellg();
        while (getline(fp, block)) {
            tokens.clear();
            tokens = vtr::split(block);

            if (tokens.empty()) {
                continue; //Skip blank lines
            } else if (tokens[0][0] == '#') {
                continue; //Skip commented lines
            } else if (tokens[0] != "Block") {
                fp.seekg(oldpos);
                input_tokens.clear();
                return;
            } else {
                format_coordinates(x, y, tokens[4]);

                //remove ()
                bnum_str = format_name(tokens[2]);
                //remove #
                bnum_str.erase(bnum_str.begin());
                bnum = atoi(bnum_str.c_str());

                if (cluster_ctx.blocks[bnum].name != tokens[1]) {
                    vpr_throw(VPR_ERROR_ROUTE, __FILE__, __LINE__,
                            "Block %s for block number %d specified in the routing file does not match given %s",
                            name.c_str(), bnum, cluster_ctx.blocks[bnum].name);
                }

                if (place_ctx.block_locs[bnum].x != x || place_ctx.block_locs[bnum].y != y) {
                    vpr_throw(VPR_ERROR_ROUTE, __FILE__, __LINE__,
                            "The placement coordinates (%d, %d) of %d block does not match given (%d, %d)",
                            x, y, place_ctx.block_locs[bnum].x, place_ctx.block_locs[bnum].y);
                }
                int node_block_pin = cluster_ctx.clbs_nlist.net[inet].pins[pin_counter].block_pin;
                if (cluster_ctx.blocks[bnum].type->pin_class[node_block_pin] != atoi(tokens[7].c_str())) {
                    vpr_throw(VPR_ERROR_ROUTE, __FILE__, __LINE__,
                            "The pin class %d of %d net does not match given ",
                            atoi(tokens[7].c_str()), inet, cluster_ctx.blocks[bnum].type->pin_class[node_block_pin]);
                }
                pin_counter++;
            }
        }
        oldpos = fp.tellg();
    } else {

        cluster_ctx.clbs_nlist.net[inet].is_global = false;
        name = format_name(name);

        if (cluster_ctx.clbs_nlist.net[inet].name != name) {
            vpr_throw(VPR_ERROR_ROUTE, __FILE__, __LINE__,
                    "Net name %s for net number %d specified in the routing file does not match given %s",
                    name.c_str(), inet, cluster_ctx.clbs_nlist.net[inet].name);
        }
        oldpos = fp.tellg();
        while (getline(fp, input)) {
            tokens.clear();
            tokens = vtr::split(input);

            if (tokens.empty()) {
                continue; //Skip blank lines
            } else if (tokens[0][0] == '#') {
                continue; //Skip commented lines
            } else if (tokens[0] == "Net") {
                fp.seekg(oldpos);
                if (!last_node_sink) {
                    vpr_throw(VPR_ERROR_ROUTE, __FILE__, __LINE__,
                            "Last node in routing has to be a sink type");
                }
                input_tokens.clear();
                return;
            } else if (input == "\n\nUsed in local cluster only, reserved one CLB pin\n\n") {
                if (cluster_ctx.clbs_nlist.net[inet].num_sinks() != false) {
                    vpr_throw(VPR_ERROR_ROUTE, __FILE__, __LINE__,
                            "Net %d should be used in local cluster only, reserved one CLB pin");
                }
                input_tokens.clear();
                return;
            } else if (tokens[0] == "Node:") {
                //An actual line, go through each node and add it to the route tree
                inode = atoi(tokens[1].c_str());
                auto& node = device_ctx.rr_nodes[inode];

                //First node needs to be source. It is isolated to correctly set heap head.
                if (node_count == 0 && tokens[2] != "SOURCE") {
                    vpr_throw(VPR_ERROR_ROUTE, __FILE__, __LINE__,
                            "First node in routing has to be a source type");
                }

                //Check node types if match rr graph
                if (tokens[2] != node.type_string()) {
                    vpr_throw(VPR_ERROR_ROUTE, __FILE__, __LINE__,
                            "Node %d has a type that does not match the RR graph", inode);
                }

                if (tokens[2] == "SINK") {
                    last_node_sink = true;
                } else {
                    last_node_sink = false;
                }

                format_coordinates(x, y, tokens[3]);

                if (tokens[4] == "to") {
                    format_coordinates(x2, y2, tokens[5]);
                    if (node.xlow() != x || node.xhigh() != x2 || node.yhigh() != y2 || node.ylow() != y) {
                        vpr_throw(VPR_ERROR_ROUTE, __FILE__, __LINE__,
                                "The coordinates of node %d does not match the rr graph", inode);
                    }
                    offset = 2;
                } else {
                    if (node.xlow() != x || node.xhigh() != x || node.yhigh() != y || node.ylow() != y) {
                        vpr_throw(VPR_ERROR_ROUTE, __FILE__, __LINE__,
                                "The coordinates of node %d does not match the rr graph", inode);
                    }
                    offset = 0;
                }


                if (tokens[2] == "SOURCE" || tokens[2] == "SINK" || tokens[2] == "OPIN" || tokens[2] == "IPIN") {
                    if (tokens[4 + offset] == "Pad:" && device_ctx.grid[x][y].type != device_ctx.IO_TYPE) {
                        vpr_throw(VPR_ERROR_ROUTE, __FILE__, __LINE__,
                                "Node %d is of the wrong type", inode);
                    }
                } else if (tokens[2] == "CHANX" || tokens[2] == "CHANY") {
                    if (tokens[4 + offset] != "Track:") {
                        vpr_throw(VPR_ERROR_ROUTE, __FILE__, __LINE__,
                                "A %s node have to have track info", tokens[2].c_str());
                    }
                }
                
                ptc = atoi(tokens[5 + offset].c_str());
                if (node.ptc_num() != ptc) {
                    vpr_throw(VPR_ERROR_ROUTE, __FILE__, __LINE__,
                            "The ptc num of node %d does not match the rr graph", inode);
                }

                if (tokens[6 + offset] != "Switch:") {
                    //This is an opin or ipin, process its pin nums
                    if (device_ctx.grid[x][y].type != device_ctx.IO_TYPE && (tokens[2] == "IPIN" || tokens[2] == "OPIN")) {
                        int pin_num = device_ctx.rr_nodes[inode].ptc_num();
                        int height_offset = device_ctx.grid[x][y].height_offset;
                        int iblock = place_ctx.grid_blocks[x][y - height_offset].blocks[0];
                        VTR_ASSERT(iblock != OPEN);
                        t_pb_graph_pin *pb_pin = get_pb_graph_node_pin_from_block_pin(iblock, pin_num);
                        t_pb_type *pb_type = pb_pin->parent_node->pb_type;

                        string pb_name, port_name;
                        int pb_pin_num;

                        format_pin_info(pb_name, port_name, pb_pin_num, tokens[6 + offset]);

                        if (pb_name != pb_type->name || port_name != pb_pin->port->name || pb_pin_num != pb_pin->pin_number) {
                            vpr_throw(VPR_ERROR_ROUTE, __FILE__, __LINE__,
                                    "%d node does not have correct pins", inode);
                        }
                    } else {
                        vpr_throw(VPR_ERROR_ROUTE, __FILE__, __LINE__,
                                "%d node does not have correct pins", inode);
                    }
                    switch_id = atoi(tokens[8 + offset].c_str());
                } else {
                    switch_id = atoi(tokens[7 + offset].c_str());
                }

                if (node_count == 0) {
                    route_ctx.trace_head[inet] = alloc_trace_data();
                    route_ctx.trace_head[inet]->index = inode;
                    route_ctx.trace_head[inet]->iswitch = switch_id;
                    route_ctx.trace_head[inet]->next = NULL;
                    tptr = route_ctx.trace_head[inet];
                    node_count++;
                } else {
                    tptr->next = alloc_trace_data();
                    tptr = tptr -> next;
                    tptr->index = inode;
                    tptr->iswitch = switch_id;
                    tptr->next = NULL;
                    node_count++;
                }
            }
            //stores last line so can easily go back to read
            oldpos = fp.tellg();
        }
    }
    if (!last_node_sink) {
        vpr_throw(VPR_ERROR_ROUTE, __FILE__, __LINE__,
                "Last node in routing has to be a sink type");
    }
    tokens.clear();
    input_tokens.clear();
    return;
}

static void format_coordinates(int &x, int &y, string coord) {
    coord = format_name(coord);
    stringstream coord_stream(coord);
    coord_stream >> x;
    coord_stream.ignore(1, ' ');
    coord_stream >> y;
}

static void format_pin_info(string &pb_name, string & port_name, int & pb_pin_num, string input) {
    stringstream pb_info(input);
    getline(pb_info, pb_name, '.');
    getline(pb_info, port_name, '[');
    pb_info >> pb_pin_num;
}

static string format_name(string name) {
    name.erase(name.begin());
    name.erase(name.end() - 1);
    return name;
}