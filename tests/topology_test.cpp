#include "catan/topology.hpp"

#include <gtest/gtest.h>

#include <set>

namespace cae {
namespace {

const Topology& t = kTopology;

TEST(Topology, HexesFollowTheCatanProjectOrder) {
  // Board._generate_hex_grid: q ascending, then r ascending.
  EXPECT_EQ(hex_at(-2, 0), 0);
  EXPECT_EQ(hex_at(-2, 2), 2);
  EXPECT_EQ(hex_at(-1, -1), 3);
  EXPECT_EQ(hex_at(0, 0), 9);
  EXPECT_EQ(hex_at(2, 0), 18);
  EXPECT_EQ(hex_at(3, 0), -1);   // sea
  EXPECT_EQ(hex_at(-1, -2), -1);  // |s| = 3: sea
  for (int h = 0; h < kHexCount; ++h) {
    const Cube c = t.hex_cube[static_cast<std::size_t>(h)];
    EXPECT_EQ(hex_at(c.q, c.r), h);
  }
}

TEST(Topology, EveryCornerTouchesOneToThreeHexes) {
  int incidences = 0;
  int coastal = 0;
  for (int n = 0; n < kNodeCount; ++n) {
    int land = 0;
    for (const auto h : t.node_hexes[static_cast<std::size_t>(n)]) land += h >= 0 ? 1 : 0;
    EXPECT_GE(land, 1) << "node " << n;
    incidences += land;
    coastal += land < 3 ? 1 : 0;
  }
  EXPECT_EQ(incidences, kHexCount * kDirections);  // each hex has six corners
  EXPECT_EQ(coastal, 30);                         // the coast is a ring of 30 corners
}

TEST(Topology, CornerAndSideTablesAgree) {
  int degree_sum = 0;
  for (int n = 0; n < kNodeCount; ++n) {
    const auto i = static_cast<std::size_t>(n);
    const int degree = count(t.node_neighbor_mask[i]);
    EXPECT_TRUE(degree == 2 || degree == 3) << "node " << n;
    EXPECT_EQ(t.node_edge_mask[i].count(), degree);
    degree_sum += degree;
    for (std::size_t k = 0; k < 3; ++k) {
      const int e = t.node_edges[i][k];
      const int other = t.node_neighbors[i][k];
      if (e < 0) {
        EXPECT_EQ(other, -1);
        continue;
      }
      const auto& ends = t.edge_nodes[static_cast<std::size_t>(e)];
      EXPECT_TRUE((ends[0] == n && ends[1] == other) || (ends[1] == n && ends[0] == other));
      // Adjacency is symmetric.
      EXPECT_TRUE(contains(t.node_neighbor_mask[static_cast<std::size_t>(other)], n));
    }
  }
  EXPECT_EQ(degree_sum, 2 * kEdgeCount);
}

TEST(Topology, EverySpellingOfACornerGivesOneIndex) {
  // Corner 0 of a hex is corner 2 of its north-east neighbor and corner 4 of
  // its east neighbor, and so on round the hex (hex_lib.py's lookup table).
  for (int h = 0; h < kHexCount; ++h) {
    const Cube c = t.hex_cube[static_cast<std::size_t>(h)];
    for (int d = 0; d < kDirections; ++d) {
      const int node = node_at(c.q, c.r, d);
      ASSERT_GE(node, 0);
      for (const auto& alias : topology_detail::kCornerAlias[static_cast<std::size_t>(d)]) {
        const Cube n = topology_detail::neighbor(c, alias[0]);
        EXPECT_EQ(node_at(n.q, n.r, alias[1]), node) << "hex " << h << " corner " << d;
      }
      const Cube across = topology_detail::neighbor(c, d);
      EXPECT_EQ(edge_at(across.q, across.r, (d + 3) % 6), edge_at(c.q, c.r, d));
    }
  }
}

TEST(Topology, CanonicalSpellingsRoundTrip) {
  std::set<std::pair<int, int>> seen;
  for (int n = 0; n < kNodeCount; ++n) {
    const Spelling s = t.node_spelling[static_cast<std::size_t>(n)];
    EXPECT_EQ(node_at(s.hex.q, s.hex.r, s.direction), n);
  }
  for (int e = 0; e < kEdgeCount; ++e) {
    const Spelling s = t.edge_spelling[static_cast<std::size_t>(e)];
    EXPECT_EQ(edge_at(s.hex.q, s.hex.r, s.direction), e);
  }
  // A coastal corner may be spelled through a sea hex: the bottom corner (2)
  // of sea hex (1,-3) is the upper left corner (4) of land hex (1,-2) and the
  // upper right one (0) of land hex (0,-2). Its upper right corner (0)
  // touches only sea.
  EXPECT_EQ(node_at(1, -3, 2), node_at(1, -2, 4));
  EXPECT_EQ(node_at(1, -3, 2), node_at(0, -2, 0));
  EXPECT_EQ(node_at(1, -3, 0), -1);
  // The side between two sea hexes is not on the board.
  EXPECT_EQ(edge_at(3, -3, 1), -1);
}

TEST(Topology, OutOfRangeIsNotOnTheBoard) {
  EXPECT_EQ(node_at(0, 0, 6), -1);
  EXPECT_EQ(node_at(0, 0, -1), -1);
  EXPECT_EQ(node_at(9, 0, 0), -1);
  EXPECT_EQ(edge_at(-4, 0, 0), -1);
}

}  // namespace
}  // namespace cae
