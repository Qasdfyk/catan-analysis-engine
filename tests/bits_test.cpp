#include "catan/bits.hpp"

#include <gtest/gtest.h>

#include <vector>

namespace cae {
namespace {

std::vector<int> nodes_of(NodeMask mask) {
  std::vector<int> out;
  for_each_node(mask, [&](int node) { out.push_back(node); });
  return out;
}

TEST(Bits, AllNodesHasExactly54LowBits) {
  EXPECT_EQ(count(kAllNodes), kNodeCount);
  EXPECT_EQ(kAllNodes, 0x003F'FFFF'FFFF'FFFFULL);
  EXPECT_FALSE(contains(kAllNodes, 54));  // bits 54..63 must stay clear
  EXPECT_FALSE(contains(kAllNodes, 63));
}

TEST(Bits, BitSetsExactlyOneBit) {
  EXPECT_EQ(bit(0), 1ULL);
  EXPECT_EQ(bit(1), 2ULL);
  EXPECT_EQ(bit(53), 1ULL << 53);
  for (int node = 0; node < kNodeCount; ++node) {
    EXPECT_EQ(count(bit(node)), 1) << "node " << node;
  }
}

TEST(Bits, BitAbove31DoesNotOverflow) {
  // A shift of a 32-bit int would lose these; NodeMask{1} << n must not.
  EXPECT_EQ(bit(32), 0x1'0000'0000ULL);
  EXPECT_NE(bit(40) & kAllNodes, 0ULL);
}

TEST(Bits, CountEmptyAndFull) {
  EXPECT_EQ(count(0), 0);
  EXPECT_EQ(count(bit(0) | bit(53)), 2);
  EXPECT_EQ(count(kAllNodes), 54);
}

TEST(Bits, ContainsEdges) {
  const NodeMask mask = bit(0) | bit(53);
  EXPECT_TRUE(contains(mask, 0));
  EXPECT_TRUE(contains(mask, 53));
  EXPECT_FALSE(contains(mask, 1));
  EXPECT_FALSE(contains(mask, 52));
  EXPECT_FALSE(contains(0, 0));
  EXPECT_FALSE(contains(0, 53));
}

TEST(Bits, ForEachNodeOnEmptyMaskNeverCalls) {
  int calls = 0;
  for_each_node(0, [&](int) { ++calls; });
  EXPECT_EQ(calls, 0);
}

TEST(Bits, ForEachNodeVisitsBit0AndBit53) {
  EXPECT_EQ(nodes_of(bit(0)), std::vector<int>{0});
  EXPECT_EQ(nodes_of(bit(53)), std::vector<int>{53});
  EXPECT_EQ(nodes_of(bit(53) | bit(0)), (std::vector<int>{0, 53}));
}

TEST(Bits, ForEachNodeVisitsAll54InOrder) {
  const std::vector<int> nodes = nodes_of(kAllNodes);
  ASSERT_EQ(nodes.size(), 54U);
  for (int i = 0; i < kNodeCount; ++i) {
    EXPECT_EQ(nodes[static_cast<std::size_t>(i)], i);
  }
}

TEST(Bits, ForEachNodeVisitsEachSetBitOnceAscending) {
  const NodeMask mask = bit(7) | bit(3) | bit(31) | bit(32) | bit(50);
  EXPECT_EQ(nodes_of(mask), (std::vector<int>{3, 7, 31, 32, 50}));
}

TEST(Bits, ForEachNodeAgreesWithContainsAndCount) {
  // Every mask of the form "every k-th corner" for k = 1..54.
  for (int step = 1; step <= kNodeCount; ++step) {
    NodeMask mask = 0;
    for (int node = 0; node < kNodeCount; node += step) {
      mask |= bit(node);
    }
    const std::vector<int> nodes = nodes_of(mask);
    EXPECT_EQ(static_cast<int>(nodes.size()), count(mask)) << "step " << step;
    NodeMask rebuilt = 0;
    for (int node : nodes) {
      EXPECT_TRUE(contains(mask, node));
      rebuilt |= bit(node);
    }
    EXPECT_EQ(rebuilt, mask) << "step " << step;
  }
}

TEST(Bits, WorksAtCompileTime) {
  // constexpr functions can run inside the compiler; static_assert proves it.
  static_assert(count(bit(3) | bit(5)) == 2);
  static_assert(contains(bit(53), 53));
  constexpr int sum = [] {
    int s = 0;
    for_each_node(bit(1) | bit(2) | bit(53), [&](int node) { s += node; });
    return s;
  }();
  static_assert(sum == 56);
  SUCCEED();
}

TEST(Bits, ToString) {
  EXPECT_EQ(to_string(0), "{}");
  EXPECT_EQ(to_string(bit(0)), "{0}");
  EXPECT_EQ(to_string(bit(0) | bit(5) | bit(53)), "{0, 5, 53}");
}

}  // namespace
}  // namespace cae
