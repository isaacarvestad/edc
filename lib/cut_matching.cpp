#include <algorithm>
#include <cmath>
#include <glog/logging.h>
#include <glog/stl_logging.h>
#include <numeric>
#include <random>

#include "absl/container/flat_hash_set.h"
#include "cut_matching.hpp"

namespace CutMatching {

Result::Result() : type(Result::Type::Expander), iterations(0), congestion(1) {}

Solver::Solver(UnitFlow::Graph *g, UnitFlow::Graph *subdivG,
               std::mt19937 *randomGen, std::vector<int> *subdivisionIdx,
               std::vector<int> *fromSubdivisionIdx, double phi,
               Parameters params)
    : graph(g), subdivGraph(subdivG), randomGen(randomGen),
      subdivisionIdx(subdivisionIdx), fromSubdivisionIdx(fromSubdivisionIdx),
      phi(phi),
      T(std::max(1, params.tConst + int(params.tFactor *square(
                                        std::log10(graph->edgeCount()))))),
      numSplitNodes(subdivGraph->size() - graph->size()) {
  assert(graph->size() != 0 && "Cut-matching expected non-empty subset.");

  // Set edge capacities in subdivision flow graph.
  const UnitFlow::Flow capacity = std::ceil(1.0 / phi / T);
  for (auto u : *graph)
    for (auto e = subdivGraph->beginEdge(u); e != subdivGraph->endEdge(u); ++e)
      e->capacity = capacity, subdivGraph->reverse(*e).capacity = capacity,
      e->congestion = 0, subdivGraph->reverse(*e).congestion = 0;

  // If potential is sampled, set the flow matrix to the identity matrix.
  if (params.samplePotential) {
    flowMatrix.resize(subdivGraph->size());
    for (int u : *subdivGraph)
      flowMatrix[u].resize(subdivGraph->size());

    for (int i = 0; i < subdivGraph->size(); ++i)
      flowMatrix[i][i] = 1.0;
  }

  // Give each 'm' subdivision vertex a unique index in the range '[0,m)'.
  int count = 0;
  for (auto u : *subdivGraph) {
    if ((*subdivisionIdx)[u] >= 0) {
      (*subdivisionIdx)[u] = count++;
      (*fromSubdivisionIdx)[(*subdivisionIdx)[u]] = u;
    }
  }
}

/**
   Given a number of matchings 'M_i' and a start state, compute the flow
   projection in place.

   Assumes no pairs of vertices in single round overlap.

   Time complexity: O(|rounds| * |start|)
 */
void Solver::projectFlow(const std::vector<Matching> &rounds,
                         std::vector<double> &start) {
  for (auto it = rounds.begin(); it != rounds.end(); ++it) {
    for (auto [i, j] : *it) {
      assert(i >= 0 && "Given vertex is not a subdivision vertex.");
      assert(j >= 0 && "Given vertex is not a subdivision vertex.");
      start[i] = 0.5 * (start[i] + start[j]);
      start[j] = start[i];
    }
  }
}

std::vector<double> Solver::randomUnitVector() {
  std::vector<double> result(numSplitNodes);
  std::uniform_int_distribution distribution(0, 1);

  int count = 0;
  for (auto it = subdivGraph->cbegin(); it != subdivGraph->cend(); ++it) {
    const auto u = (*subdivisionIdx)[*it];
    if (u >= 0) {
      count++;
      result[u] = (distribution(*randomGen) == 0 ? -1 : 1);
    }
  }
  for (auto it = subdivGraph->cbegin(); it != subdivGraph->cend(); ++it) {
    const auto u = (*subdivisionIdx)[*it];
    if (u >= 0) {
      result[u] /= double(count);
    }
  }

  return result;
}

double Solver::samplePotential() const {
  // Subdivision vertices remaining.
  std::vector<int> alive;
  for (auto it = subdivGraph->cbegin(); it != subdivGraph->cend(); ++it) {
    const auto u = (*subdivisionIdx)[*it];
    if (u >= 0)
      alive.push_back(u);
  }

  long double result = 0;
  std::vector<long double> avgFlowVector(numSplitNodes);

  for (int u : alive)
    for (int v : alive)
      avgFlowVector[v] += flowMatrix[u][v];
  for (auto &f : avgFlowVector)
    f /= (long double)alive.size();

  for (int u : alive)
    for (int v : alive)
      result += square(flowMatrix[u][v] - avgFlowVector[v]);

  return (double)result;
}

std::pair<std::vector<int>, std::vector<int>>
Solver::proposeCut(const std::vector<double> &flow,
                   const Parameters &params) const {
  const int curSubdivisionCount = subdivGraph->size() - graph->size();
  double avgFlow = 0.0;
  for (auto u : *subdivGraph) {
    const int idx = (*subdivisionIdx)[u];
    if (idx >= 0)
      avgFlow += flow[idx];
  }
  avgFlow /= (double)curSubdivisionCount;

  // Partition subdivision vertices into a left and right set.
  std::vector<int> axLeft, axRight;
  for (auto u : *subdivGraph) {
    const int idx = (*subdivisionIdx)[u];
    if (idx >= 0) {
      if (flow[idx] < avgFlow)
        axLeft.push_back(u);
      else
        axRight.push_back(u);
    }
  }
  bool leftLarger = axLeft.size() > axRight.size();
  if (leftLarger)
    std::swap(axLeft, axRight);

  // Compute potentials
  double totalPotential = 0.0, leftPotential = 0.0;
  for (auto u : *subdivGraph) {
    const int idx = (*subdivisionIdx)[u];
    if (idx >= 0)
      totalPotential += square(flow[idx] - avgFlow);
  }
  for (auto u : axLeft) {
    const int idx = (*subdivisionIdx)[u];
    assert(idx >= 0);
    leftPotential += square(flow[idx] - avgFlow);
  }

  // Sort by flow
  auto cmpFlow = [&flow, &subdivisionIdx = subdivisionIdx](int u, int v) {
    return flow[(*subdivisionIdx)[u]] < flow[(*subdivisionIdx)[v]];
  };
  std::sort(axLeft.begin(), axLeft.end(), cmpFlow);
  std::sort(axRight.begin(), axRight.end(), cmpFlow);

  // If left side is empty due to floating point precision, divide axRight in
  // half. Otherwise consider the two cases from Lemma 3.3 in RST.
  if (axLeft.empty()) {
    while (axLeft.size() < axRight.size())
      axLeft.push_back(axRight.back()), axRight.pop_back();
    if (axLeft.size() > axRight.size())
      std::swap(axLeft, axRight);
  } else if (leftPotential > totalPotential / 20.0) {
    // If left side was not larger, remove smallest flow values instead of
    // largest from 'axRight'.
    if (!leftLarger)
      std::reverse(axRight.begin(), axRight.end());
  } else {
    double l = 0.0;
    for (auto u : axLeft) {
      const int idx = (*subdivisionIdx)[u];
      assert(idx >= 0);
      l += std::abs(flow[idx] - avgFlow);
    }
    const double mu = avgFlow + 4.0 * l / (double)curSubdivisionCount;

    // Re-partition along '\mu'.
    axLeft.clear(), axRight.clear();
    for (auto u : *subdivGraph) {
      const int idx = (*subdivisionIdx)[u];
      if (idx >= 0) {
        if (flow[idx] < mu)
          axRight.push_back(u);
        else if (flow[idx] >= avgFlow + 6.0 * l / (double)curSubdivisionCount)
          axLeft.push_back(u);
      }
    }
    std::reverse(axRight.begin(), axRight.end());
  }

  assert(!axLeft.empty() && "Left side of cut cannot be empty.");
  if (params.balancedCutStrategy) {
    while (!axRight.empty() && axRight.size() > axLeft.size())
      axRight.pop_back();
    assert(axLeft.size() == axRight.size() &&
           "Proposed cut should be perfectly balanced.");
  } else {
    // If left side was larger, remove smallest flow values instead of largest
    // from 'axLeft'.
    if (leftLarger)
      std::reverse(axLeft.begin(), axLeft.end());
    while ((int)axLeft.size() * 8 > curSubdivisionCount)
      axLeft.pop_back();
    assert(!axLeft.size() <= axRight.size() &&
           "Left side of cut can't be larger.");
  }

  return std::make_pair(axLeft, axRight);
}

Result Solver::compute(Parameters params) {
  if (numSplitNodes <= 1) {
    VLOG(3) << "Cut matching exited early with " << numSplitNodes
            << " subdivision vertices.";
    return Result{};
  }

  const int lowerVolumeBalance = numSplitNodes / 10 / T;
  const int targetVolumeBalance = std::max(
      lowerVolumeBalance, int(params.minBalance * subdivGraph->globalVolume()));

  std::vector<Matching> rounds;
  Result result;

  auto flow = randomUnitVector();

  int iterations = 0;
  for (; iterations < T &&
         subdivGraph->globalVolume(subdivGraph->cbeginRemoved(),
                                   subdivGraph->cendRemoved()) <=
             targetVolumeBalance;
       ++iterations) {
    VLOG(3) << "Iteration " << iterations << " out of " << T << ".";

    if (params.samplePotential) {
      VLOG(4) << "Sampling potential function";
      result.sampledPotentials.push_back(samplePotential());
      VLOG(4) << "Finished sampling potential function";
    }

    if (params.resampleUnitVector) {
      flow = randomUnitVector();

      for (int i = 0; i < params.randomWalkSteps; ++i)
        projectFlow(rounds, flow);
    }

    auto [axLeft, axRight] = proposeCut(flow, params);

    VLOG(3) << "Number of sources: " << axLeft.size()
            << " sinks: " << axRight.size();

    subdivGraph->reset();
    for (const auto u : axLeft)
      subdivGraph->addSource(u, 1);
    for (const auto u : axRight)
      subdivGraph->addSink(u, 1);

    const int h = std::max((int)round(1.0 / phi / std::log10(numSplitNodes)),
                           int(std::ceil(std::log10(numSplitNodes))));
    VLOG(3) << "Computing flow with |S| = " << axLeft.size()
            << " |T| = " << axRight.size() << " and max height " << h << ".";
    const auto hasExcess = subdivGraph->compute(h);

    absl::flat_hash_set<int> removed;
    if (hasExcess.empty()) {
      VLOG(3) << "\tAll flow routed.";
    } else {
      VLOG(3) << "\tHas " << hasExcess.size()
              << " vertices with excess. Computing level cut.";
      const auto levelCut = subdivGraph->levelCut(h);
      VLOG(3) << "\tHas level cut with " << levelCut.size() << " vertices.";
      for (auto u : levelCut)
        removed.insert(u);
    }

    VLOG(3) << "\tRemoving " << removed.size() << " vertices.";

    auto isRemoved = [&removed](int u) {
      return removed.find(u) != removed.end();
    };
    axLeft.erase(std::remove_if(axLeft.begin(), axLeft.end(), isRemoved),
                 axLeft.end());
    axRight.erase(std::remove_if(axRight.begin(), axRight.end(), isRemoved),
                  axRight.end());

    for (auto u : removed) {
      if ((*subdivisionIdx)[u] == -1)
        graph->remove(u);
      subdivGraph->remove(u);
    }

    std::vector<int> zeroDegrees;
    for (auto it = subdivGraph->cbegin(); it != subdivGraph->cend(); ++it)
      if (subdivGraph->degree(*it) == 0)
        zeroDegrees.push_back(*it), removed.insert(*it);
    for (auto u : zeroDegrees) {
      if ((*subdivisionIdx)[u] == -1)
        graph->remove(u);
      subdivGraph->remove(u);
    }

    if (params.resampleUnitVector) {
      auto removeCond = [&isRemoved, &fromSubdivisionIdx = fromSubdivisionIdx](
                            std::pair<int, int> p) {
        return isRemoved((*fromSubdivisionIdx)[p.first]) ||
               isRemoved((*fromSubdivisionIdx)[p.second]);
      };
      for (auto &matchings : rounds)
        matchings.erase(
            std::remove_if(matchings.begin(), matchings.end(), removeCond),
            matchings.end());
    }

    VLOG(3) << "Computing matching with |S| = " << axLeft.size()
            << " |T| = " << axRight.size() << ".";
    auto matching =
        subdivGraph->matching(axLeft, UnitFlow::Graph::MatchingMethod::Dfs);
    for (auto &p : matching) {
      int u = (*subdivisionIdx)[p.first];
      int v = (*subdivisionIdx)[p.second];

      flow[u] = 0.5 * (flow[u] + flow[v]);
      flow[v] = flow[u];

      if (params.samplePotential) {
        for (int i : *subdivGraph) {
          int w = (*subdivisionIdx)[i];
          if (w >= 0) {
            flowMatrix[u][w] = 0.5 * (flowMatrix[u][w] + flowMatrix[v][w]);
            flowMatrix[v][w] = flowMatrix[u][w];
          }
        }
      }
    }
    VLOG(3) << "Found matching of size " << matching.size() << ".";

    assert(matching.size() == axLeft.size() &&
           "Expected all source vertices to be matched.");

    if (params.resampleUnitVector) {
      for (auto &p : matching) {
        p.first = (*subdivisionIdx)[p.first];
        p.second = (*subdivisionIdx)[p.second];
      }

      rounds.push_back(matching);
    }
  }

  result.iterations = iterations;
  result.congestion = 1;
  for (auto u : *subdivGraph)
    for (auto e = subdivGraph->beginEdge(u); e != subdivGraph->endEdge(u); ++e)
      result.congestion =
          std::max(result.congestion, e->congestion * iterations);

  if (params.samplePotential) {
    VLOG(4) << "Final sampling of potential function";
    result.sampledPotentials.push_back(samplePotential());
    VLOG(4) << "Finished final sampling of potential function";
  }

  if (graph->size() != 0 && graph->removedSize() != 0 &&
      subdivGraph->globalVolume(subdivGraph->cbeginRemoved(),
                                subdivGraph->cendRemoved()) >
          lowerVolumeBalance)
    // We have: graph.volume(R) > m / (10 * T)
    result.type = Result::Balanced;
  else if (graph->removedSize() == 0)
    result.type = Result::Expander;
  else if (graph->size() == 0)
    graph->restoreRemoves(), result.type = Result::Expander;
  else
    result.type = Result::NearExpander;

  switch (result.type) {
  case Result::Balanced: {
    VLOG(2) << "Cut matching ran " << iterations
            << " iterations and resulted in balanced cut with size ("
            << graph->size() << ", " << graph->removedSize() << ") and volume ("
            << graph->globalVolume(graph->cbegin(), graph->cend()) << ", "
            << graph->globalVolume(graph->cbeginRemoved(), graph->cendRemoved())
            << ").";
    break;
  }
  case Result::Expander: {
    VLOG(2) << "Cut matching ran " << iterations
            << " iterations and resulted in expander.";
    break;
  }
  case Result::NearExpander: {
    VLOG(2) << "Cut matching ran " << iterations
            << " iterations and resulted in near expander of size "
            << graph->size() << ".";
    break;
  }
  }

  return result;
}
} // namespace CutMatching
