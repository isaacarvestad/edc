#include "gtest/gtest.h"

#include "lib/datastructures/subset_graph.hpp"
#include "lib/datastructures/undirected_graph.hpp"

#include <iostream>

using Graph = Undirected::Graph;

TEST(SubsetGraph, ConstructEmpty) {
  Graph g(0, {});

  EXPECT_EQ(g.size(), 0);
  EXPECT_EQ(g.volume(), 0);
}

/**
   Construct a small graph and verify that all edges and vertices are present.
 */
TEST(SubsetGraph, ConstructSmall) {
  const int n = 10;
  const std::vector<Undirected::Edge> es = {{0, 1}, {0, 2}, {1, 2}, {2, 3},
                                            {3, 4}, {4, 5}, {0, 5}, {6, 7},
                                            {6, 8}, {7, 8}, {7, 9}};
  Graph g(n, es);

  ASSERT_EQ(g.size(), n);
  ASSERT_EQ(g.edgeCount(), int(es.size()));

  std::set<int> vsLeft;
  for (int u = 0; u < n; ++u)
    vsLeft.insert(u);
  for (auto u : g) {
    ASSERT_FALSE(vsLeft.find(u) == vsLeft.end());
    vsLeft.erase(u);
  }
  ASSERT_TRUE(vsLeft.empty());

  std::set<std::pair<int, int>> esLeft;
  for (auto e : es)
    esLeft.insert({e.from, e.to}), esLeft.insert({e.to, e.from});
  for (auto u : g) {
    for (auto e = g.beginEdge(u); e != g.endEdge(u); ++e) {
      ASSERT_FALSE(esLeft.find({e->from, e->to}) == esLeft.end());
      esLeft.erase({e->from, e->to});
    }
  }
  ASSERT_TRUE(esLeft.empty());
}

TEST(SubsetGraph, ConstructComplete) {
  const int n = 100;
  std::vector<Undirected::Edge> es;
  for (int u = 0; u < n; ++u)
    for (int v = u + 1; v < n; ++v)
      es.emplace_back(u, v);
  Graph g(n, es);
  EXPECT_EQ(g.size(), n);
  EXPECT_EQ(g.edgeCount(), n * (n - 1) / 2);
}

/**
   Test that 'reverse' returns the correct edge.
 */
TEST(SubsetGraph, Reverse) {
  const int n = 4;
  const std::vector<Undirected::Edge> es = {{0, 1}, {1, 2}, {0, 2}, {0, 3}};

  Graph g(n, es);

  for (auto u : g) {
    for (auto e = g.beginEdge(u); e != g.endEdge(u); ++e) {
      ASSERT_NE(e->revIdx, -1);
      auto re = g.reverse(*e);
      ASSERT_EQ(e->to, re.from);
      ASSERT_EQ(e->from, re.to);
    }
  }
}

/**
   Test 'connectedComponents' finds all three components in a small graph.
 */
TEST(SubsetGraph, ConnectedComponents) {
  const int n = 10;
  const std::vector<Undirected::Edge> es = {{0, 1}, {0, 2}, {0, 3}, {1, 2},
                                            {4, 5}, {5, 6}, {6, 7}, {7, 8}};
  Graph g(n, es);

  auto comps = g.connectedComponents();
  ASSERT_EQ(int(comps.size()), 3);

  for (auto comp : comps) {
    std::sort(comp.begin(), comp.end());
    if (comp.size() == 1)
      ASSERT_EQ(comp, std::vector<int>({9}));
    else if (comp.size() == 4)
      ASSERT_EQ(comp, std::vector<int>({0, 1, 2, 3}));
    else
      ASSERT_EQ(comp, std::vector<int>({4, 5, 6, 7, 8}));
  }
}

/**
   Remove vertex from graph, test that the graph is now disconnected.
 */
TEST(SubsetGraph, RemoveSingle) {
  const int n = 5;
  const std::vector<Undirected::Edge> es = {{0, 1}, {0, 2}, {1, 2},
                                            {2, 3}, {2, 4}, {3, 4}};
  Graph g(n, es);

  ASSERT_EQ(int(g.connectedComponents().size()), 1);
  g.remove(2);
  ASSERT_EQ(int(g.connectedComponents().size()), 2);

  EXPECT_EQ(g.degree(0), 1);
  EXPECT_EQ(g.degree(1), 1);
  EXPECT_EQ(g.degree(2), 0);
  EXPECT_EQ(g.degree(3), 1);
  EXPECT_EQ(g.degree(4), 1);

  std::set<int> alive(g.begin(), g.end()),
      removed(g.beginRemoved(), g.endRemoved());
  EXPECT_EQ(alive, std::set<int>({0, 1, 3, 4}));
  EXPECT_EQ(removed, std::set<int>({2}));
}

/**
   Remove every other vertex in path.
 */
TEST(SubsetGraph, RemoveSeveralInPath) {
  const int n = 10;
  std::vector<Undirected::Edge> es;
  for (int i = 0; i < n - 1; ++i)
    es.emplace_back(i, i + 1);
  Graph g(n, es);

  EXPECT_EQ(int(g.connectedComponents().size()), 1);
  g.remove(0);
  EXPECT_EQ(int(g.connectedComponents().size()), 1);
  g.remove(2);
  EXPECT_EQ(int(g.connectedComponents().size()), 2);
  g.remove(8);
  EXPECT_EQ(int(g.connectedComponents().size()), 3);
  g.remove(6);
  EXPECT_EQ(int(g.connectedComponents().size()), 4);
  g.remove(4);
  EXPECT_EQ(int(g.connectedComponents().size()), 5);

  std::set<int> alive(g.begin(), g.end()),
      removed(g.beginRemoved(), g.endRemoved());

  EXPECT_EQ(alive, std::set<int>({1, 3, 5, 7, 9}));
  EXPECT_EQ(removed, std::set<int>({0, 2, 4, 6, 8}));

  for (auto u : g)
    EXPECT_EQ(g.degree(u), 0);
}

/**
   Remove vertices from small graph.
 */
TEST(SubsetGraph, RemoveSeveral) {
  const int n = 6;
  const std::vector<Undirected::Edge> es = {{0, 1}, {0, 2}, {1, 2}, {2, 3},
                                            {2, 4}, {3, 4}, {4, 5}};
  Graph g(n, es);

  EXPECT_EQ(int(g.connectedComponents().size()), 1);
  g.remove(0);
  EXPECT_EQ(int(g.connectedComponents().size()), 1);
  g.remove(4);
  EXPECT_EQ(int(g.connectedComponents().size()), 2);
  g.remove(2);
  EXPECT_EQ(int(g.connectedComponents().size()), 3);

  std::set<int> alive(g.begin(), g.end()),
      removed(g.beginRemoved(), g.endRemoved());

  EXPECT_EQ(alive, std::set<int>({1, 3, 5}));
  EXPECT_EQ(removed, std::set<int>({0, 2, 4}));
}

TEST(SubsetGraph, SubgraphEmpty) {
  const int n = 4;
  const std::vector<Undirected::Edge> es = {{0, 1}, {0, 2}, {2, 3}};

  Graph g(n, es);

  std::vector<int> subset;
  g.subgraph(subset.begin(), subset.end());

  EXPECT_EQ(g.size(), 0);
  EXPECT_EQ(g.volume(), 0);
}

TEST(SubsetGraph, SubgraphSimple) {
  const int n = 6;
  const std::vector<Undirected::Edge> es = {{0, 1}, {0, 2}, {1, 2}, {2, 3},
                                            {2, 4}, {3, 4}, {4, 5}};
  Graph g(n, es);

  std::vector<int> subset = {0, 1, 2, 3};
  g.subgraph(subset.begin(), subset.end());

  EXPECT_EQ(g.size(), 4);
  EXPECT_EQ(g.edgeCount(), 4);
  std::set<int> seen;
  for (auto u : g)
    seen.insert(u);
  EXPECT_EQ(seen, std::set<int>(subset.begin(), subset.end()));
}

/**
   Focus on subset twice then restore once.
 */
TEST(SubsetGraph, RestoreSubgraphSimple) {
  const int n = 6;
  const std::vector<Undirected::Edge> es = {{0, 1}, {0, 2}, {1, 2}, {2, 3},
                                            {2, 4}, {3, 4}, {4, 5}};
  Graph g(n, es);

  std::set<int> subset1 = {0, 1, 2, 3};
  g.subgraph(subset1.begin(), subset1.end());

  std::set<int> subset2 = {1, 2};
  g.subgraph(subset2.begin(), subset2.end());

  EXPECT_EQ(g.size(), 2);
  EXPECT_EQ(g.edgeCount(), 1);
  EXPECT_EQ(g.degree(1), 1);
  EXPECT_EQ(g.degree(2), 1);

  g.restoreSubgraph();

  EXPECT_EQ(g.size(), 4);
  EXPECT_EQ(g.edgeCount(), 4);
  EXPECT_EQ(g.degree(0), 2);
  EXPECT_EQ(g.degree(1), 2);
  EXPECT_EQ(g.degree(2), 3);
  EXPECT_EQ(g.degree(3), 1);

  std::set<int> seen;
  for (auto u : g)
    seen.insert(u);
  EXPECT_EQ(seen, subset1);
}

/**
   Remove some vertices, restore removes, verify entire graph is restored.
 */
TEST(SubsetGraph, RestoreRemoves) {
  Graph g(5, {{0, 1}, {0, 2}, {1, 4}, {2, 4}, {3, 4}});
  std::set<std::pair<int, int>> expected;
  for (auto u : g)
    for (auto e = g.cbeginEdge(u); e != g.cendEdge(u); ++e)
      expected.insert({e->from, e->to});

  g.remove(2);
  g.remove(4);
  g.restoreRemoves();

  std::set<std::pair<int, int>> result;
  for (auto u : g)
    for (auto e = g.cbeginEdge(u); e != g.cendEdge(u); ++e)
      result.insert({e->from, e->to});

  EXPECT_EQ(result, expected);
}

TEST(SubsetGraph, SubdivisionVerticesSmall) {
  const int n = 10;
  const std::vector<Undirected::Edge> es = {{0, 1}, {1, 2}, {2, 3}};

  Graph g(n, es);

  {
    std::vector<int> subset = {0};
    auto vs = g.subdivisionVertices(subset.begin(), subset.end());
    std::sort(vs.begin(), vs.end());
    EXPECT_EQ(vs, std::vector<int>({0, 1}));
  }
  {
    std::vector<int> subset = {1};
    auto vs = g.subdivisionVertices(subset.begin(), subset.end());
    std::sort(vs.begin(), vs.end());
    EXPECT_EQ(vs, std::vector<int>({0, 1, 2}));
  }
  {
    std::vector<int> subset = {2};
    auto vs = g.subdivisionVertices(subset.begin(), subset.end());
    std::sort(vs.begin(), vs.end());
    EXPECT_EQ(vs, std::vector<int>({1, 2, 3}));
  }
  {
    std::vector<int> subset = {3};
    auto vs = g.subdivisionVertices(subset.begin(), subset.end());
    std::sort(vs.begin(), vs.end());
    EXPECT_EQ(vs, std::vector<int>({2, 3}));
  }
}

TEST(SubsetGraph, SubdivisionVerticesOnSubgraph) {
  Graph g(5, {{0, 1}, {0, 2}, {0, 3}, {0, 4}});

  std::vector<int> xs = {0, 3, 4};
  g.subgraph(xs.begin(), xs.end());

  std::vector<int> ys = {0};
  auto rs = g.subdivisionVertices(ys.begin(), ys.end());
  std::sort(rs.begin(), rs.end());

  EXPECT_EQ(rs, std::vector<int>({0, 3, 4}));
}
