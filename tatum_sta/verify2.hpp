#ifndef VERIFY2_HPP
#define VERIFY2_HPP
#include <memory>

#include "TimingAnalyzer.hpp"
#include "TimingGraph.hpp"
#include "golden_reference.hpp"

size_t verify_analyzer2(const tatum::TimingGraph& tg, std::shared_ptr<tatum::TimingAnalyzer> analyzer, GoldenReference& gr);

#endif
