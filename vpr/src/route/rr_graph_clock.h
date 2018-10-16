#ifndef RR_GRAPH_CLOCK_H
#define RR_GRAPH_CLOCK_H

#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <set>
#include <utility>

#include "clock_fwd.h"

#include "clock_network_types.h"
#include "clock_connection_types.h"

class ClockNetwork;
class ClockConnection;

class SwitchPoint {
    public:
        // [grid_width][grid_height][nodes]
        std::vector<std::vector<std::vector<int>>> rr_node_indices;
        std::set<std::pair<int, int>> locations; // x,y
    public:
        /** Getters **/
        std::vector<int> get_rr_node_indices_at_location(int x, int y) const;

        std::set<std::pair<int, int>> get_switch_locations() const;

        /** Setters **/
        void insert_node_idx(int x, int y, int node_idx);
};

class SwitchPoints {
    public:
        std::unordered_map<std::string, SwitchPoint> switch_name_to_switch_location;
    public:
        /** Getters **/
        std::vector<int> get_rr_node_indices_at_location(
            std::string switch_name,
            int x,
            int y) const;

        std::set<std::pair<int, int>> get_switch_locations(std::string switch_name) const;

        /** Setters **/
        void insert_switch_node_idx(std::string switch_name, int x, int y, int node_idx);
};

class ClockRRGraph {

    public:
        /* Returns the current ptc num where the wire should be drawn and updates the
           channel width. */
        int get_and_increment_chanx_ptc_num();
        int get_and_increment_chany_ptc_num();


    public:
        /* Reverse lookup for to find the clock source and tap locations for each clock_network
           The map key is the the clock network name and value are all the switch points*/
        std::unordered_map<std::string, SwitchPoints> clock_name_to_switch_points;

        void add_switch_location(
            std::string clock_name,
            std::string switch_name,
            int x,
            int y,
            int node_index);

        std::vector<int> get_rr_node_indices_at_switch_location(
            std::string clock_name,
            std::string switch_name,
            int x,
            int y) const;

        std::set<std::pair<int, int>> get_switch_locations(
            std::string clock_name,
            std::string switch_name) const;

    public:
        /* Creates the routing resourse (rr) graph of the clock network and appends it to the
           existing rr graph created in build_rr_graph for inter-block and intra-block routing. */
        static void create_and_append_clock_rr_graph(
                const float R_minW_nmos,
                const float R_minW_pmos);

    private:
        /* Dummy clock network that connects every I/O input to every clock pin. */
        static void create_star_model_network();

        /* loop over all of the clock networks and create their wires */
        void create_clock_networks_wires(std::vector<std::unique_ptr<ClockNetwork>>& clock_networks);

        /* loop over all clock routing connections and create the switches and connections */
        void create_clock_networks_switches(
            std::vector<std::unique_ptr<ClockConnection>>& clock_connections);

        /* Adds the architecture switches that the clock rr_nodes use to the rr switches and
           maps the newly added rr_switches to the nodes */
        // TODO: Change to account for swtich fanin. Note: this function is simular to
        //       remap_rr_node_switch_indices but does not take into account node fanin.
        void add_rr_switches_and_map_to_nodes(
                size_t nodes_start_idx,
                const float R_minW_nmos,
                const float R_minW_pmos);

        /* Returns the index of the newly added rr_switch. The rr_switch information is coppied
           in from the arch_switch information */
        // TODO: Does not account for fanin information when copping Tdel. Note: this function
        //       is simular to load_rr_switch_inf but does not take into account node fanin.
        int add_rr_switch_from_arch_switch_inf(
                int arch_switch_idx,
                const float R_minW_nmos,
                const float R_minW_pmos);
};

#endif

