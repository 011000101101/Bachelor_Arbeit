#include <cstdio>
#include <cstring>
#include <ctime>
#include <sstream>
#include <unordered_set>
using namespace std;

#include "blifparse.hpp"
#include "atom_netlist.h"
#include "atom_netlist_utils.h"

#include "vtr_assert.h"
#include "vtr_util.h"
#include "vtr_list.h"
#include "vtr_log.h"
#include "vtr_logic.h"
#include "vtr_time.h"

#include "vpr_types.h"
#include "vpr_error.h"
#include "globals.h"
#include "read_blif.h"
#include "arch_types.h"
#include "ReadOptions.h"
#include "hash.h"

/* PRINT_PIN_NETS */

struct s_model_stats {
	const t_model * model;
	int count;
};

#define MAX_ATOM_PARSE 200000000

/* This source file will read in a FLAT blif netlist consisting     *
 * of .inputs, .outputs, .names and .latch commands.  It currently   *
 * does not handle hierarchical blif files.  Hierarchical            *
 * blif files can be flattened via the read_blif and write_blif      *
 * commands of sis.  LUT circuits should only have .names commands;  *
 * there should be no gates.  This parser performs limited error     *
 * checking concerning the consistency of the netlist it obtains.    *
 * .inputs and .outputs statements must be given; this parser does   *
 * not infer primary inputs and outputs from non-driven and fanout   *
 * free nodes.  This parser can be extended to do this if necessary, *
 * or the sis read_blif and write_blif commands can be used to put a *
 * netlist into the standard format.                                 *
 * V. Betz, August 25, 1994.                                         *
 * Added more error checking, March 30, 1995, V. Betz                */

static void read_blif2(const char *blif_file, bool absorb_buffers, bool sweep_hanging_nets_and_inputs,
		const t_model *user_models, const t_model *library_models,
		bool read_activity_file, char * activity_file);
static void show_blif_stats2(const AtomNetlist& netlist);
static std::unordered_map<AtomNetId,t_net_power> read_activity2(const AtomNetlist& netlist, char * activity_file);
bool add_activity_to_net2(const AtomNetlist& netlist, std::unordered_map<AtomNetId,t_net_power>& atom_net_power,
                          char * net_name, float probability, float density);

void blif_error(int lineno, std::string near_text, std::string msg);

void blif_error(int lineno, std::string near_text, std::string msg) {
    vpr_throw(VPR_ERROR_BLIF_F, "", lineno,
            "Error in blif file near '%s': %s\n", near_text.c_str(), msg.c_str());
}

vtr::LogicValue to_vtr_logic_value(blifparse::LogicValue);

struct BlifAllocCallback : public blifparse::Callback {
    public:
        BlifAllocCallback(AtomNetlist& main_netlist, const t_model* user_models, const t_model* library_models)
            : main_netlist_(main_netlist)
            , user_arch_models_(user_models) 
            , library_arch_models_(library_models) {}

        static constexpr const char* OUTPAD_NAME_PREFIX = "out:";

    public: //Callback interface
        void start_parse() override {}

        void finish_parse() override {
            //When parsing is finished we *move* the main netlist
            //into the user object. This ensures we never have two copies
            //(consuming twice the memory).
            size_t main_netlist_idx = determine_main_netlist_index();
            main_netlist_ = std::move(blif_models_[main_netlist_idx]); 
        }

        void begin_model(std::string model_name) override { 
            //Create a new model, and set it's name

            blif_models_.emplace_back(model_name);
            blif_models_black_box_.emplace_back(false);
            ended_ = false;
        }

        void inputs(std::vector<std::string> input_names) override {
            const t_model* blk_model = find_model("input");

            VTR_ASSERT_MSG(!blk_model->inputs, "Inpad model has an input port");
            VTR_ASSERT_MSG(blk_model->outputs, "Inpad model has no output port");
            VTR_ASSERT_MSG(blk_model->outputs->size == 1, "Inpad model has non-single-bit output port");
            VTR_ASSERT_MSG(!blk_model->outputs->next, "Inpad model has multiple output ports");

            std::string pin_name = blk_model->outputs->name;
            for(const auto& input : input_names) {
                AtomBlockId blk_id = curr_model().create_block(input, blk_model);
                AtomPortId port_id = curr_model().create_port(blk_id, blk_model->outputs->name);
                AtomNetId net_id = curr_model().create_net(input);
                curr_model().create_pin(port_id, 0, net_id, AtomPinType::DRIVER);
            }
        }

        void outputs(std::vector<std::string> output_names) override { 
            const t_model* blk_model = find_model("output");

            VTR_ASSERT_MSG(!blk_model->outputs, "Outpad model has an output port");
            VTR_ASSERT_MSG(blk_model->inputs, "Outpad model has no input port");
            VTR_ASSERT_MSG(blk_model->inputs->size == 1, "Outpad model has non-single-bit input port");
            VTR_ASSERT_MSG(!blk_model->inputs->next, "Outpad model has multiple input ports");

            std::string pin_name = blk_model->inputs->name;
            for(const auto& output : output_names) {
                //Since we name blocks based on thier drivers we need to uniquify outpad names,
                //which we do with a prefix
                AtomBlockId blk_id = curr_model().create_block(OUTPAD_NAME_PREFIX + output, blk_model);
                AtomPortId port_id = curr_model().create_port(blk_id, blk_model->inputs->name);
                AtomNetId net_id = curr_model().create_net(output);
                curr_model().create_pin(port_id, 0, net_id, AtomPinType::SINK);
            }
        }

        void names(std::vector<std::string> nets, std::vector<std::vector<blifparse::LogicValue>> so_cover) override { 
            const t_model* blk_model = find_model("names");

            VTR_ASSERT_MSG(nets.size() > 0, "BLIF .names has no connections");
            
            VTR_ASSERT_MSG(blk_model->inputs, ".names model has no input port");
            VTR_ASSERT_MSG(!blk_model->inputs->next, ".names model has multiple input ports");
            VTR_ASSERT_MSG(blk_model->inputs->size >= static_cast<int>(nets.size()) - 1, ".names model does not match blif .names input size");

            VTR_ASSERT_MSG(blk_model->outputs, ".names has no output port");
            VTR_ASSERT_MSG(!blk_model->outputs->next, ".names model has multiple output ports");
            VTR_ASSERT_MSG(blk_model->outputs->size == 1, ".names model has non-single-bit output");

            //Convert the single-output cover to a netlist truth table
            AtomNetlist::TruthTable truth_table;
            for(const auto& row : so_cover) {
                truth_table.emplace_back();
                for(auto val : row) {
                    truth_table[truth_table.size()-1].push_back(to_vtr_logic_value(val));
                }
            }

            AtomBlockId blk_id = curr_model().create_block(nets[nets.size()-1], blk_model, truth_table);

            //Create inputs
            AtomPortId input_port_id = curr_model().create_port(blk_id, blk_model->inputs->name);
            for(size_t i = 0; i < nets.size() - 1; ++i) {
                AtomNetId net_id = curr_model().create_net(nets[i]);

                curr_model().create_pin(input_port_id, i, net_id, AtomPinType::SINK);
            }

            //Figure out if the output is a constant generator
            bool output_is_const = false;
            if(truth_table.empty()
                || (truth_table.size() == 1 && truth_table[0].size() == 1 && truth_table[0][0] == vtr::LogicValue::FALSE)) {
                //An empty truth table in BLIF corresponds to a constant-zero
                //  e.g.
                //
                //  #gnd is a constant 0 generator
                //  .names gnd 
                //
                //An single entry truth table with value '0' also corresponds to a constant-zero
                //  e.g.
                //
                //  #gnd2 is a constant 0 generator
                //  .names gnd2
                //  0
                //
                output_is_const = true;
                vtr::printf("Found constant-zero generator '%s'\n", nets[nets.size()-1].c_str());
            } else if(truth_table.size() == 1 && truth_table[0].size() == 1 && truth_table[0][0] == vtr::LogicValue::TRUE) {
                //A single-entry truth table with value '1' in BLIF corresponds to a constant-one
                //  e.g.
                //
                //  #vcc is a constant 1 generator
                //  .names vcc
                //  1
                //
                output_is_const = true;
                vtr::printf("Found constant-one generator '%s'\n", nets[nets.size()-1].c_str());
            }

            //Create output
            AtomNetId net_id = curr_model().create_net(nets[nets.size()-1]);
            AtomPortId output_port_id = curr_model().create_port(blk_id, blk_model->outputs->name);
            curr_model().create_pin(output_port_id, 0, net_id, AtomPinType::DRIVER, output_is_const);
        }

        void latch(std::string input, std::string output, blifparse::LatchType type, std::string control, blifparse::LogicValue init) override {
            if(type == blifparse::LatchType::UNSPECIFIED) {
                vtr::printf_warning(filename_.c_str(), lineno_, "Treating latch '%s' of unspecified type as rising edge triggered\n", output.c_str());
            } else if(type != blifparse::LatchType::RISING_EDGE) {
                vpr_throw(VPR_ERROR_BLIF_F, filename_.c_str(), lineno_, "Only rising edge latches supported\n");
            }
            
            const t_model* blk_model = find_model("latch");

            VTR_ASSERT_MSG(blk_model->inputs, "Has one input port");
            VTR_ASSERT_MSG(blk_model->inputs->next, "Has two input port");
            VTR_ASSERT_MSG(!blk_model->inputs->next->next, "Has no more than two input port");
            VTR_ASSERT_MSG(blk_model->outputs, "Has one output port");
            VTR_ASSERT_MSG(!blk_model->outputs->next, "Has no more than one input port");

            const t_model_ports* d_model_port = blk_model->inputs;
            const t_model_ports* clk_model_port = blk_model->inputs->next;
            const t_model_ports* q_model_port = blk_model->outputs;

            VTR_ASSERT(d_model_port->name == std::string("D"));
            VTR_ASSERT(clk_model_port->name == std::string("clk"));
            VTR_ASSERT(q_model_port->name == std::string("Q"));
            VTR_ASSERT(d_model_port->size == 1);
            VTR_ASSERT(clk_model_port->size == 1);
            VTR_ASSERT(q_model_port->size == 1);
            VTR_ASSERT(clk_model_port->is_clock);

            //We set the initital value as a single entry in the 'truth_table' field
            AtomNetlist::TruthTable truth_table(1);
            truth_table[0].push_back(to_vtr_logic_value(init));

            AtomBlockId blk_id = curr_model().create_block(output, blk_model, truth_table);

            //The input
            AtomPortId d_port_id = curr_model().create_port(blk_id, d_model_port->name);
            AtomNetId d_net_id = curr_model().create_net(input);
            curr_model().create_pin(d_port_id, 0, d_net_id, AtomPinType::SINK);

            //The output
            AtomPortId q_port_id = curr_model().create_port(blk_id, q_model_port->name);
            AtomNetId q_net_id = curr_model().create_net(output);
            curr_model().create_pin(q_port_id, 0, q_net_id, AtomPinType::DRIVER);

            //The clock
            AtomPortId clk_port_id = curr_model().create_port(blk_id, clk_model_port->name);
            AtomNetId clk_net_id = curr_model().create_net(control);
            curr_model().create_pin(clk_port_id, 0, clk_net_id, AtomPinType::SINK);
        }

        void subckt(std::string subckt_model, std::vector<std::string> ports, std::vector<std::string> nets) override {
            VTR_ASSERT(ports.size() == nets.size());

            const t_model* blk_model = find_model(subckt_model);

            std::string first_output_name;
            for(size_t i = 0; i < ports.size(); ++i) {
                const t_model_ports* model_port = find_model_port(blk_model, ports[i]);
                VTR_ASSERT(model_port);

                //Determine the pin type
                if(model_port->dir == OUT_PORT) {
                    first_output_name = nets[i];
                    break;
                }
            }
            //We must have an output in-order to name the subckt
            // Also intuitively the subckt can be swept if it has no outside effect
            if(first_output_name.empty()) {
                vpr_throw(VPR_ERROR_BLIF_F, filename_.c_str(), lineno_, "Found no output pin on .subckt '%s'",
                          subckt_model.c_str());
            }


            AtomBlockId blk_id = curr_model().create_block(first_output_name, blk_model);


            for(size_t i = 0; i < ports.size(); ++i) {
                //Check for consistency between model and ports
                const t_model_ports* model_port = find_model_port(blk_model, ports[i]);
                VTR_ASSERT(model_port);

                //Determine the pin type
                AtomPinType pin_type = AtomPinType::SINK;
                if(model_port->dir == OUT_PORT) {
                    pin_type = AtomPinType::DRIVER;
                } else {
                    VTR_ASSERT_MSG(model_port->dir == IN_PORT, "Unexpected port type");
                }

                //Make the port
                std::string port_base;
                size_t port_bit;
                std::tie(port_base, port_bit) = split_index(ports[i]);

                AtomPortId port_id = curr_model().create_port(blk_id, port_base);

                //Make the net
                AtomNetId net_id = curr_model().create_net(nets[i]);

                //Make the pin
                curr_model().create_pin(port_id, port_bit, net_id, pin_type);
            }
        }

        void blackbox() override {
            //We treat black-boxes as netlists during parsing so they should contain
            //only inpads/outpads
            for(const auto& blk_id : curr_model().blocks()) {
                auto blk_type = curr_model().block_type(blk_id);
                if(!(blk_type == AtomBlockType::INPAD || blk_type == AtomBlockType::OUTPAD)) {
                    vpr_throw(VPR_ERROR_BLIF_F, filename_.c_str(), lineno_, "Unexpected primitives in blackbox model");
                }
            }
            set_curr_model_blackbox(true);
        }

        void end_model() override {
            if(ended_) {
                vpr_throw(VPR_ERROR_BLIF_F, filename_.c_str(), lineno_, "Unexpected .end");
            }
            ended_ = true;
        }

        void filename(std::string fname) override { filename_ = fname; }

        void lineno(int line_num) override { lineno_ = line_num; }
    public:
        //Retrieve the netlist
        size_t determine_main_netlist_index() { 
            //Look through all the models loaded, to find the one which is non-blackbox (i.e. has real blocks
            //and is not a blackbox).  To check for errors we look at all models, even if we've already
            //found a non-blackbox model.
            int top_model_idx = -1; //Not valid

            for(int i = 0; i < static_cast<int>(blif_models_.size()); ++i) {
                if(!blif_models_black_box_[i]) {
                    //A non-blackbox model
                    if(top_model_idx == -1) {
                        //This is the top model
                        top_model_idx = i;
                    } else {
                        //We already have a top model
                        vpr_throw(VPR_ERROR_BLIF_F, filename_.c_str(), lineno_, 
                                "Found multiple models with primitives. "
                                "Only one model can contain primitives, the others must be blackboxes.");
                    }
                } else {
                    //Verify blackbox models match the architecture
                    verify_blackbox_model(blif_models_[i]);
                }
            }

            if(top_model_idx == -1) {
                vpr_throw(VPR_ERROR_BLIF_F, filename_.c_str(), lineno_, 
                        "No non-blackbox models found. The main model must not be a blackbox.");
            }

            //Return the main model
            VTR_ASSERT(top_model_idx >= 0);
            return static_cast<size_t>(top_model_idx);
        }

    private:
        const t_model* find_model(std::string name) {
            const t_model* arch_model = nullptr;
            for(const t_model* arch_models : {user_arch_models_, library_arch_models_}) {
                arch_model = arch_models;
                while(arch_model) {
                    if(name == arch_model->name) {
                        //Found it
                        break;
                    }
                    arch_model = arch_model->next;
                }
                if(arch_model) {
                    //Found it
                    break;
                }
            }
            if(!arch_model) {
                vpr_throw(VPR_ERROR_BLIF_F, filename_.c_str(), lineno_, "Failed to find matching architecture model for '%s'\n",
                          name.c_str());
            }
            return arch_model;
        }

        const t_model_ports* find_model_port(const t_model* blk_model, std::string port_name) {
            //We need to handle both single, and multi-bit port names
            //
            //By convention multi-bit port names have the bit index stored in square brackets
            //at the end of the name. For example:
            //
            //   my_signal_name[2]
            //
            //indicates the 2nd bit of the port 'my_signal_name'.
            std::string trimmed_port_name;
            int bit_index;

            //Extract the index bit
            std::tie(trimmed_port_name, bit_index) = split_index(port_name);
             
            //We now have the valid bit index and port_name is the name excluding the index info
            VTR_ASSERT(bit_index >= 0);

            //We now look through all the ports on the model looking for the matching port
            for(const t_model_ports* ports : {blk_model->inputs, blk_model->outputs}) {

                const t_model_ports* curr_port = ports;
                while(curr_port) {
                    if(trimmed_port_name == curr_port->name) {
                        //Found a matching port, we need to verify the index
                        if(bit_index < curr_port->size) {
                            //Valid port index
                            return curr_port;
                        } else {
                            //Out of range
                            vpr_throw(VPR_ERROR_BLIF_F, filename_.c_str(), lineno_, 
                                     "Port '%s' on architecture model '%s' exceeds port width (%d bits)\n",
                                      port_name.c_str(), blk_model->name, curr_port->size);
                        }
                    }
                    curr_port = curr_port->next;
                }
            }

            //No match
            vpr_throw(VPR_ERROR_BLIF_F, filename_.c_str(), lineno_, 
                     "Found no matching port '%s' on architecture model '%s'\n",
                      port_name.c_str(), blk_model->name);
            return nullptr;
        }

        //Splits the index off a signal name and returns the base signal name (excluding
        //the index) and the index as an integer. For example
        //
        //  "my_signal_name[2]"   -> "my_signal_name", 2
        std::pair<std::string, int> split_index(const std::string& signal_name) {
            int bit_index = 0;

            std::string trimmed_signal_name = signal_name;

            auto iter = --signal_name.end(); //Initialized to the last char
            if(*iter == ']') {
                //The name may end in an index
                //
                //To verify, we iterate back through the string until we find
                //an open square brackets, or non digit character
                --iter; //Advance past ']'
                while(iter != signal_name.begin() && std::isdigit(*iter)) --iter;

                //We are at the first non-digit character from the end (or the beginning of the string)
                if(*iter == '[') {
                    //We have a valid index in the open range (iter, --signal_name.end())
                    std::string index_str(iter+1, --signal_name.end());

                    //Convert to an integer
                    std::stringstream ss(index_str);
                    ss >> bit_index;
                    VTR_ASSERT_MSG(!ss.fail() && ss.eof(), "Failed to extract signal index");

                    //Trim the signal name to exclude the final index
                    trimmed_signal_name = std::string(signal_name.begin(), iter);
                }
            }
            return std::make_pair(trimmed_signal_name, bit_index);
        }

        //Retieves a reference to the currently active .model
        AtomNetlist& curr_model() { 
            if(blif_models_.empty() || ended_) {
                vpr_throw(VPR_ERROR_BLIF_F, filename_.c_str(), lineno_, "Expected .model");
            }

            return blif_models_[blif_models_.size()-1]; 
        }

        void set_curr_model_blackbox(bool val) {
            VTR_ASSERT(blif_models_.size() == blif_models_black_box_.size());
            blif_models_black_box_[blif_models_black_box_.size()-1] = val;
        }

        bool verify_blackbox_model(AtomNetlist& blif_model) {
            const t_model* arch_model = find_model(blif_model.netlist_name());

            //Verify each port on the model
            //
            // We parse each .model as it's own netlist so the IOs
            // get converted to blocks
            for(auto blk_id : blif_model.blocks()) {


                //Check that the port directions match
                if(blif_model.block_type(blk_id) == AtomBlockType::INPAD) {

                    const auto& input_name = blif_model.block_name(blk_id);

                    //Find model port already verifies the port widths
                    const t_model_ports* arch_model_port = find_model_port(arch_model, input_name);
                    VTR_ASSERT(arch_model_port);
                    VTR_ASSERT(arch_model_port->dir == IN_PORT);

                } else {
                    VTR_ASSERT(blif_model.block_type(blk_id) == AtomBlockType::OUTPAD);

                    auto raw_output_name = blif_model.block_name(blk_id);

                    std::string output_name = vtr::replace_first(raw_output_name, OUTPAD_NAME_PREFIX, "");

                    //Find model port already verifies the port widths
                    const t_model_ports* arch_model_port = find_model_port(arch_model, output_name);
                    VTR_ASSERT(arch_model_port);

                    VTR_ASSERT(arch_model_port->dir == OUT_PORT);
                }
            }
            return true;
        }

    private:
        bool ended_ = true; //Initially no active .model
        std::string filename_;
        int lineno_;

        std::vector<AtomNetlist> blif_models_;
        std::vector<bool> blif_models_black_box_;

        AtomNetlist& main_netlist_; //User object we fill
        const t_model* user_arch_models_;
        const t_model* library_arch_models_;

};


vtr::LogicValue to_vtr_logic_value(blifparse::LogicValue val) {
    vtr::LogicValue new_val;
    switch(val) {
        case blifparse::LogicValue::TRUE: new_val = vtr::LogicValue::TRUE; break;
        case blifparse::LogicValue::FALSE: new_val = vtr::LogicValue::FALSE; break;
        case blifparse::LogicValue::DONT_CARE: new_val = vtr::LogicValue::DONT_CARE; break;
        case blifparse::LogicValue::UNKOWN: new_val = vtr::LogicValue::UNKOWN; break;
        default: VTR_ASSERT_MSG(false, "Unkown logic value");
    }
    return new_val;
}

static void read_blif2(const char *blif_file, bool absorb_buffers, bool sweep_hanging_nets_and_inputs,
		const t_model *user_models, const t_model *library_models,
		bool read_activity_file, char * activity_file) {

    //Throw VPR errors instead of using libblifparse default error
    blifparse::set_blif_error_handler(blif_error);

    AtomNetlist netlist;
    {
        vtr::ScopedPrintTimer t1("Load BLIF");

        BlifAllocCallback alloc_callback(netlist, user_models, library_models);
        blifparse::blif_parse_filename(blif_file, alloc_callback);
    }

    {
        vtr::ScopedPrintTimer t2("Verify BLIF");
        netlist.verify();
    }

    netlist.print_stats();

    {
        vtr::ScopedPrintTimer t2("Clean BLIF");
        
        //Clean-up lut buffers
        if(absorb_buffers) {
            absorb_buffer_luts(netlist);
        }

        //Remove the special 'unconn' net
        AtomNetId unconn_net_id = netlist.find_net("unconn");
        if(unconn_net_id) {
            netlist.remove_net(unconn_net_id);
        }
        AtomBlockId unconn_blk_id = netlist.find_block("unconn");
        if(unconn_blk_id) {
            netlist.remove_block(unconn_blk_id);
        }

        //Sweep unused logic/nets/inputs/outputs
        //TODO: sweep iteratively, for now sweep only inputs/nets to match old behavior
        if(sweep_hanging_nets_and_inputs) {
            sweep_nets(netlist); 
            sweep_inputs(netlist); 
        }
        /*sweep_iterative(netlist, false);*/
    }

    {
        vtr::ScopedPrintTimer t2("Compress BLIF");

        //Compress the netlist to clean-out invalid entries
        netlist.compress();
        netlist.print_stats();
    }
    {
        vtr::ScopedPrintTimer t2("Verify BLIF");

        netlist.verify();
    }

    show_blif_stats2(netlist);

    if(read_activity_file) {
        auto atom_net_power = read_activity2(netlist, activity_file);
        g_atom_net_power = std::move(atom_net_power);
    }

    /*
     *{
     *    vtr::ScopedPrintTimer t2("Print BLIF");
     *    print_netlist(stdout, netlist);
     *}
     */

    /*
     *{
     *    vtr::ScopedPrintTimer t2("Echo File BLIF");
     *    FILE* f = vtr::fopen("atom_netlist.echo", "w");
     *    VTR_ASSERT(f);
     *    print_netlist_as_blif(f, netlist);
     *    fclose(f);
     *}
     */

    g_atom_nl = std::move(netlist);
}
static void show_blif_stats2(const AtomNetlist& netlist) {
    std::map<std::string,size_t> block_type_counts;

    //Count the block statistics
    for(auto blk_id : netlist.blocks()) {

        const t_model* blk_model = netlist.block_model(blk_id);
        if(blk_model->name == std::string("names")) {
            //LUT
            size_t lut_size = 0;
            auto in_ports = netlist.block_input_ports(blk_id);

            //May have zero (no input LUT) or one input port
            if(in_ports.size() == 1) {
                auto port_id = *in_ports.begin();

                //Figure out the LUT size
                lut_size = netlist.port_width(port_id);

            } else {
                VTR_ASSERT(in_ports.size() == 0);
            }

            ++block_type_counts[std::to_string(lut_size) + "-LUT"];
        } else {
            //Other types
            ++block_type_counts[blk_model->name];
        }
    }
    //Count the net statistics
    std::map<std::string,double> net_stats;
    for(auto net_id : netlist.nets()) {
        double fanout = netlist.net_sinks(net_id).size();

        net_stats["Max Fanout"] = std::max(net_stats["Max Fanout"], fanout);

        if(net_stats.count("Min Fanout")) {
            net_stats["Min Fanout"] = std::min(net_stats["Min Fanout"], fanout);
        } else {
            net_stats["Min Fanout"] = fanout;
        }

        net_stats["Avg Fanout"] += fanout;
    }
    net_stats["Avg Fanout"] /= netlist.nets().size();

    //Determine the maximum length of a type name for nice formatting
    size_t max_block_type_len = 0;
    for(auto kv : block_type_counts) {
        max_block_type_len = std::max(max_block_type_len, kv.first.size());
    }
    size_t max_net_type_len = 0;
    for(auto kv : net_stats) {
        max_net_type_len = std::max(max_net_type_len, kv.first.size());
    }

    //Print the statistics
    vtr::printf_info("Blif Circuit Statistics:\n"); 
    vtr::printf_info("  Blocks: %zu\n", netlist.blocks().size()); 
    for(auto kv : block_type_counts) {
        vtr::printf_info("    %-*s: %5zu\n", max_block_type_len, kv.first.c_str(), kv.second);
    }
    vtr::printf_info("  Nets  : %zu\n", netlist.nets().size()); 
    for(auto kv : net_stats) {
        vtr::printf_info("    %-*s: %6.1f\n", max_net_type_len, kv.first.c_str(), kv.second);
    }
}

static std::unordered_map<AtomNetId,t_net_power> read_activity2(const AtomNetlist& netlist, char * activity_file) {
	char buf[vtr::BUFSIZE];
	char * ptr;
	char * word1;
	char * word2;
	char * word3;

	FILE * act_file_hdl;

    std::unordered_map<AtomNetId,t_net_power> atom_net_power;

	for (auto net_id : netlist.nets()) {
		atom_net_power[net_id].probability = -1.0;
		atom_net_power[net_id].density = -1.0;
	}

	act_file_hdl = vtr::fopen(activity_file, "r");
	if (act_file_hdl == NULL) {
		vpr_throw(VPR_ERROR_BLIF_F, __FILE__, __LINE__,
				"Error: could not open activity file: %s\n", activity_file);
	}

	ptr = vtr::fgets(buf, vtr::BUFSIZE, act_file_hdl);
	while (ptr != NULL) {
		word1 = strtok(buf, TOKENS);
		word2 = strtok(NULL, TOKENS);
		word3 = strtok(NULL, TOKENS);
		add_activity_to_net2(netlist, atom_net_power, word1, atof(word2), atof(word3));

		ptr = vtr::fgets(buf, vtr::BUFSIZE, act_file_hdl);
	}
	fclose(act_file_hdl);

	/* Make sure all nets have an activity value */
	for (auto net_id : netlist.nets()) {
		if (atom_net_power[net_id].probability < 0.0
				|| atom_net_power[net_id].density < 0.0) {
			vpr_throw(VPR_ERROR_BLIF_F, __FILE__, __LINE__,
					"Error: Activity file does not contain signal %s\n",
					netlist.net_name(net_id).c_str());
		}
	}
    return atom_net_power;
}

bool add_activity_to_net2(const AtomNetlist& netlist, std::unordered_map<AtomNetId,t_net_power>& atom_net_power,
                          char * net_name, float probability, float density) {
    AtomNetId net_id = netlist.find_net(net_name);
    if(net_id) {
        atom_net_power[net_id].probability = probability;
        atom_net_power[net_id].density = density;
        return false;
    }

	printf("Error: net %s found in activity file, but it does not exist in the .blif file.\n",
			net_name);
	return true;
}

/* Read blif file and perform basic sweep/accounting on it
 * - power_opts: Power options, can be NULL
 */
void read_and_process_blif(const char *blif_file,
		bool sweep_hanging_nets_and_inputs, bool absorb_buffer_luts,
        const t_model *user_models,
		const t_model *library_models, bool read_activity_file,
		char * activity_file) {

	/* begin parsing blif input file */
	read_blif2(blif_file, absorb_buffer_luts, sweep_hanging_nets_and_inputs, user_models,
			library_models, read_activity_file, activity_file);
}

