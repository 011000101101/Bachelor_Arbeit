#pragma once
#include <algorithm>
#include <iterator>
#include <functional>


#include <boost/iterator/iterator_facade.hpp>

#include "assert.hpp"
#include "memory_pool.hpp"
#include "timing_graph_fwd.hpp"
#include "Time.hpp"

//How many timing tag objects should be flattened into the TimingTags class?
//  A value of 1 tends to help cache locality and performs best
#define NUM_FLAT_TAGS 1

//Identifies the type of a TimingTag
enum class TagType { 
    CLOCK, //This tag corresponds to the clock path
    DATA,  //This tag corresponds to the data path
    UNKOWN //This tag is invalid/default intialized.  Usually indicates an error if encountered in a real design.
};

std::ostream& operator<<(std::ostream& os, const TagType tag_type);

class TimingTag {
    public:
        TimingTag()
            : next_(nullptr)
            , time_(NAN)
            , clock_domain_(INVALID_CLOCK_DOMAIN)
            , launch_node_(-1) 
            , type_(TagType::UNKOWN) {}
        TimingTag(const Time& time_val, DomainId domain, NodeId node, TagType tag_type)
            : next_(nullptr)
            , time_(time_val)
            , clock_domain_(domain)
            , launch_node_(node)
            , type_(tag_type) {}
        TimingTag(const Time& time_val, const TimingTag& base_tag)
            : next_(nullptr)
            , time_(time_val)
            , clock_domain_(base_tag.clock_domain())
            , launch_node_(base_tag.launch_node()) 
            , type_(base_tag.type()) {}

        //Getters
        const Time& time() const { return time_; }
        DomainId clock_domain() const { return clock_domain_; }
        NodeId launch_node() const { return launch_node_; }
        TagType type() const { return type_; }
        TimingTag* next() const { return next_; }

        //Setters
        void set_time(const Time& new_time) { time_ = new_time; };
        void set_clock_domain(const DomainId new_clock_domain) { clock_domain_ = new_clock_domain; };
        void set_launch_node(const NodeId new_launch_node) { launch_node_ = new_launch_node; };
        void set_type(const TagType new_type) { type_ = new_type; }
        void set_next(TimingTag* new_next) { next_ = new_next; }
        void update(const Time& new_time, const TimingTag& base_tag);

    private:
        TimingTag* next_; //Next element in linked list
        Time time_;
        DomainId clock_domain_;
        NodeId launch_node_;
        TagType type_;
};

template <class Value>
class TimingTagIter : public boost::iterator_facade<TimingTagIter<Value>, Value, boost::forward_traversal_tag> {
    private:
        Value* p;
    public:
        TimingTagIter(): p(nullptr) {}
        explicit TimingTagIter(Value* tag): p(tag) {}
    private:
        friend class boost::iterator_core_access;

        void increment() { p = p->next(); }
        bool equal(TimingTagIter<Value> const& other) const { return p == other.p; }
        Value& dereference() const { return *p; }
};

typedef TimingTagIter<TimingTag> TimingTagIterator;
typedef TimingTagIter<TimingTag const> TimingTagConstIterator;

class TimingTags {
    public:
        //Getters
        size_t num_tags() const { return num_tags_; };
        TimingTagIterator find_tag_by_clock_domain(DomainId domain_id);
        TimingTagConstIterator find_tag_by_clock_domain(DomainId domain_id) const;
#if NUM_FLAT_TAGS >= 1
        TimingTagIterator begin() { return (num_tags_ > 0) ? TimingTagIterator(&head_tags_[0]) : end(); };
        TimingTagConstIterator begin() const { return (num_tags_ > 0) ? TimingTagConstIterator(&head_tags_[0]) : end(); };
#else
        TimingTagIterator begin() { return TimingTagIterator(head_tags_); };
        TimingTagConstIterator begin() const { return TimingTagConstIterator(head_tags_); };
#endif
        TimingTagIterator end() { return TimingTagIterator(nullptr); };
        TimingTagConstIterator end() const { return TimingTagConstIterator(nullptr); };

        //Modifiers
        void add_tag(MemoryPool& tag_pool, const Time& new_time, const TimingTag& src_tag);
        void max_tag(MemoryPool& tag_pool, const Time& new_time, const TimingTag& src_tag);
        void min_tag(MemoryPool& tag_pool, const Time& new_time, const TimingTag& src_tag);
        void clear();


    private:
        int num_tags_;

#if NUM_FLAT_TAGS >= 1
        //The first NUM_FLAT_TAGS tags are stored directly as members
        //of this object. Any additional tags are stored in a dynamically
        //allocated linked list.
        //Note that despite being an array, each element of head_tags_ is
        //hooked into the linked list
        std::array<TimingTag, NUM_FLAT_TAGS> head_tags_;
#else
        TimingTag* head_tags_;
#endif
};


