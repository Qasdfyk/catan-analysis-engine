#pragma once

// Sets of board corners ("nodes") stored as the bits of one 64-bit integer.
//
// The standard board has 54 corners, numbered 0..53. Bit i of a NodeMask is 1
// when corner i is in the set. A whole set then fits in one CPU register, and
// set operations become single instructions:
//   union        a | b
//   intersection a & b
//   difference   a & ~b
// This is the representation the rules engine will use for buildings, roads
// and legal placements (step B5).

#include <bit>
#include <cstdint>
#include <string>

// The namespace is `cae` (Catan Analysis Engine), not `catan`: catan() is
// also the C complex arc tangent, which GCC knows as a built-in function, and
// GCC warns about any namespace with that name, even without -Wall.
namespace cae {

using NodeMask = std::uint64_t;

// Number of corners on the standard board. 54 <= 64, so one bit per corner fits.
inline constexpr int kNodeCount = 54;

// The set of all 54 corners: bits 0..53 set, bits 54..63 clear.
inline constexpr NodeMask kAllNodes = (NodeMask{1} << kNodeCount) - 1;

// The set containing only `node`. The 1 must already be 64 bits wide before
// the shift: `1 << 40` would shift a 32-bit int, which is undefined behaviour.
// Precondition: 0 <= node < kNodeCount.
[[nodiscard]] constexpr NodeMask bit(int node) noexcept {
  return NodeMask{1} << node;
}

// How many corners are in the set. std::popcount becomes one POPCNT
// instruction only when the compiler may assume the CPU has it (-mpopcnt,
// -march=x86-64-v2 or newer, -march=native). With the default x86-64 baseline
// it becomes a short sequence of shifts, masks and a multiply instead.
[[nodiscard]] constexpr int count(NodeMask mask) noexcept {
  return std::popcount(mask);
}

// Whether `node` is in the set.
[[nodiscard]] constexpr bool contains(NodeMask mask, int node) noexcept {
  return (mask & bit(node)) != 0;
}

// Calls f(node) for every corner in the set, lowest index first.
//
// The loop runs once per *set* bit, not once per corner: a mask with 3 corners
// takes 3 iterations whatever their indices are.
//   std::countr_zero(mask) = number of zero bits below the lowest 1
//                          = index of the lowest corner in the set.
//   mask &= mask - 1       clears exactly that lowest 1. Subtracting 1 turns
//                          the lowest 1 into 0 and the zeros below it into 1s;
//                          the AND then wipes all of those.
//   Example: 0b10100 - 1 = 0b10011, and 0b10100 & 0b10011 = 0b10000.
// `mask` is taken by value, so the caller's copy is left untouched.
template <typename F>
constexpr void for_each_node(NodeMask mask, F&& f) {
  while (mask != 0) {
    f(std::countr_zero(mask));
    mask &= mask - 1;
  }
}

// Human-readable form for test failures and debugging, e.g. "{0, 5, 53}".
[[nodiscard]] std::string to_string(NodeMask mask);

// Number of edges ("sides" where roads go) on the standard board.
inline constexpr int kEdgeCount = 72;

// A set of edges. 72 bits do not fit one 64-bit word, so the set is two
// words: edges 0..63 in `lo`, edges 64..71 in the low 8 bits of `hi`. The
// upper 56 bits of `hi` are always zero; every operation keeps them zero, so
// == and count() need no masking.
//
// Why not std::bitset<72>? It would work, but its operations are not
// constexpr in C++20 and its layout is up to the library; two plain words are
// as fast and easy to reason about.
struct EdgeMask {
  std::uint64_t lo = 0;
  std::uint64_t hi = 0;

  [[nodiscard]] static constexpr EdgeMask of(int edge) noexcept {
    EdgeMask m;
    m.set(edge);
    return m;
  }
  constexpr void set(int edge) noexcept {
    if (edge < 64) {
      lo |= std::uint64_t{1} << edge;
    } else {
      hi |= std::uint64_t{1} << (edge - 64);
    }
  }
  constexpr void reset(int edge) noexcept {
    if (edge < 64) {
      lo &= ~(std::uint64_t{1} << edge);
    } else {
      hi &= ~(std::uint64_t{1} << (edge - 64));
    }
  }
  [[nodiscard]] constexpr bool test(int edge) const noexcept {
    return edge < 64 ? ((lo >> edge) & 1U) != 0 : ((hi >> (edge - 64)) & 1U) != 0;
  }
  [[nodiscard]] constexpr bool any() const noexcept { return (lo | hi) != 0; }
  [[nodiscard]] constexpr int count() const noexcept {
    return std::popcount(lo) + std::popcount(hi);
  }

  constexpr EdgeMask& operator|=(EdgeMask o) noexcept {
    lo |= o.lo;
    hi |= o.hi;
    return *this;
  }
  constexpr EdgeMask& operator&=(EdgeMask o) noexcept {
    lo &= o.lo;
    hi &= o.hi;
    return *this;
  }
  [[nodiscard]] friend constexpr EdgeMask operator|(EdgeMask a, EdgeMask b) noexcept { return a |= b; }
  [[nodiscard]] friend constexpr EdgeMask operator&(EdgeMask a, EdgeMask b) noexcept { return a &= b; }
  // Complement within the 72 edges: the unused upper bits of `hi` stay zero.
  [[nodiscard]] friend constexpr EdgeMask operator~(EdgeMask a) noexcept {
    return EdgeMask{~a.lo, ~a.hi & ((std::uint64_t{1} << (kEdgeCount - 64)) - 1)};
  }
  friend constexpr bool operator==(EdgeMask, EdgeMask) noexcept = default;
};

// Calls f(edge) for every edge in the set, lowest index first (same trick as
// for_each_node, once per word).
template <typename F>
constexpr void for_each_edge(EdgeMask mask, F&& f) {
  while (mask.lo != 0) {
    f(std::countr_zero(mask.lo));
    mask.lo &= mask.lo - 1;
  }
  while (mask.hi != 0) {
    f(64 + std::countr_zero(mask.hi));
    mask.hi &= mask.hi - 1;
  }
}

}  // namespace cae
