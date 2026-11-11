#pragma once

// The fixed geometry of the standard board: 19 hexes, 54 corners ("nodes")
// and 72 sides ("edges"), each numbered once, with every adjacency the rules
// need stored as a small table or a bitmask.
//
// Positions in game records use the cube coordinates of catan-project
// (backend/app/models/hex_lib.py): a hex is (q, r, s) with q + r + s = 0, the
// board is pointy-topped, and a corner or a side is a hex plus a direction
// 0..5. A corner belongs to up to three hexes, so it has up to three
// spellings; node_at() and edge_at() accept every one of them. The rules
// engine never sees coordinates again: inside it a corner is just 0..53.
//
// The tables are computed by the compiler (constexpr), so they cost nothing
// at run time and a mistake in them can fail the build (see the
// static_asserts at the end).

#include "catan/bits.hpp"

#include <array>
#include <cstdint>

namespace cae {

inline constexpr int kHexCount = 19;
inline constexpr int kDirections = 6;

// Hex directions, clockwise from east, as in hex_lib.py:
// 0 east, 1 south-east, 2 south-west, 3 west, 4 north-west, 5 north-east.
// Corner d of a hex lies between neighbors d-1 and d (0 = upper right, then
// clockwise); side d is shared with neighbor d and runs from corner d to d+1.
struct Cube {
  int q = 0;
  int r = 0;
  [[nodiscard]] constexpr int s() const noexcept { return -q - r; }
  friend constexpr bool operator==(Cube, Cube) noexcept = default;
};

// A corner or a side written as (hex, direction).
struct Spelling {
  Cube hex;
  int direction = 0;
};

// Hexes that are land: the center and two rings around it. The lookup
// tables also cover the third ring (sea), because records may spell a
// coastal corner through a sea hex.
inline constexpr int kLandRadius = 2;
inline constexpr int kTableRadius = 3;
inline constexpr int kTableSide = 2 * kTableRadius + 1;

struct Topology {
  // Hex i sits at hex_cube[i]. The order is that of Board._generate_hex_grid
  // in catan-project: q ascending, then r ascending.
  std::array<Cube, kHexCount> hex_cube{};
  // Corner d / side d of hex i.
  std::array<std::array<std::int8_t, kDirections>, kHexCount> hex_nodes{};
  std::array<std::array<std::int8_t, kDirections>, kHexCount> hex_edges{};
  // The six corners of hex i as a set: who touches a hex is then
  // popcount(buildings & hex_node_mask[i]).
  std::array<NodeMask, kHexCount> hex_node_mask{};

  // The three hexes around a corner, -1 for sea, in the order hex_lib.py
  // lists them for the canonical spelling (owner, neighbor d, neighbor d-1).
  std::array<std::array<std::int8_t, 3>, kNodeCount> node_hexes{};
  // The two or three sides at a corner, and the corners at their other ends.
  // Unused slots are -1 (a coastal corner has only two sides on the board).
  std::array<std::array<std::int8_t, 3>, kNodeCount> node_edges{};
  std::array<std::array<std::int8_t, 3>, kNodeCount> node_neighbors{};
  std::array<NodeMask, kNodeCount> node_neighbor_mask{};
  std::array<EdgeMask, kNodeCount> node_edge_mask{};
  // The canonical spelling: of the equivalent (hex, direction) pairs, the one
  // whose hex is smallest by (q, r, s). Records are written this way.
  std::array<Spelling, kNodeCount> node_spelling{};

  std::array<std::array<std::int8_t, 2>, kEdgeCount> edge_nodes{};
  std::array<Spelling, kEdgeCount> edge_spelling{};

  // Lookups from coordinates; index ((q + 3) * 7 + (r + 3)) * 6 + direction.
  std::array<std::int8_t, kTableSide * kTableSide> hex_table{};
  std::array<std::int8_t, kTableSide * kTableSide * kDirections> node_table{};
  std::array<std::int8_t, kTableSide * kTableSide * kDirections> edge_table{};
};

namespace topology_detail {

inline constexpr std::array<Cube, kDirections> kNeighbor = {
    Cube{1, 0}, Cube{0, 1}, Cube{-1, 1}, Cube{-1, 0}, Cube{0, -1}, Cube{1, -1}};

// Corner d of a hex is also corner kCornerAlias[d][k].second of its neighbor
// in direction kCornerAlias[d][k].first (the lookup table of hex_lib.py).
// Example: the upper right corner (0) is the bottom corner (2) of the
// north-east neighbor (5) and the upper left corner (4) of the east one (0).
inline constexpr std::array<std::array<std::array<int, 2>, 2>, kDirections> kCornerAlias = {{
    {{{5, 2}, {0, 4}}},
    {{{0, 3}, {1, 5}}},
    {{{1, 4}, {2, 0}}},
    {{{2, 5}, {3, 1}}},
    {{{3, 0}, {4, 2}}},
    {{{4, 1}, {5, 3}}},
}};

// The third side at corner d, the one pointing away from the hex: side
// kSpoke[d].second of the neighbor in direction kSpoke[d].first.
inline constexpr std::array<std::array<int, 2>, kDirections> kSpoke = {
    {{5, 1}, {0, 2}, {1, 3}, {2, 4}, {3, 5}, {4, 0}}};

constexpr Cube neighbor(Cube h, int d) noexcept {
  return Cube{h.q + kNeighbor[static_cast<std::size_t>(d)].q,
              h.r + kNeighbor[static_cast<std::size_t>(d)].r};
}

constexpr int abs(int x) noexcept { return x < 0 ? -x : x; }

constexpr int distance_from_center(Cube h) noexcept {
  const int a = abs(h.q);
  const int b = abs(h.r);
  const int c = abs(h.s());
  return a > b ? (a > c ? a : c) : (b > c ? b : c);
}

constexpr bool in_table(Cube h) noexcept { return distance_from_center(h) <= kTableRadius; }

constexpr std::size_t hex_slot(Cube h) noexcept {
  return static_cast<std::size_t>((h.q + kTableRadius) * kTableSide + (h.r + kTableRadius));
}

constexpr std::size_t slot(Cube h, int d) noexcept {
  return hex_slot(h) * kDirections + static_cast<std::size_t>(d);
}

// (q, r, s) in lexicographic order, as Python compares the tuples.
constexpr bool less(Cube a, Cube b) noexcept {
  if (a.q != b.q) return a.q < b.q;
  if (a.r != b.r) return a.r < b.r;
  return a.s() < b.s();
}

constexpr std::array<Spelling, 3> corner_spellings(Cube h, int d) noexcept {
  const auto& alias = kCornerAlias[static_cast<std::size_t>(d)];
  return {Spelling{h, d}, Spelling{neighbor(h, alias[0][0]), alias[0][1]},
          Spelling{neighbor(h, alias[1][0]), alias[1][1]}};
}

constexpr std::array<Spelling, 2> side_spellings(Cube h, int d) noexcept {
  return {Spelling{h, d}, Spelling{neighbor(h, d), (d + 3) % kDirections}};
}

constexpr Topology build() {
  Topology t{};
  t.hex_table.fill(-1);
  t.node_table.fill(-1);
  t.edge_table.fill(-1);

  int hexes = 0;
  for (int q = -kLandRadius; q <= kLandRadius; ++q) {
    for (int r = -kLandRadius; r <= kLandRadius; ++r) {
      const Cube h{q, r};
      if (distance_from_center(h) > kLandRadius) continue;
      t.hex_cube[static_cast<std::size_t>(hexes)] = h;
      t.hex_table[hex_slot(h)] = static_cast<std::int8_t>(hexes);
      ++hexes;
    }
  }

  // Number corners and sides in order of first sight: hex 0 directions 0..5,
  // then hex 1, ... Each new one writes all its spellings into the table.
  int nodes = 0;
  int edges = 0;
  for (std::size_t i = 0; i < kHexCount; ++i) {
    const Cube h = t.hex_cube[i];
    for (int d = 0; d < kDirections; ++d) {
      if (t.node_table[slot(h, d)] < 0) {
        const auto spellings = corner_spellings(h, d);
        Spelling canonical = spellings[0];
        for (const Spelling& s : spellings) {
          if (in_table(s.hex)) t.node_table[slot(s.hex, s.direction)] = static_cast<std::int8_t>(nodes);
          if (less(s.hex, canonical.hex)) canonical = s;
        }
        t.node_spelling[static_cast<std::size_t>(nodes)] = canonical;
        ++nodes;
      }
      if (t.edge_table[slot(h, d)] < 0) {
        const auto spellings = side_spellings(h, d);
        Spelling canonical = spellings[0];
        for (const Spelling& s : spellings) {
          if (in_table(s.hex)) t.edge_table[slot(s.hex, s.direction)] = static_cast<std::int8_t>(edges);
          if (less(s.hex, canonical.hex)) canonical = s;
        }
        t.edge_spelling[static_cast<std::size_t>(edges)] = canonical;
        ++edges;
      }
      t.hex_nodes[i][static_cast<std::size_t>(d)] = t.node_table[slot(h, d)];
      t.hex_edges[i][static_cast<std::size_t>(d)] = t.edge_table[slot(h, d)];
      t.hex_node_mask[i] |= bit(t.node_table[slot(h, d)]);
    }
  }

  const auto land_hex = [&t](Cube h) -> std::int8_t {
    return in_table(h) ? t.hex_table[hex_slot(h)] : std::int8_t{-1};
  };
  const auto side_at = [&t](Cube h, int d) -> std::int8_t {
    return in_table(h) ? t.edge_table[slot(h, d)] : std::int8_t{-1};
  };

  for (std::size_t e = 0; e < kEdgeCount; ++e) {
    const Spelling s = t.edge_spelling[e];
    t.edge_nodes[e][0] = t.node_table[slot(s.hex, s.direction)];
    t.edge_nodes[e][1] = t.node_table[slot(s.hex, (s.direction + 1) % kDirections)];
  }

  for (std::size_t n = 0; n < kNodeCount; ++n) {
    const Spelling s = t.node_spelling[n];
    const int d = s.direction;
    t.node_hexes[n] = {land_hex(s.hex), land_hex(neighbor(s.hex, d)),
                       land_hex(neighbor(s.hex, (d + 5) % kDirections))};
    const auto& spoke = kSpoke[static_cast<std::size_t>(d)];
    const std::array<std::int8_t, 3> sides = {side_at(s.hex, d),
                                              side_at(s.hex, (d + 5) % kDirections),
                                              side_at(neighbor(s.hex, spoke[0]), spoke[1])};
    std::size_t k = 0;
    t.node_edges[n] = {-1, -1, -1};
    t.node_neighbors[n] = {-1, -1, -1};
    for (const std::int8_t e : sides) {
      if (e < 0) continue;
      const auto& ends = t.edge_nodes[static_cast<std::size_t>(e)];
      const std::int8_t other = ends[0] == static_cast<std::int8_t>(n) ? ends[1] : ends[0];
      t.node_edges[n][k] = e;
      t.node_neighbors[n][k] = other;
      t.node_edge_mask[n].set(e);
      t.node_neighbor_mask[n] |= bit(other);
      ++k;
    }
  }
  return t;
}

}  // namespace topology_detail

inline constexpr Topology kTopology = topology_detail::build();

// Hex index 0..18 at (q, r), or -1 when that is not a land hex.
[[nodiscard]] constexpr int hex_at(int q, int r) noexcept {
  const Cube h{q, r};
  if (topology_detail::distance_from_center(h) > kTableRadius) return -1;
  return kTopology.hex_table[topology_detail::hex_slot(h)];
}

// Corner index 0..53 for any spelling of a corner, or -1 when the corner does
// not touch land or the input is out of range.
[[nodiscard]] constexpr int node_at(int q, int r, int direction) noexcept {
  const Cube h{q, r};
  if (direction < 0 || direction >= kDirections) return -1;
  if (topology_detail::distance_from_center(h) > kTableRadius) return -1;
  return kTopology.node_table[topology_detail::slot(h, direction)];
}

// Side index 0..71 for any spelling of a side, or -1 when it is not on the board.
[[nodiscard]] constexpr int edge_at(int q, int r, int direction) noexcept {
  const Cube h{q, r};
  if (direction < 0 || direction >= kDirections) return -1;
  if (topology_detail::distance_from_center(h) > kTableRadius) return -1;
  return kTopology.edge_table[topology_detail::slot(h, direction)];
}

// The standard board, checked by the compiler.
static_assert(count(kTopology.hex_node_mask[9]) == 6, "the center hex has six corners");
static_assert(hex_at(0, 0) == 9, "hex order follows catan-project: the center is the 10th hex");
static_assert(node_at(2, 0, 3) == node_at(1, 0, 1), "a shared corner has one index");
static_assert(edge_at(0, 0, 0) == edge_at(1, 0, 3), "a shared side has one index");

}  // namespace cae
