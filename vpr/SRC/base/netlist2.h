#ifndef NETLIST2_H
#define NETLIST2_H
#include <vector>
#include <unordered_map>

#include "vtr_range.h"
#include "vtr_hash.h"
#include "vtr_logic.h"
#include "vtr_strong_id.h"

#include "logic_types.h" //For t_model

//Type tags for Ids
struct atom_blk_id_tag;
struct atom_net_id_tag;
struct atom_port_id_tag;
struct atom_pin_id_tag;

typedef vtr::StrongId<atom_blk_id_tag> AtomBlockId;
typedef vtr::StrongId<atom_net_id_tag> AtomNetId;
typedef vtr::StrongId<atom_port_id_tag> AtomPortId;
typedef vtr::StrongId<atom_pin_id_tag> AtomPinId;

enum class AtomPortType {
    INPUT,
    OUTPUT,
    CLOCK
};

enum class AtomPinType {
    DRIVER,
    SINK
};

enum class AtomBlockType {
    INPAD,
    OUTPAD,
    COMBINATIONAL,
    SEQUENTIAL
};

namespace std {
    //Make tuples hashable for std::unordered_map
    template<>
    struct hash<std::tuple<AtomPortId,size_t>> {
        std::size_t operator()(const std::tuple<AtomPortId,size_t>& k) const {
            std::size_t seed = 0;
            vtr::hash_combine(seed, std::hash<AtomPortId>()(get<0>(k)));
            vtr::hash_combine(seed, std::hash<size_t>()(get<1>(k)));
            return seed;
        }
    };
    template<>
    struct hash<std::tuple<std::string,AtomPortType>> {
        typedef std::underlying_type<AtomPortType>::type enum_type;
        std::size_t operator()(const std::tuple<std::string,AtomPortType>& k) const {
            std::size_t seed = 0;
            vtr::hash_combine(seed, std::hash<std::string>()(get<0>(k)));
            vtr::hash_combine(seed, std::hash<enum_type>()(static_cast<enum_type>(get<1>(k))));
            return seed;
        }
    };
    template<>
    struct hash<std::tuple<AtomBlockId,std::string>> {
        std::size_t operator()(const std::tuple<AtomBlockId,std::string>& k) const {
            std::size_t seed = 0;
            vtr::hash_combine(seed, std::hash<AtomBlockId>()(get<0>(k)));
            vtr::hash_combine(seed, std::hash<std::string>()(get<1>(k)));
            return seed;
        }
    };
} //namespace std

class AtomNetlist {
    public: //Public types
        typedef std::vector<AtomBlockId>::const_iterator block_iterator;
        typedef std::vector<AtomPortId>::const_iterator port_iterator;
        typedef std::vector<AtomPinId>::const_iterator pin_iterator;
        typedef std::vector<AtomNetId>::const_iterator net_iterator;
        typedef std::vector<std::vector<vtr::LogicValue>> TruthTable;
    public:
        AtomNetlist(std::string name);

    public: //Public Accessors
        //Netlist
        const std::string&  netlist_name() const;
        bool                is_blackbox() const;

        //Block
        const std::string&          block_name          (const AtomBlockId id) const;
        AtomBlockType               block_type          (const AtomBlockId id) const;
        const t_model*              block_model         (const AtomBlockId id) const;
        const TruthTable&           block_truth_table   (const AtomBlockId id) const; 
        vtr::Range<port_iterator>   block_input_ports   (const AtomBlockId id) const;
        vtr::Range<port_iterator>   block_output_ports  (const AtomBlockId id) const;
        vtr::Range<port_iterator>   block_clock_ports   (const AtomBlockId id) const;
        AtomPinId                   block_pin           (const AtomPortId port_id, size_t port_bit) const;

        //Port
        const std::string&          port_name   (const AtomPortId id) const;
        AtomBlockId                 port_block  (const AtomPortId id) const; 
        AtomPortType                port_type   (const AtomPortId id) const; 
        vtr::Range<pin_iterator>    port_pins   (const AtomPortId id) const;

        //Pin
        AtomNetId           pin_net     (const AtomPinId id) const; 
        AtomPinType         pin_type    (const AtomPinId id) const; 
        AtomPortId          pin_port    (const AtomPinId id) const;
        size_t              pin_port_bit(const AtomPinId id) const;
        AtomBlockId         pin_block   (const AtomPinId id) const;

        //Net
        const std::string&          net_name    (const AtomNetId id) const; 
        vtr::Range<pin_iterator>    net_pins    (const AtomNetId id) const;
        AtomPinId                   net_driver  (const AtomNetId id) const;
        vtr::Range<pin_iterator>    net_sinks   (const AtomNetId id) const;

        //Aggregates
        vtr::Range<block_iterator>  blocks  () const;
        vtr::Range<net_iterator>    nets    () const;
        
        //Lookups
        AtomBlockId find_block  (const std::string& name) const;
        AtomPortId  find_port   (const AtomBlockId blk_id, const std::string& name) const;
        AtomPinId   find_pin    (const AtomPortId port_id, size_t port_bit) const;
        AtomNetId   find_net    (const std::string& name) const;

        //Sanity check for internal consistency
        bool verify() const;

    public: //Public Mutators
        //Note: all create_*() functions will silently return the appropriate ID if it has already been created
        AtomBlockId create_block(const std::string name, const AtomBlockType blk_type, const t_model* model, const TruthTable truth_table=TruthTable());
        AtomPortId  create_port (const AtomBlockId blk_id, const std::string& name);
        AtomPinId   create_pin  (const AtomPortId port_id, size_t port_bit, const AtomNetId net_id, const AtomPinType type);
        AtomNetId   create_net  (const std::string name);

        void set_blackbox(bool val);
    
    private: //Private types
        struct atom_port_common_id_tag;
        typedef vtr::StrongId<atom_port_common_id_tag> AtomPortCommonId;

    private: //Private members
        //Lookups
        AtomPortCommonId find_port_common_id(const std::string& name, const AtomPortType type) const;
        AtomPortCommonId find_port_common_id(const AtomPortId id) const;

        //Mutators
        AtomPortCommonId create_port_common(const std::string& name, const AtomPortType type);

        //Sanity Checks
        bool valid_block_id(AtomBlockId id) const;
        bool valid_port_id(AtomPortId id) const;
        bool valid_port_bit(AtomPortId id, size_t port_bit) const;
        bool valid_pin_id(AtomPinId id) const;
        bool valid_net_id(AtomNetId id) const;

    private: //Private data

        //Netlist data
        std::string                 netlist_name_;   //Name of the top-level netlist
        bool                        is_blackbox_;    //Indicates this netlist is a black box

        //Block data
        std::vector<AtomBlockId>             block_ids_;      //Valid block ids
        std::vector<std::string>             block_names_;    //Name of each block
        std::vector<AtomBlockType>           block_types_;    //Type of each block
        std::vector<const t_model*>          block_models_;   //Architecture model of each block
        std::vector<TruthTable>              block_truth_tables_; //Truth tables of each block
        std::vector<std::vector<AtomPortId>> block_input_ports_; //Input ports of each block
        std::vector<std::vector<AtomPortId>> block_output_ports_; //Output ports of each block
        std::vector<std::vector<AtomPortId>> block_clock_ports_; //Clock ports of each block

        //Port data
        std::vector<AtomPortId>             port_ids_;          //Valid port ids
        std::vector<AtomBlockId>            port_blocks_;       //Block associated with each port (indexed by AtomPortId)
        std::vector<std::vector<AtomPinId>> port_pins_;         //Pins associated with each port (indexed by AtomPortId)
        std::vector<std::string>            port_common_names_; //Port names (indexed by AtomPortCommonId)
        std::vector<AtomPortType>           port_common_types_; //Type of each port (indexed by AtomPortCommonId)
        std::vector<AtomPortCommonId>       port_common_ids_;   //Since ports have duplicate data we use another 'common' id 
                                                                // to look-up the shared info (indexed by AtomPortId)

        //Pin data
        std::vector<AtomPinId>      pin_ids_;        //Valid pin ids
        std::vector<AtomPortId>     pin_ports_;      //Type of each pin
        std::vector<size_t>         pin_port_bits_;  //The ports bit position in the port
        //std::vector<AtomPinType>    pin_types_;      //Type of each pin
        std::vector<AtomNetId>      pin_nets_;       //Net associated with each pin

        //Net data
        std::vector<AtomNetId>              net_ids_;    //Valid net ids
        std::vector<std::string>            net_names_;  //Name of each net
        std::vector<std::vector<AtomPinId>> net_pins_;   //Pins associated with each net

    private: //Fast lookups

        std::unordered_map<std::string,AtomBlockId> block_name_to_block_id_;
        std::unordered_map<std::tuple<AtomBlockId,std::string>,AtomPortId> block_id_port_name_to_port_id_;
        std::unordered_map<std::tuple<AtomPortId,size_t>,AtomPinId> pin_port_port_bit_to_pin_id_;
        std::unordered_map<std::string,AtomNetId> net_name_to_net_id_;
        std::unordered_map<std::tuple<std::string,AtomPortType>,AtomPortCommonId> port_name_type_to_common_id_;
};

void print_netlist(FILE* f, const AtomNetlist& netlist);

#endif
