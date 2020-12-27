#include <algorithm>
#include <cmath>
#include <glog/logging.h>
#include <glog/stl_logging.h>
#include <numeric>
#include <random>

#include "absl/container/flat_hash_set.h"
#include "cut_matching.hpp"

namespace CutMatching {

Solver::Solver(UnitFlow::Graph *g, UnitFlow::Graph *subdivGraph,
               const double phi, const int tConst, const double tFactor)
    : graph(g), subdivGraph(subdivGraph), phi(phi),
      T(tConst + std::ceil(tFactor * std::log10(graph->edgeCount()) *
                           std::log10(graph->edgeCount()))) {
  assert(graph->size() != 0 && "Cut-matching expected non-empty subset.");

  std::random_device rd;
  // randomGen = std::mt19937(0);
  randomGen = std::mt19937(rd());

  const UnitFlow::Flow capacity = std::ceil(1.0 / phi / T);
  for (auto u : *graph)
    for (auto e = subdivGraph->beginEdge(u); e != subdivGraph->endEdge(u); ++e)
      e->capacity = capacity, subdivGraph->reverse(*e).capacity = capacity;
}

/**
   Given a number of matchings 'M_i' and a start state, compute the flow
   projection.

   Assumes no pairs of vertices in single round overlap.

   Time complexity: O(|rounds| + |start|)
 */
using Matching = std::vector<std::pair<int, int>>;
std::vector<double> projectFlow(const std::vector<Matching> &rounds,
                                const std::vector<int> &fromSplitNode,
                                std::vector<double> start) {
  for (auto it = rounds.begin(); it != rounds.end(); ++it) {
    for (const auto &[u, v] : *it) {
      int i = fromSplitNode[u], j = fromSplitNode[v];
      assert(i >= 0 && "Given vertex is not a subdivision vertex.");
      assert(j >= 0 && "Given vertex is not a subdivision vertex.");
      start[i] = 0.5 * (start[i] + start[j]);
      start[j] = start[i];
    }
  }

  return start;
}

/**
   Fill a vector of size 'n' with random data such that it is orthogonal to the
   all ones vector.
 */
std::vector<double> randomUnitVectorFast(std::mt19937 &gen, int n) {
  std::vector<double> xs(n);
  for (int i = 0; i < n / 2; ++i)
    xs[i] = -1;
  for (int i = n / 2; i < n; ++i)
    xs[i] = 1;
  if (n % 2 != 0)
    xs[0] = -2;
  std::shuffle(xs.begin(), xs.end(), gen);
  return xs;
}

/**
   Construct a random unit vector by sampling from normal distribution. Based
   on https://stackoverflow.com/a/8453514
 */
std::vector<double> randomUnitVector(std::mt19937 &gen, int n) {
  std::normal_distribution<> d(0, 1);
  std::vector<double> xs(n);
  double m = 0;
  for (auto &x : xs) {
    x = d(gen);
    m += x * x;
  }
  m = std::sqrt(m);
  for (auto &x : xs)
    x /= m;
  return xs;
}

template <typename It>
double potential(const double avgFlow, const std::vector<double> &flow,
                 const std::vector<int> &fromSplitNode, It begin, It end) {
  double p = 0;
  for (auto it = begin; it != end; it++) {
    double f = flow[fromSplitNode[*it]];
    p += (f - avgFlow) * (f - avgFlow);
  }
  return p;
}

ResultType Solver::compute() {
  std::vector<Matching> rounds;

  const int numSplitNodes = subdivGraph->size() - graph->size();

  if (numSplitNodes <= 1) {
    VLOG(3) << "Cut matching exited early with " << numSplitNodes
            << " subdivision vertices.";
    return Expander;
  }

  {
    int count = 0;
    for (auto u : *subdivGraph)
      if (subdivGraph->isSubdivision(u))
        subdivGraph->setSubdivision(u, count++);
  }

  const int goodBalance = 0.45 * subdivGraph->globalVolume(),
            minBalance = numSplitNodes / (10 * T);

  int iterations = 1;
  for (; iterations <= T &&
         subdivGraph->globalVolume(subdivGraph->cbeginRemoved(),
                                   subdivGraph->cendRemoved()) <= goodBalance;
       ++iterations) {
    VLOG(3) << "Iteration " << iterations << " out of " << T << ".";

    auto flow = projectFlow(rounds, subdivGraph->getSubdivisionVector(),
                            randomUnitVectorFast(randomGen, numSplitNodes));
    double avgFlow =
        std::accumulate(flow.begin(), flow.end(), 0.0) / (double)flow.size();

    std::vector<int> axLeft, axRight;
    for (auto u : *subdivGraph) {
      if (subdivGraph->isSubdivision(u)) {
        if (flow[subdivGraph->getSubdivision(u)] < avgFlow)
          axLeft.push_back(u);
        else
          axRight.push_back(u);
      }
    }
    // TODO: Is this what w.l.o.g in RST Lemma 3.3 refers to?
    if (axLeft.size() > axRight.size()) {
      axLeft.clear(), axRight.clear();
      for (auto &f : flow)
        f *= -1;
      avgFlow *= -1;
      for (auto u : *subdivGraph) {
        if (subdivGraph->isSubdivision(u)) {
          if (flow[subdivGraph->getSubdivision(u)] < avgFlow)
            axLeft.push_back(u);
          else
            axRight.push_back(u);
        }
      }
    }

    std::vector<int> tmpSubdiv;
    for (auto u : *subdivGraph)
      if (subdivGraph->isSubdivision(u))
        tmpSubdiv.push_back(u);
    double pAll = potential(avgFlow, flow, subdivGraph->getSubdivisionVector(),
                            tmpSubdiv.begin(), tmpSubdiv.end()),
           pLeft = potential(avgFlow, flow, subdivGraph->getSubdivisionVector(),
                             axLeft.begin(), axLeft.end());

    if (pLeft >= pAll / 20.0) {
      sort(axLeft.begin(), axLeft.end(),
           [&flow, &subdivGraph = subdivGraph](int u, int v) {
             return flow[subdivGraph->getSubdivision(u)] <
                    flow[subdivGraph->getSubdivision(v)];
           });
      while (8 * axLeft.size() > tmpSubdiv.size())
        axLeft.pop_back();
    } else {
      double leftL = 0;
      for (auto u : axLeft)
        leftL += std::abs(flow[subdivGraph->getSubdivision(u)] - avgFlow);
      double rightL = 0;
      for (auto u : axRight)
        rightL += std::abs(flow[subdivGraph->getSubdivision(u)] - avgFlow);
      assert(std::abs(leftL - rightL) < 1e-9 &&
             "Left and right sums should be equal.");
      const double l = leftL;
      const double mu = avgFlow + 4.0 * l / tmpSubdiv.size();

      axRight.clear();
      for (auto u : *subdivGraph)
        if (subdivGraph->isSubdivision(u))
          if (flow[subdivGraph->getSubdivision(u)] <= mu)
            axRight.push_back(u);

      axLeft.clear();
      for (auto u : *subdivGraph)
        if (subdivGraph->isSubdivision(u))
          if (flow[subdivGraph->getSubdivision(u)] >=
              avgFlow + 6.0 * l / tmpSubdiv.size())
            axLeft.push_back(u);
      sort(axLeft.begin(), axLeft.end(),
           [&flow, &subdivGraph = subdivGraph](int u, int v) {
             return flow[subdivGraph->getSubdivision(u)] >
                    flow[subdivGraph->getSubdivision(v)];
           });
      while (8 * axLeft.size() > tmpSubdiv.size())
        axLeft.pop_back();
    }

    subdivGraph->reset();

    for (const auto u : axLeft)
      subdivGraph->addSource(u, 1);
    for (const auto u : axRight)
      subdivGraph->addSink(u, 1);

    const int h = std::max((int)round(1.0 / phi / std::log10(numSplitNodes)),
                           (int)std::log10(numSplitNodes));
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
      if (!subdivGraph->isSubdivision(u))
        graph->remove(u);
      subdivGraph->remove(u);
    }
    std::vector<int> zeroDegrees;
    for (auto it = subdivGraph->cbegin(); it != subdivGraph->cend(); ++it)
      if (subdivGraph->degree(*it) == 0)
        zeroDegrees.push_back(*it);
    for (auto u : zeroDegrees) {
      if (!subdivGraph->isSubdivision(u))
        graph->remove(u);
      subdivGraph->remove(u);
    }

    VLOG(3) << "Computing matching with |S| = " << axLeft.size()
            << " |T| = " << axRight.size() << ".";
    auto matching = subdivGraph->matching(axLeft);
    rounds.push_back(matching);
    VLOG(3) << "Found matching of size " << matching.size() << ".";
  }

  ResultType resultType;

  if (graph->size() != 0 && graph->removedSize() != 0 &&
      subdivGraph->globalVolume(subdivGraph->cbeginRemoved(),
                                subdivGraph->cendRemoved()) > minBalance)
    // We have: graph.volume(R) > m / (10 * T)
    resultType = Balanced;
  else if (graph->removedSize() == 0)
    resultType = Expander;
  else if (graph->size() == 0)
    graph->restoreRemoves(), resultType = Expander;
  else
    resultType = NearExpander;

  switch (resultType) {
  case Balanced: {
    VLOG(2) << "Cut matching ran " << iterations
            << " iterations and resulted in balanced cut with size ("
            << graph->size() << ", " << graph->removedSize() << ") and volume ("
            << graph->globalVolume(graph->cbegin(), graph->cend()) << ", "
            << graph->globalVolume(graph->cbeginRemoved(), graph->cendRemoved())
            << ").";
    break;
  }
  case Expander: {
    VLOG(2) << "Cut matching ran " << iterations
            << " iterations and resulted in expander.";
    break;
  }
  case NearExpander: {
    VLOG(2) << "Cut matching ran " << iterations
            << " iterations and resulted in near expander of size "
            << graph->size() << ".";
    break;
  }
  }

  return resultType;
}
} // namespace CutMatching
