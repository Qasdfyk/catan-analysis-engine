// "Predict, then measure": how many of a player's buildings touch hex h, for
// each of the 19 hexes, computed three ways over the same random positions.
//
//   BM_TouchingLoopBool  buildings in std::array<bool, 54>, loop over 6 corners
//   BM_TouchingLoopBits  buildings in a NodeMask,           loop over 6 corners
//   BM_TouchingPopcount  buildings in a NodeMask, popcount(buildings & hex mask)
//
// One "item" is one (position, hex) answer, so items_per_second compares the
// three directly. BM_ForEachNode shows how iterating a mask scales with the
// number of set bits.

#include "catan/bits.hpp"

#include <benchmark/benchmark.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <random>

namespace {

using cae::NodeMask;

constexpr int kHexCount = 19;
constexpr int kCornersPerHex = 6;

// Size of the input pool. A power of two so `i & (kPoolSize - 1)` wraps the
// index without a division. Large enough that the branch predictor cannot
// learn one fixed input, small enough (8 KiB of masks) to stay in L1 cache,
// so we time the computation and not memory.
constexpr std::size_t kPoolSize = 1024;

using HexCorners = std::array<std::array<int, kCornersPerHex>, kHexCount>;

// SYNTHETIC corner indices: 6 distinct corners in 0..53 per hex, spread so
// that most corners touch 2 hexes, like on the real board. This is NOT the
// real board topology, which comes in step B5. For these benchmarks only the
// shape of the work matters (6 lookups per hex), not which corners they are.
// corner k of hex h = (7h + 9k) mod 54; 9k mod 54 differs for k = 0..5, so
// the 6 corners of a hex are distinct.
constexpr HexCorners make_hex_corners() {
  HexCorners corners{};
  for (int h = 0; h < kHexCount; ++h) {
    for (int k = 0; k < kCornersPerHex; ++k) {
      corners[static_cast<std::size_t>(h)][static_cast<std::size_t>(k)] =
          (7 * h + 9 * k) % cae::kNodeCount;
    }
  }
  return corners;
}

constexpr HexCorners kHexCorners = make_hex_corners();

// The same corners, precomputed once as one NodeMask per hex.
constexpr std::array<NodeMask, kHexCount> make_hex_masks() {
  std::array<NodeMask, kHexCount> masks{};
  for (std::size_t h = 0; h < kHexCount; ++h) {
    for (int corner : kHexCorners[h]) {
      masks[h] |= cae::bit(corner);
    }
  }
  return masks;
}

constexpr std::array<NodeMask, kHexCount> kHexMasks = make_hex_masks();

// A mask with exactly `bits` random corners set. Fixed seed: every run and
// every benchmark sees the same positions.
NodeMask random_mask(std::mt19937_64& rng, int bits) {
  std::uniform_int_distribution<int> pick(0, cae::kNodeCount - 1);
  NodeMask mask = 0;
  while (cae::count(mask) < bits) {
    mask |= cae::bit(pick(rng));
  }
  return mask;
}

// Player positions: 2..9 buildings each (a player starts with 2 settlements
// and can own at most 5 settlements + 4 cities on distinct corners).
const std::array<NodeMask, kPoolSize>& building_masks() {
  static const std::array<NodeMask, kPoolSize> pool = [] {
    std::array<NodeMask, kPoolSize> out{};
    std::mt19937_64 rng(12345);
    std::uniform_int_distribution<int> how_many(2, 9);
    for (NodeMask& mask : out) {
      mask = random_mask(rng, how_many(rng));
    }
    return out;
  }();
  return pool;
}

using BoolBoard = std::array<bool, cae::kNodeCount>;

// The same positions as building_masks(), one bool per corner.
const std::array<BoolBoard, kPoolSize>& building_bools() {
  static const std::array<BoolBoard, kPoolSize> pool = [] {
    std::array<BoolBoard, kPoolSize> out{};
    const auto& masks = building_masks();
    for (std::size_t i = 0; i < kPoolSize; ++i) {
      for (int node = 0; node < cae::kNodeCount; ++node) {
        out[i][static_cast<std::size_t>(node)] = cae::contains(masks[i], node);
      }
    }
    return out;
  }();
  return pool;
}

// The pattern shared by the three Touching benchmarks:
//  - each iteration takes the next position from the pool;
//  - it fills `touching` with 19 answers;
//  - DoNotOptimize(touching) tells the compiler the array is read by someone
//    it cannot see, so it may not skip computing it; ClobberMemory() tells it
//    all memory may have been read/written, so it may not merge or hoist the
//    stores of one iteration into the next.

void BM_TouchingLoopBool(benchmark::State& state) {
  const auto& pool = building_bools();
  std::array<int, kHexCount> touching{};
  std::size_t i = 0;
  for (auto _ : state) {
    const BoolBoard& buildings = pool[i++ & (kPoolSize - 1)];
    for (std::size_t h = 0; h < kHexCount; ++h) {
      int n = 0;
      for (int corner : kHexCorners[h]) {
        n += buildings[static_cast<std::size_t>(corner)] ? 1 : 0;
      }
      touching[h] = n;
    }
    benchmark::DoNotOptimize(touching);
    benchmark::ClobberMemory();
  }
  state.SetItemsProcessed(state.iterations() * kHexCount);
}
BENCHMARK(BM_TouchingLoopBool);

void BM_TouchingLoopBits(benchmark::State& state) {
  const auto& pool = building_masks();
  std::array<int, kHexCount> touching{};
  std::size_t i = 0;
  for (auto _ : state) {
    const NodeMask buildings = pool[i++ & (kPoolSize - 1)];
    for (std::size_t h = 0; h < kHexCount; ++h) {
      int n = 0;
      for (int corner : kHexCorners[h]) {
        n += cae::contains(buildings, corner) ? 1 : 0;
      }
      touching[h] = n;
    }
    benchmark::DoNotOptimize(touching);
    benchmark::ClobberMemory();
  }
  state.SetItemsProcessed(state.iterations() * kHexCount);
}
BENCHMARK(BM_TouchingLoopBits);

void BM_TouchingPopcount(benchmark::State& state) {
  const auto& pool = building_masks();
  std::array<int, kHexCount> touching{};
  std::size_t i = 0;
  for (auto _ : state) {
    const NodeMask buildings = pool[i++ & (kPoolSize - 1)];
    for (std::size_t h = 0; h < kHexCount; ++h) {
      touching[h] = cae::count(buildings & kHexMasks[h]);
    }
    benchmark::DoNotOptimize(touching);
    benchmark::ClobberMemory();
  }
  state.SetItemsProcessed(state.iterations() * kHexCount);
}
BENCHMARK(BM_TouchingPopcount);

// Visits every set bit of a mask with for_each_node. The argument is the
// number of bits set; the work done per visit is a trivial sum.
void BM_ForEachNode(benchmark::State& state) {
  const int bits = static_cast<int>(state.range(0));
  std::array<NodeMask, kPoolSize> pool{};
  std::mt19937_64 rng(67890);
  for (NodeMask& mask : pool) {
    mask = random_mask(rng, bits);
  }
  std::size_t i = 0;
  for (auto _ : state) {
    int sum = 0;
    cae::for_each_node(pool[i++ & (kPoolSize - 1)], [&](int node) { sum += node; });
    benchmark::DoNotOptimize(sum);
  }
  state.SetItemsProcessed(state.iterations());
  state.counters["bits_set"] = static_cast<double>(bits);
}
BENCHMARK(BM_ForEachNode)->Arg(0)->Arg(4)->Arg(16)->Arg(54);

}  // namespace
