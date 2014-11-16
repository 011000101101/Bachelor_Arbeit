#ifndef TIMINGEDGE_HPP
#define TIMINGEDGE_HPP
#include <vector>

#include "Time.hpp"

class TimingEdge {
    public:
        int to_node_id() const {return to_node_;}
        void set_to_node_id(int node) {to_node_ = node;}

        int from_node_id() const {return from_node_;}
        void set_from_node_id(int node) {from_node_ = node;}

        Time delay() const {return delay_;}
        void set_delay(Time delay_val) {delay_ = delay_val;}

    private:
        int to_node_;
        int from_node_;
        Time delay_;
};

#endif
