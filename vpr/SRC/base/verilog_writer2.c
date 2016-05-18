#include <fstream>
#include <iostream>
#include <sstream>
#include <set>
#include <map>
#include <vector>
#include <string>
#include <algorithm>
#include <cassert>
#include <bitset>

#include "verilog_writer2.h"

#include "globals.h"
#include "path_delay.h"

class PrintingVisitor : public NetlistVisitor {
    private:
        void visit_top_impl(const char* top_level_name) { 
            printf("Top: %s\n", top_level_name); 
        }

        void visit_clb_impl(const t_pb* clb) { 
            const t_pb_type* pb_type = clb->pb_graph_node->pb_type;
            printf("CLB: %s (%s)\n", clb->name, pb_type->name); 
        }

        void visit_atom_impl(const t_pb* atom) { 
            const t_pb_type* pb_type = atom->pb_graph_node->pb_type;
            const t_model* model = logical_block[atom->logical_block].model;
            printf("ATOM: %s (%s: %s)\n", atom->name, pb_type->name, model->name); 
        }
};



class VerilogSdfWriterVisitor : public NetlistVisitor {
    public:
        VerilogSdfWriterVisitor(std::ostream& verilog_os, std::ostream& blif_os, std::ostream& sdf_os)
            : verilog_os_(verilog_os)
            , blif_os_(blif_os)
            , sdf_os_(sdf_os) {
            
            pin_id_to_tnode_lookup_ = alloc_and_load_tnode_lookup_from_pin_id();
        }

        //Non copyable/assignable/moveable
        VerilogSdfWriterVisitor(VerilogSdfWriterVisitor& other) = delete;
        VerilogSdfWriterVisitor(VerilogSdfWriterVisitor&& other) = delete;
        VerilogSdfWriterVisitor& operator=(VerilogSdfWriterVisitor& rhs) = delete;

        ~VerilogSdfWriterVisitor() {
            free(pin_id_to_tnode_lookup_);
        }

    private: //Internal types
        enum class LogicVal {
            FALSE=0,
            TRUE=1,
            DONTCARE,
            UNKOWN,
            HIGHZ
        };

        friend std::ostream& operator<<(std::ostream& os, LogicVal val) {
            if(val == LogicVal::FALSE) os << "0";
            else if (val == LogicVal::TRUE) os << "1";
            else if (val == LogicVal::DONTCARE) os << "-";
            else if (val == LogicVal::UNKOWN) os << "x";
            else if (val == LogicVal::HIGHZ) os << "z";
            else assert(false);
            return os;
        }

        class LogicVec {
            public:
                LogicVec() = default;
                LogicVec(size_t size_val, LogicVal init_value)
                    : values_(size_val, init_value)
                    {}

                LogicVal& operator[](size_t i) { return values_[i]; }
                size_t size() { return values_.size(); }

                friend std::ostream& operator<<(std::ostream& os, LogicVec logic_vec) {
                    os << logic_vec.values_.size() << "'b";
                    //Print in reverse since th convention is MSB on the left, LSB on the right
                    for(auto iter = logic_vec.begin(); iter != logic_vec.end(); iter++) {
                        os << *iter;
                    }
                    return os;
                }

                void rotate(std::vector<int> permute) {
                    assert(permute.size() == values_.size());

                    auto orig_values = values_;

                    for(size_t i = 0; i < values_.size(); i++) {
                        /*std::cout << "\tMove " << permute[i] << " -> " << i << ": " << *this;*/

                        values_[i] = orig_values[permute[i]];

                        /*std::cout << " -> " << *this << std::endl;*/
                    }
                }

                std::vector<size_t> minterms() {
                    std::vector<size_t> minterms_vec;

                    minterms_recurr(minterms_vec, *this);

                    return minterms_vec;
                }

                std::vector<LogicVal>::reverse_iterator begin() { return values_.rbegin(); }
                std::vector<LogicVal>::reverse_iterator end() { return values_.rend(); }
                std::vector<LogicVal>::const_reverse_iterator begin() const { return values_.crbegin(); }
                std::vector<LogicVal>::const_reverse_iterator end() const { return values_.crend(); }

            private:

                void minterms_recurr(std::vector<size_t>& minterms_vec, LogicVec logic_vec) {

                    auto iter = std::find(logic_vec.begin(), logic_vec.end(), LogicVal::DONTCARE);
                    if(iter == logic_vec.end()) {
                        //Base case (only TRUE/FALSE) caluclate minterm number
                        size_t minterm_number = 0;
                        for(size_t i = 0; i < values_.size(); i++) {
                            if(logic_vec.values_[i] == LogicVal::TRUE) {
                                size_t index_power = (1 << i);
                                minterm_number += index_power;
                            } else if(logic_vec.values_[i] == LogicVal::FALSE) {
                                //pass
                            } else {
                                assert(false); //Unsupported values
                            }
                        }
                        minterms_vec.push_back(minterm_number);
                    } else {
                        //Recurse
                        *iter = LogicVal::TRUE;
                        minterms_recurr(minterms_vec, logic_vec);

                        *iter = LogicVal::FALSE;
                        minterms_recurr(minterms_vec, logic_vec);
                    }
                }

                std::vector<LogicVal> values_;
        };

        enum class PortDir {
            IN,
            OUT
        };

        class Arc {
            public:
                Arc(std::string src, std::string snk, float del)
                    : source_name_(src)
                    , sink_name_(snk)
                    , delay_(del)
                    {}

                std::string source_name() { return source_name_; }
                std::string sink_name() { return sink_name_; }
                float delay() { return delay_; }

            private:
                std::string source_name_;
                std::string sink_name_;
                float delay_;
        };

        class LutInstance {
            public:
                LutInstance(std::string type_name, LogicVec lut_mask, std::string inst_name, 
                            std::map<std::string,std::string> port_conns, std::map<int,Arc> timing_arc_values)
                    : type_(type_name)
                    , lut_mask_(lut_mask)
                    , inst_name_(inst_name)
                    , port_connections_(port_conns)
                    , timing_arcs_(timing_arc_values)
                    {}

                const std::map<int,Arc>& timing_arcs() { return timing_arcs_; }

                void print_verilog(std::ostream& os, std::string indent) {
                    //Instantiate the lut
                    os << indent << type_;

                    std::stringstream param_ss;
                    param_ss << lut_mask_;
                    os << " #(" << param_ss.str() << ") ";

                    os << inst_name_ << "(";

                    //and all its named port connections
                    for(auto iter = port_connections_.begin(); iter != port_connections_.end(); ++iter) {
                        os << "." + iter->first;
                        os << "(";
                        if(iter->second == "") {
                            //Disconnected

                            if(iter != --port_connections_.end()) {
                                //Disconnected inputs are grounded
                                os << "1'b0";
                            } else {
                                //Disconnected outputs are left open
                                os << "";
                            }
                            
                        } else {
                            os << iter->second;
                        }
                        os << ")";

                        if(iter != --port_connections_.end()) {
                            os << ", ";
                        }
                    }
                    os << ");\n\n";
                }

                void print_blif(std::ostream& os , std::string indent) {
                    os << indent << ".names ";

                    //We currently rely upon the ports begin sorted by thier name (e.g. in_0, in_1)
                    for(auto kv : port_connections_) {
                        if(kv.second == "") {
                            //Disconnected
                            os << "unconn" << " ";
                        } else {
                            os << kv.second << " ";
                        }
                    }
                    os << "\n";

                    size_t minterms_set = 0;
                    for(size_t minterm = 0; minterm < lut_mask_.size(); minterm++) {
                        if(lut_mask_[minterm] == LogicVal::TRUE) {
                            //Convert the minterm to a string of bits
                            std::string bit_str = std::bitset<64>(minterm).to_string();

                            //Because BLIF puts the input values in order from LSB (left) to MSB (right), we need
                            //to reverse the string
                            std::reverse(bit_str.begin(), bit_str.end());

                            //Trim to the LUT size
                            std::string input_values(bit_str.begin(), bit_str.begin() + (port_connections_.size() - 1));

                            //Add the row as true
                            os << input_values << " 1\n";

                            minterms_set++;
                        }
                    }
                    if(minterms_set == 0) {
                        //To make ABC happy (i.e. avoid complaints about mismatching cover size and fanin)
                        //put in a false value for luts that are always false
                        os << std::string(port_connections_.size() - 1, '-') << " 0\n";
                    }
                }

                std::string instance_name() { return inst_name_; }
                std::string type() { return type_; }
            private:
                std::string type_;
                LogicVec lut_mask_;
                std::string inst_name_;
                std::map<std::string,std::string> port_connections_;
                std::map<int,Arc> timing_arcs_; //Pin index to timing arc
        };

        class Assignment {
            public:
                Assignment(std::string lval, std::string rval)
                    : lval_(lval)
                    , rval_(rval)
                    {}

                void print_verilog(std::ostream& os, std::string indent) {
                    os << indent << "assign " << lval_ << " = " << rval_ << ";\n";
                }
                void print_blif(std::ostream& os, std::string indent) {
                    os << indent << ".names " << rval_ << " " << lval_ << "\n";
                    os << indent << "1 1\n";
                }
            private:
                std::string lval_;
                std::string rval_;
        };
    private: //NetlistVisitor interface functions

        void visit_top_impl(const char* top_level_name) { 
            top_module_name_ = top_level_name;
        }

        void visit_atom_impl(const t_pb* atom) { 
            const t_model* model = logical_block[atom->logical_block].model;

            if(model->name == std::string("input")) {
                inputs_.emplace_back(make_io(atom, PortDir::IN));
            } else if(model->name == std::string("output")) {
                outputs_.emplace_back(make_io(atom, PortDir::OUT));
            } else if(model->name == std::string("names")) {
                cell_instances_.push_back(make_lut_instance(atom));
            }
        }

        void finish_impl() {
            
            print_verilog();
            print_blif();
            print_sdf();
        }

    private: //Helper functions

        void print_verilog(int depth=0) {
            verilog_os_ << indent(depth) << "//Verilog generated by VPR from post-place-and-route implementation\n";
            verilog_os_ << indent(depth) << "module " << top_module_name_ << " (\n";
            for(auto iter = inputs_.begin(); iter != inputs_.end(); ++iter) {
                verilog_os_ << indent(depth+1) << "input " << *iter;
                if(iter + 1 != inputs_.end() || outputs_.size() > 0) {
                   verilog_os_ << ",";
                }
                verilog_os_ << "\n";
            }

            for(auto iter = outputs_.begin(); iter != outputs_.end(); ++iter) {
                verilog_os_ << indent(depth+1) << "output " << *iter;
                if(iter + 1 != outputs_.end()) {
                   verilog_os_ << ",";
                }
                verilog_os_ << "\n";
            }
            verilog_os_ << indent(depth) << ");\n" ;

            verilog_os_ << "\n";
            verilog_os_ << indent(depth+1) << "//Wires\n";
            for(auto& kv : logical_net_drivers_) {
                verilog_os_ << indent(depth+1) << "wire " << kv.second.first << ";\n";
            }
            for(auto& kv : logical_net_sinks_) {
                for(auto& wire_tnode_pair : kv.second) {
                    verilog_os_ << indent(depth+1) << "wire " << wire_tnode_pair.first << ";\n";
                }
            }

            verilog_os_ << "\n";
            verilog_os_ << indent(depth+1) << "//IO assignments\n";
            for(auto& assign : assignments_) {
                assign.print_verilog(verilog_os_, indent(depth+1));
            }

            verilog_os_ << "\n";
            verilog_os_ << indent(depth+1) << "//Interconnect\n";
            for(const auto& kv : logical_net_sinks_) {
                int atom_net_idx = kv.first;
                auto driver_iter = logical_net_drivers_.find(atom_net_idx);
                assert(driver_iter != logical_net_drivers_.end());
                const auto& driver_wire = driver_iter->second.first;

                for(auto& sink_wire_tnode_pair : kv.second) {
                    std::string inst_name = interconnect_name(driver_wire, sink_wire_tnode_pair.first);
                    verilog_os_ << indent(depth+1) << "fpga_interconnect " << inst_name;
                    verilog_os_ << "(" << driver_wire << ", " << sink_wire_tnode_pair.first << ");\n\n";
                }
            }

            verilog_os_ << "\n";
            verilog_os_ << indent(depth+1) << "//Cell instances\n";
            for(auto& inst : cell_instances_) {
                inst.print_verilog(verilog_os_, indent(depth+1));
            }

            verilog_os_ << "\n";
            verilog_os_ << indent(depth) << "endmodule\n";
        }

        void print_blif(int depth=0) {
            blif_os_ << indent(depth) << "#BLIF generated by VPR from post-place-and-route implementation\n";
            blif_os_ << indent(depth) << ".model " << top_module_name_ << "\n";
            blif_os_ << indent(depth) << ".inputs ";
            for(auto iter = inputs_.begin(); iter != inputs_.end(); ++iter) {
                blif_os_ << *iter << " ";
            }
            blif_os_ << "\n";

            blif_os_ << indent(depth) << ".outputs ";
            for(auto iter = outputs_.begin(); iter != outputs_.end(); ++iter) {
                blif_os_ << *iter << " ";
            }
            blif_os_ << "\n";

            blif_os_ << "\n";
            blif_os_ << indent(depth) << "#IO assignments\n";
            for(auto& assign : assignments_) {
                assign.print_blif(blif_os_, indent(depth));
            }

            blif_os_ << "\n";
            blif_os_ << indent(depth) << "#Interconnect\n";
            for(const auto& kv : logical_net_sinks_) {
                int atom_net_idx = kv.first;
                auto driver_iter = logical_net_drivers_.find(atom_net_idx);
                assert(driver_iter != logical_net_drivers_.end());
                const auto& driver_wire = driver_iter->second.first;

                for(auto& sink_wire_tnode_pair : kv.second) {
                    blif_os_ << indent(depth) << ".names " << driver_wire << " " << sink_wire_tnode_pair.first << "\n";
                    blif_os_ << indent(depth) << "1 1\n";
                }
            }

            blif_os_ << "\n";
            blif_os_ << indent(depth) << "#Cell instances\n";
            for(auto& inst : cell_instances_) {
                inst.print_blif(blif_os_, indent(depth));
            }

            blif_os_ << "\n";
            blif_os_ << indent(depth) << ".end\n";
        }

        void print_sdf(int depth=0) {
            sdf_os_ << indent(depth) << "(DELAYFILE\n";
            sdf_os_ << indent(depth+1) << "(SDFVERSION \"2.1\")\n";
            sdf_os_ << indent(depth+1) << "(DESIGN \""<< blif_circuit_name << "\")\n";
            sdf_os_ << indent(depth+1) << "(VENDOR \"verilog-to-routing\")\n";
            sdf_os_ << indent(depth+1) << "(PROGRAM \"vpr\")\n";
            sdf_os_ << indent(depth+1) << "(VERSION \"" << BUILD_VERSION << "\")\n";
            sdf_os_ << indent(depth+1) << "(DIVIDER /)\n";
            sdf_os_ << indent(depth+1) << "(TIMESCALE 1 ps)\n";
            sdf_os_ << "\n";

            //Interconnect
            for(const auto& kv : logical_net_sinks_) {
                int atom_net_idx = kv.first;
                auto driver_iter = logical_net_drivers_.find(atom_net_idx);
                assert(driver_iter != logical_net_drivers_.end());
                auto driver_wire = driver_iter->second.first;
                auto driver_tnode = driver_iter->second.second;

                for(auto& sink_wire_tnode_pair : kv.second) {
                    auto sink_wire = sink_wire_tnode_pair.first;
                    auto sink_tnode = sink_wire_tnode_pair.second;

                    sdf_os_ << indent(depth+1) << "(CELL\n";
                    sdf_os_ << indent(depth+2) << "(CELLTYPE \"fpga_interconnect\")\n";
                    sdf_os_ << indent(depth+2) << "(INSTANCE " << interconnect_name(driver_wire, sink_wire) << ")\n";
                    sdf_os_ << indent(depth+2) << "(DELAY\n";
                    sdf_os_ << indent(depth+3) << "(ABSOLUTE\n";

                    int delay = get_delay_ps(driver_tnode, sink_tnode);

                    std::stringstream delay_triple;
                    delay_triple << "(" << delay << ":" << delay << ":" << delay << ")";

                    sdf_os_ << indent(depth+4) << "(IOPATH datain dataout " << delay_triple.str() << " " << delay_triple.str() << ")\n";
                    sdf_os_ << indent(depth+3) << ")\n";
                    sdf_os_ << indent(depth+2) << ")\n";
                    sdf_os_ << indent(depth+1) << ")\n";
                    sdf_os_ << indent(depth) << "\n";
                }
            }

            //Cells
            for(auto& inst : cell_instances_) {
                sdf_os_ << indent(depth+1) << "(CELL\n";
                sdf_os_ << indent(depth+2) << "(CELLTYPE \"" << inst.type() << "\")\n";
                sdf_os_ << indent(depth+2) << "(INSTANCE " << inst.instance_name()<< ")\n";

                auto arcs = inst.timing_arcs();

                if(!arcs.empty()) {
                    sdf_os_ << indent(depth+2) << "(DELAY\n";
                    sdf_os_ << indent(depth+3) << "(ABSOLUTE\n";
                    
                    for(auto& pin_arc_pair : arcs) {
                        auto arc = pin_arc_pair.second;

                        int delay_ps = get_delay_ps(arc.delay());

                        std::stringstream delay_triple;
                        delay_triple << "(" << delay_ps << ":" << delay_ps << ":" << delay_ps << ")";

                        sdf_os_ << indent(depth+4) << "(IOPATH " << arc.source_name() << " " << arc.sink_name();
                        sdf_os_ << " " << delay_triple.str() << " " << delay_triple.str() << ")\n";
                    }
                    sdf_os_ << indent(depth+3) << ")\n";
                    sdf_os_ << indent(depth+2) << ")\n";
                }

                sdf_os_ << indent(depth+1) << ")\n";
                sdf_os_ << indent(depth) << "\n";
            }

            sdf_os_ << indent(depth) << ")\n";
        }

        std::string escape_name(const char* name) {
            std::string escaped_name = name;
            for(size_t i = 0; i < escaped_name.size(); i++) {
                //Replace invalid verilog characters (e.g. '^' , '~' , '[' , etc. ) with '_'
                if(escaped_name[i] == '^' || 
                    (int) escaped_name[i] < 48 || 
                   ((int) escaped_name[i] > 57 && (int) escaped_name[i] < 65) || 
                   ((int) escaped_name[i] > 90 && (int) escaped_name[i] < 97) ||
                    (int) escaped_name[i] > 122) {
                    escaped_name[i]='_';
                }
            }
            return escaped_name;
        }

        int find_num_inputs(const t_pb* pb) {
            int count = 0;
            for(int i = 0; i < pb->pb_graph_node->num_input_ports; i++) {
                count += pb->pb_graph_node->num_input_pins[i];
            }
            return count;
        }

        std::string make_inst_wire(int atom_net_idx, int tnode_id, std::string inst_name, PortDir dir, int port_idx, int pin_idx) {
            std::stringstream ss;
            ss << inst_name;
            if(dir == PortDir::IN) {
                ss << "_input";
            } else {
                assert(dir == PortDir::OUT);
                ss << "_output";
            }
            ss << "_" << port_idx;
            ss << "_" << pin_idx;

            std::string wire_name = ss.str();

            auto value = std::make_pair(wire_name, tnode_id);
            if(dir == PortDir::IN) {
                //Add the sink
                logical_net_sinks_[atom_net_idx].push_back(value);

            } else {
                //Add the driver
                auto ret = logical_net_drivers_.insert(std::make_pair(atom_net_idx, value));
                assert(ret.second); //Was inserted, drivers are unique
            }

            return wire_name;
        }

        std::string make_io(const t_pb* atom, PortDir dir) {
            std::string io_name = escape_name(atom->name);  
            

            const t_pb_graph_node* pb_graph_node = atom->pb_graph_node;

            int cluster_pin_idx = -1;
            if(dir == PortDir::IN) {
                assert(pb_graph_node->num_output_ports == 1); //One output port
                assert(pb_graph_node->num_output_pins[0] == 1); //One output pin
                cluster_pin_idx = pb_graph_node->output_pins[0][0].pin_count_in_cluster; //Unique pin index in cluster

            } else {
                assert(pb_graph_node->num_input_ports == 1); //One input port
                assert(pb_graph_node->num_input_pins[0] == 1); //One input pin
                cluster_pin_idx = pb_graph_node->input_pins[0][0].pin_count_in_cluster; //Unique pin index in cluster

                //Trip off the starting 'out_' that vpr adds to uniqify outputs
                //this makes the port names match the input blif file
                io_name = std::string(io_name.begin() + 4, io_name.end());
            }

            const t_block* top_block = find_top_block(atom);

            int atom_net_idx = top_block->pb_route[cluster_pin_idx].atom_net_idx; //Connected net in atom netlist

            //Port direction is inverted (inputs drive internal nets, outputs sink internal nets)
            PortDir wire_dir = (dir == PortDir::IN) ? PortDir::OUT : PortDir::IN;

            //Look up the tnode associated with this pin (used for delay calculation)
            int tnode_id = find_tnode(atom, cluster_pin_idx);

            auto wire_name = make_inst_wire(atom_net_idx, tnode_id, io_name, wire_dir, 0, 0);

            //Connect the wires to to I/Os with assign statements
            if(wire_dir == PortDir::IN) {
                assignments_.emplace_back(io_name, wire_name);
            } else {
                assignments_.emplace_back(wire_name, io_name);
            }
            
            return io_name;
        }

        LutInstance make_lut_instance(const t_pb* atom)  {
            //Determine what size LUT
            int lut_size = find_num_inputs(atom);

            //Determine the instance type
            std::stringstream ss;
            ss << "LUT_" << lut_size;
            auto inst_type = ss.str();

            //Determine the truth table
            auto lut_mask = load_lut_mask(lut_size, atom);

            //Determine the instance name
            auto inst_name = "lut_" + escape_name(atom->name);

            //Determine the port connections
            std::map<std::string,std::string> port_conns;

            const t_pb_graph_node* pb_graph_node = atom->pb_graph_node;
            assert(pb_graph_node->num_input_ports == 1); //LUT has one input port

            const t_block* top_block = find_top_block(atom);

            //Add inputs adding connections
            std::map<int,Arc> timing_arcs;
            for(int pin_idx = 0; pin_idx < pb_graph_node->num_input_pins[0]; pin_idx++) {
                int cluster_pin_idx = pb_graph_node->input_pins[0][pin_idx].pin_count_in_cluster; //Unique pin index in cluster
                int atom_net_idx = top_block->pb_route[cluster_pin_idx].atom_net_idx; //Connected net in atom netlist

                std::string port_name = "in_" + std::to_string(pin_idx);
                if(atom_net_idx == OPEN) {
                    //Disconnected

                    auto ret = port_conns.insert(std::make_pair(port_name, "")); 
                    assert(ret.second); //Was inserted
                } else {
                    //Connected to a net
                    
                    //Look up the tnode associated with this pin (used for delay calculation)
                    int tnode_id = find_tnode(atom, cluster_pin_idx);

                    std::string input_net = make_inst_wire(atom_net_idx, tnode_id, inst_name, PortDir::IN, 0, pin_idx);
                    auto ret = port_conns.insert(std::make_pair(port_name, input_net)); 
                    assert(ret.second); //Was inserted


                    //Record the timing arc
                    std::string source_name = "inter" + std::to_string(pin_idx) + "/datain";
                    std::string sink_name = "inter" + std::to_string(pin_idx) + "/dataout";

                    assert(tnode[tnode_id].num_edges == 1);
                    float delay = tnode[tnode_id].out_edges[0].Tdel;
                    Arc timing_arc(source_name, sink_name, delay);

                    timing_arcs.insert(std::make_pair(pin_idx,timing_arc));
                }
            }

            //Add the single output connection
            {
                assert(pb_graph_node->num_output_ports == 1); //LUT has one output port
                assert(pb_graph_node->num_output_pins[0] == 1); //LUT has one output pin
                int cluster_pin_idx = pb_graph_node->output_pins[0][0].pin_count_in_cluster; //Unique pin index in cluster
                int atom_net_idx = top_block->pb_route[cluster_pin_idx].atom_net_idx; //Connected net in atom netlist

                std::string port_name = "out";
                if(atom_net_idx == OPEN) {
                    //Disconnected

                    //We leave disconnected LUT output pins disconnected
                    auto ret = port_conns.insert(std::make_pair(port_name, "")); 
                    assert(ret.second); //Was inserted
                } else {
                    //Connected to a net

                    //Look up the tnode associated with this pin (used for delay calculation)
                    int tnode_id = find_tnode(atom, cluster_pin_idx);

                    std::string input_net = make_inst_wire(atom_net_idx, tnode_id, inst_name, PortDir::OUT, 0, 0);
                    auto ret = port_conns.insert(std::make_pair(port_name, input_net)); 
                    assert(ret.second); //Was inserted
                }
            }

            auto inst = LutInstance(inst_type, lut_mask, inst_name, port_conns, timing_arcs);

            return inst;
        }

        const t_block* find_top_block(const t_pb* curr) {
            //TODO: this is not very efficient...
            const t_pb* top_pb = find_top_clb(curr); 

            for(int i = 0; i < num_blocks; i++) {
                if(block[i].pb == top_pb) {
                    return &block[i];
                }
            }
            assert(false);
        }

        const t_pb* find_top_clb(const t_pb* curr) {
            //Walk up through the pb graph until curr
            //has no parent, at which point it will be the top pb
            const t_pb* parent = curr->parent_pb;
            while(parent != nullptr) {
                curr = parent;
                parent = curr->parent_pb;
            }
            return curr;
        }

        int find_tnode(const t_pb* atom, int cluster_pin_idx) {

            int clb_index = logical_block[atom->logical_block].clb_index;
            int tnode_id = pin_id_to_tnode_lookup_[clb_index][cluster_pin_idx];

            assert(tnode_id != OPEN);

            return tnode_id;
        }

        LogicVec load_lut_mask(size_t num_inputs, const t_pb* atom) {
            const t_model* model = logical_block[atom->logical_block].model;
            assert(model->name == std::string("names"));

            std::cout << "Loading LUT mask for: " << atom->name << std::endl;


            //Since the LUT inputs may have been rotated from the input blif specification we need to
            //figure out this permutation to reflect the physical implementation connectivity.
            //
            //We create a permutation map (which is a list of swaps from index to index)
            //which is then applied to do the rotation of the lutmask
            std::vector<int> permute(num_inputs, OPEN);

            std::cout << "\tInit Permute: {";
            for(size_t i = 0; i < permute.size(); i++) {
                std::cout << permute[i] << ", ";
            }
            std::cout << "}" << std::endl;

            //Determine the permutation
            //
            //We walk through the logical inputs to this atom (i.e. in the original truth table/netlist)
            //and find the corresponding input in the implementation atom (i.e. in the current netlist)
            for(size_t i = 0; i < num_inputs; i++) {
                int logical_net = logical_block[atom->logical_block].input_nets[0][i]; //The original net in the input netlist

                if(logical_net != OPEN) {
                    int atom_input_net = OPEN;
                    size_t j;
                    for(j = 0; j < num_inputs; j++) {
                        atom_input_net = find_atom_input_logical_net(atom, j); //The net currently connected to input j

                        if(logical_net == atom_input_net) {
                            std::cout << "\tLogic net " << logical_net << " (" << g_atoms_nlist.net[logical_net].name << ") atom lut input " << i << " -> impl lut input " << j << std::endl;
                            break;
                        }
                    }
                    assert(atom_input_net == logical_net);

                    permute[j] = i;
                }
            }

            //Fill in any missing values in the permutation (i.e. zeros)
            std::set<int> perm_indicies(permute.begin(), permute.end());
            size_t unused_index = 0;
            for(size_t i = 0; i < permute.size(); i++) {
                if(permute[i] == OPEN) {
                    while(perm_indicies.count(unused_index)) {
                        unused_index++;
                    }
                    permute[i] = unused_index;
                    perm_indicies.insert(unused_index);
                }
            }



            std::cout << "\tPermute: {";
            for(size_t k = 0; k < permute.size(); k++) {
                std::cout << permute[k] << ", ";
            }
            std::cout << "}" << std::endl;


            std::cout << "\tBLIF = Input ->  Rotated" << std::endl;
            std::cout << "\t------------------------" << std::endl;
            
            //VPR stores the truth table as in BLIF
            //Each row of the table (i.e. a c-string) is stored in a linked list 
            //
            //The c-string is the literal row from BLIF, e.g. "0 1" for an inverter, "11 1" for an AND2
            t_linked_vptr* names_row_ptr = logical_block[atom->logical_block].truth_table;


            //Determine whether the truth table stores the ON or OFF set
            //
            //  In blif, the 'output values' of a .names must be either '1' or '0', and must be consistent
            //  within a single .names -- that is a single .names can encode either the ON or OFF set 
            //  (of which only one will be encoded in a single .names)
            //
            const std::string names_first_row = (const char*) names_row_ptr->data_vptr;
            auto names_first_row_output_iter = names_first_row.end() - 1;

            //Extract the truth (output value) for this row
            bool encoding_on_set = false;
            if(*names_first_row_output_iter == '1') {
                encoding_on_set = true; 
            } else if (*names_first_row_output_iter == '0') {
                encoding_on_set = false; 
            } else {
                assert(false);
            }

            //Initialize LUT mask
            int lut_bits = std::pow(2, num_inputs);
            LogicVec lut_mask;
            if(encoding_on_set) {
                //Encoding the ON set, so the 'background' value for unspecified
                //minterms is FALSE
                lut_mask = LogicVec(lut_bits, LogicVal::FALSE); 
            } else {
                //Encoding the OFF set, so the 'background' value for unspecified
                //minterms is TRUE
                lut_mask = LogicVec(lut_bits, LogicVal::TRUE); 
            }

            //Process each row
            while(names_row_ptr != nullptr) {
                const std::string names_row = (const char*) names_row_ptr->data_vptr;

                auto output_val_iter = names_row.end() - 1;
                auto space_iter = names_row.end() - 2;

                assert(*space_iter == ' ');
                
                //Extract the truth (output value) for this row
                LogicVal output_val = LogicVal::UNKOWN;
                if(*output_val_iter == '1') {
                    assert(encoding_on_set);
                    output_val = LogicVal::TRUE; 
                } else if (*output_val_iter == '0') {
                    assert(!encoding_on_set);
                    output_val = LogicVal::FALSE; 
                } else {
                    assert(false);
                }

                //Extract the input values for this row
                LogicVec input_values(num_inputs, LogicVal::FALSE);
                size_t i = 0;
                while(names_row[i] != ' ') {
                    LogicVal input_val = LogicVal::UNKOWN;
                    if(names_row[i] == '1') {
                        input_val = LogicVal::TRUE; 
                    } else if (names_row[i] == '0') {
                        input_val = LogicVal::FALSE; 
                    } else if (names_row[i] == '-') {
                        input_val = LogicVal::DONTCARE; 
                    } else {
                        assert(false);
                    }

                    input_values[i] =  input_val;
                    /*std::cout << "Setting input " << i << " " << input_val << ": " << input_values << std::endl;*/
                    i++;
                }
                assert(i == num_inputs);

                //Apply any LUT input rotations
                auto permuted_input_values = input_values;
                permuted_input_values.rotate(permute);

                std::cout << "\t" << names_row << " = "<< input_values << ":" << output_val;

                std::cout << " -> " << permuted_input_values << ":" << output_val << std::endl;

                for(size_t minterm : permuted_input_values.minterms()) {
                    std::cout << "\tSetting minterm : " << minterm << " to " << output_val << std::endl;
                    //Set the appropraite lut mask entry
                    lut_mask[minterm] = output_val;
                }
                
                //Advance to the next row
                names_row_ptr = names_row_ptr->next;
            }

            std::cout << "\tLUT_MASK: " << lut_mask << std::endl;

            return lut_mask;
        }

        int find_atom_input_logical_net(const t_pb* atom, int atom_input_idx) {
            const t_pb_graph_node* pb_node = atom->pb_graph_node;

            int cluster_pin_idx = pb_node->input_pins[0][atom_input_idx].pin_count_in_cluster;
            const t_block* top_clb = find_top_block(atom);
            int atom_net_idx = top_clb->pb_route[cluster_pin_idx].atom_net_idx;
            return atom_net_idx;
        }

#if 0
        std::vector<int> expand_input_values_to_lut_mask_index(int lut_bits, LogicVec input_values) {
            //Return a vector of indicies, since a single set of input values (if they have don't cares)
            //can expend to multiplie indicies.
            //However currently we do not support don't cares in the input values.
            std::vector<int> lut_mask_indicies;


            //We invert the index here to make the actual LUT mask parameter sensible,
            //by intializing it to the highest index in the lut_mask string, and then
            //subtracting the powers for each input that is specified as true.
            //
            //For example:
            //
            //  Consider an AND2 whose truth table looks like:
            //      .names a b a_and_b
            //      11 1
            //
            //  The caller of this function should have already padded this out expanded to fill out the 6-LUT as:
            //      .names unconn5 unconn4 unconn3 unconn2 a b a_and_b
            //      000011 1
            //
            //  (The corresponding input_values is "000011")
            //
            //  The caller should also have already handled applying any lut rotations
            //
            //  This implies that minterm number 3 (= 2^1 + 2^0) should be set to '1'
            //
            //  We initialize lut_mask_idx to 63 (since there are 2^6 == 64 bits in the lut mask)
            //
            //  Index 63 corresponds to the right-most character in the std::string
            //  used to represent the lut mask:
            //
            //  logic minterm:    m63                                                             m0
            //                     v                                                              v
            //
            //  lut_mask     :     0000000000000000000000000000000000000000000000000000000000000000
            //
            //                     ^                                                              ^
            //  string idex  :    i0                                                              i63
            //
            //   which corresponds to minterm zero (i.e. all LUT inputs zero).
            //
            //   We then subtract the corresponding power (i.e. 2^i) to get the string index
            //   corresponding to the appropriate case:
            //
            //       lut_mask_index = 63 - 2^1 - 2^0 = 60
            //
            //   Which is then used to set the appropriate lut mask bit:
            //      m63                                                         m3  m0
            //                                                                   v 
            //
            //       0000000000000000000000000000000000000000000000000000000000001000
            //
            //                                                                   ^ 
            //      i0                                                          i60 i63
            int lut_mask_idx = lut_bits-1;
            for(size_t i = 0; i < input_values.size(); i++) {

                if(input_values[i] == '1') {
                    int index_power = (1 << i); 
                    lut_mask_idx -= index_power;
                } else {
                    assert(input_values[i] == '0'); //Currently no support for don't cares
                }
            }
            lut_mask_indicies.push_back(lut_mask_idx);

            return lut_mask_indicies;
        }
#endif

        int get_delay_ps(float delay_sec) {

            return delay_sec * 1e12 + 0.5; //Scale and rounding
        }

        int get_delay_ps(int source_tnode, int sink_tnode) {
            float source_delay = tnode[source_tnode].T_arr;
            float sink_delay = tnode[sink_tnode].T_arr;

            float delay_sec = sink_delay - source_delay;


            return get_delay_ps(delay_sec);
        }

        std::string interconnect_name(std::string driver_wire, std::string sink_wire) {
            return "routing_segment_" + driver_wire + "_to_" + sink_wire;
        }

        std::string indent(size_t depth) {
            std::string new_indent;
            for(size_t i = 0; i < depth; ++i) {
                new_indent += indent_;
            }
            return new_indent;
        }

    private: //Data
        std::string top_module_name_;
        std::vector<std::string> inputs_;
        std::vector<std::string> outputs_;
        std::vector<Assignment> assignments_;
        std::vector<LutInstance> cell_instances_;

        std::map<int, std::pair<std::string,int>> logical_net_drivers_; //Value: pair of wire_name and tnode_id
        std::map<int, std::vector<std::pair<std::string,int>>> logical_net_sinks_; //Value: vector wire_name tnode_id pairs
        std::map<std::string, float> logical_net_sink_delays_;

        int** pin_id_to_tnode_lookup_;

        std::ostream& verilog_os_;
        std::ostream& blif_os_;
        std::ostream& sdf_os_;
        std::string indent_ = "    ";
};

void verilog_writer2() {
    std::string top_level_name = blif_circuit_name;
    std::string verilog_filename = top_level_name + "_post_synthesis.v";
    std::string blif_filename = top_level_name + "_post_synthesis.blif";
    std::string sdf_filename = top_level_name + "_post_synthesis.sdf";

    std::ofstream verilog_os(verilog_filename);
    std::ofstream blif_os(blif_filename);
    std::ofstream sdf_os(sdf_filename);

    VerilogSdfWriterVisitor visitor(verilog_os, blif_os, sdf_os);

    NetlistWalker nl_walker(visitor);

    nl_walker.walk();

}
