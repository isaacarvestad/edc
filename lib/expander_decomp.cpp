#include <glog/logging.h>
#include <glog/stl_logging.h>
#include <memory>
#include <numeric>

#include "cut_matching.hpp"
#include "expander_decomp.hpp"
#include "trimming.hpp"

namespace ExpanderDecomposition {

std::unique_ptr<UnitFlow::Graph>
constructFlowGraph(const std::unique_ptr<Undirected::Graph> &g) {
  auto f = std::make_unique<UnitFlow::Graph>(g->size());

  for (UnitFlow::Vertex u = 0; u < g->size(); ++u)
    for (const auto &e : g->edges(u))
      if (e->from < e->to)
        f->addEdge(e->from, e->to, 0);

  return f;
}

std::unique_ptr<UnitFlow::Graph>
constructSubdivisionFlowGraph(const std::unique_ptr<Undirected::Graph> &g) {
  auto f = std::make_unique<UnitFlow::Graph>(g->size() + g->edgeCount());

  for (UnitFlow::Vertex u = 0; u < g->size(); ++u)
    for (const auto &e : g->edges(u))
      if (e->from < e->to) {
        UnitFlow::Vertex splitVertex = g->size() + f->edgeCount() / 2;
        f->addEdge(e->from, splitVertex, 0);
        f->addEdge(e->to, splitVertex, 0);
      }

  return f;
}

void Solver::compute(const std::vector<int> &xs, int partition) {
  VLOG(1) << "Attempting to find balanced cut for partition " << partition
          << " (" << xs.size() << " vertices).";
  if (xs.empty()) {
    VLOG(2) << "Exiting early, partition " << partition << " was empty.";
    return;
  }

  CutMatching::Solver cm(flowGraph.get(), subdivisionFlowGraph.get(), xs, phi);
  auto result = cm.compute();

  switch (result.t) {
  case CutMatching::Balanced: {
    assert(!result.a.empty() && "Cut should be balanced but A was empty.");
    assert(!result.r.empty() && "Cut should be balanced but R was empty.");
    int newPartition = flowGraph->newPartition(result.a, xs);
    compute(result.a, newPartition);
    compute(result.r, partition);
    break;
  }
  case CutMatching::NearExpander: {
    Trimming::Solver trimming(flowGraph.get(), result.a, phi, partition);
    const auto trimmingResult = trimming.compute();
    result.r.insert(result.r.end(), trimmingResult.r.begin(),
                    trimmingResult.r.end());
    if (result.r.size() > 0 && result.r.size() < xs.size()) {
      int newPartition = flowGraph->newPartition(result.r, xs);
      compute(result.r, newPartition);
    }
    break;
  }
  case CutMatching::Expander: {
    break;
  }
  }
}

Solver::Solver(std::unique_ptr<Undirected::Graph> graph, const double phi)
    : flowGraph(nullptr), subdivisionFlowGraph(nullptr), phi(phi) {
  flowGraph = constructFlowGraph(graph);
  subdivisionFlowGraph = constructSubdivisionFlowGraph(graph);

  VLOG(1) << "Preparing to run expander decomposition."
          << "\n\tGraph: " << graph->size() << " vertices and "
          << graph->edgeCount() << " edges."
          << "\n\tFlow graph: " << flowGraph->size() << " vertices and "
          << flowGraph->edgeCount() << " edges."
          << "\n\tSubdivision graph: " << subdivisionFlowGraph->size()
          << " vertices and " << subdivisionFlowGraph->edgeCount() << " edges.";

  std::vector<int> vertices(graph->size());
  std::iota(vertices.begin(), vertices.end(), 0);
  compute(vertices, 0);
}

std::vector<std::vector<int>> Solver::getPartition() const {
  std::vector<std::vector<int>> result(flowGraph->partitionCount());
  for (int u = 0; u < flowGraph->size(); ++u)
    result[flowGraph->getPartition(u)].push_back(u);
  return result;
}

} // namespace ExpanderDecomposition
