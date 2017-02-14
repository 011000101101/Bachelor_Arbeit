#ifndef VPR_TIMING_INFO_H
#define VPR_TIMING_INFO_H
#include <memory>

#include "timing_info_fwd.h"
#include "analyzer_factory.hpp"
#include "timing_util.h"

//Create a SetupTimingInfo for the given delay calculator
template<class DelayCalc>
std::unique_ptr<SetupTimingInfo> make_setup_timing_info(std::shared_ptr<DelayCalc> delay_calculator);

//Create a HoldTimingInfo for the given delay calculator
template<class DelayCalc>
std::unique_ptr<HoldTimingInfo> make_hold_timing_info(std::shared_ptr<DelayCalc> delay_calculator);

//Create a SetupHoldTimingInfo for the given delay calculator
template<class DelayCalc>
std::unique_ptr<SetupHoldTimingInfo> make_setup_hold_timing_info(std::shared_ptr<DelayCalc> delay_calculator);


//Generic inteface which provides functionality to update (but not
//access) timing information.  
//
//This is useful for algorithms which know they need to update timing 
//information (e.g. because they have made a change to the implementation) 
//but do not care *what* timing information is updated (e.g. setup vs hold)
class TimingInfo {
    public:
        //Constructors
        virtual ~TimingInfo() {}
    public:
        //Mutators

        //Update all timing information
        virtual void update() = 0;

        //Return the underlying timing analyzer
        virtual std::shared_ptr<const tatum::TimingAnalyzer> analyzer() const = 0;

        //Return the underlying timing graph
        //virtual std::shared_ptr<const tatum::TimingGraph> timing_graph() const = 0;

        //Return the underlying timing constraints
        //virtual std::shared_ptr<const tatum::TimingConstraints> timing_constraints() const = 0;
};

//Generic interface which provides setup-related timing information
//
//This is useful for algorithms which need to access setup timing related
//information (e.g. to optimize critical path delay)
class SetupTimingInfo : public virtual TimingInfo {
    public:
        //Accessors

        //Return the critical path with the least slack
        virtual PathInfo least_slack_critical_path() const = 0;

        //Return the critical path the the longest absolute delay
        virtual PathInfo longest_critical_path() const = 0;

        //Return the set of critical paths between all clock domain pairs
        virtual std::vector<PathInfo> critical_paths() const = 0;


        //Return the total negative slack w.r.t. setup constraints
        virtual float setup_total_negative_slack() const = 0;

        //Return the worst negative slack w.r.t. setup constraints
        virtual float setup_worst_negative_slack() const = 0;


        //Return the worst setup slack of all paths passing through pin
        virtual float setup_pin_slack(AtomPinId pin) const = 0;

        //Return the setup criticality of the worst connection passing through pin
        virtual float setup_pin_criticality(AtomPinId pin) const  = 0;


        //Return the underlying timing analyzer
        virtual std::shared_ptr<const tatum::SetupTimingAnalyzer> setup_analyzer() const = 0;
    public:
        //Mutators
        virtual void update_setup() = 0;
};

//Generic interface which provides setup-related timing information
//
//This is useful for algorithms which need to access hold timing related
//information (e.g. to fix hold timing)
class HoldTimingInfo : public virtual TimingInfo {
    public:
        //Accessors

        //Return the total negative slack w.r.t. hold constraints
        virtual float hold_total_negative_slack() const = 0;

        //Return the worst negative slack w.r.t. hold constraints
        virtual float hold_worst_negative_slack() const = 0;

        //Return the worst slack of all paths passing through pin
        virtual float hold_pin_slack(AtomPinId pin) const = 0;

        //Return the hold criticality of the worst connection passing through pin
        virtual float hold_pin_criticality(AtomPinId pin) const = 0;

        //Return the underlying timing analyzer
        virtual std::shared_ptr<const tatum::HoldTimingAnalyzer> hold_analyzer() const = 0;
    public:
        //Mutators
        virtual void update_hold() = 0;
};

//Generic interface which provides both setup and hold related timing information
//
//This is useful for algorithms which require access to both setup and hold timing
//information (e.g. simulatneously optimizing setup and hold)
//
//This class supports both the SetupTimingInfo and HoldTimingInfo interfaces and 
//can be used in place of them in any algorithm requiring setup or hold related 
//information.
//
//Implementation Note:
//  This class uses multiple inheritence, which is OK in this case for the following reasons:
//      * The inheritance is virtual avoiding the diamon problem (i.e. there is only 
//        one base TimingInfo class instance)
//      * Both SetupTimingInfo and HoldTimingInfo are purely abstract classes so there 
//        is no data to be duplicated
class SetupHoldTimingInfo : public SetupTimingInfo, public HoldTimingInfo {
    public:
        virtual std::shared_ptr<const tatum::SetupHoldTimingAnalyzer> setup_hold_analyzer() const = 0;

};

#include "concrete_timing_info.h"

template<class DelayCalc>
std::unique_ptr<SetupTimingInfo> make_setup_timing_info(std::shared_ptr<DelayCalc> delay_calculator) {

    std::shared_ptr<tatum::SetupTimingAnalyzer> analyzer = tatum::AnalyzerFactory<tatum::SetupAnalysis>::make(*g_timing_graph, *g_timing_constraints, *delay_calculator);

     return std::unique_ptr<ConcreteSetupTimingInfo<DelayCalc>>(
             new ConcreteSetupTimingInfo<DelayCalc>(*g_timing_graph, *g_timing_constraints, delay_calculator, analyzer)
            );
}

template<class DelayCalc>
std::unique_ptr<HoldTimingInfo> make_hold_timing_info(std::shared_ptr<DelayCalc> delay_calculator) {
    std::shared_ptr<tatum::HoldTimingAnalyzer> analyzer = tatum::AnalyzerFactory<tatum::HoldAnalysis>::make(*g_timing_graph, *g_timing_constraints, *delay_calculator);

     return std::unique_ptr<ConcreteHoldTimingInfo<DelayCalc>>(
             new ConcreteHoldTimingInfo<DelayCalc>(*g_timing_graph, *g_timing_constraints, delay_calculator, analyzer)
            );
}

template<class DelayCalc>
std::unique_ptr<SetupHoldTimingInfo> make_setup_hold_timing_info(std::shared_ptr<DelayCalc> delay_calculator) {
    std::shared_ptr<tatum::SetupHoldTimingAnalyzer> analyzer = tatum::AnalyzerFactory<tatum::SetupHoldAnalysis>::make(*g_timing_graph, *g_timing_constraints, *delay_calculator);

     return std::unique_ptr<ConcreteSetupHoldTimingInfo<DelayCalc>>(
             new ConcreteSetupHoldTimingInfo<DelayCalc>(*g_timing_graph, *g_timing_constraints, delay_calculator, analyzer)
            );
}

#endif
