#pragma once

// A small, fast random number generator for simulations.
//
// Why not std::mt19937 and std::uniform_int_distribution?
//  - Speed: a Mersenne Twister keeps 2.5 KiB of state and refills it every
//    624 draws. xoshiro256** keeps 32 bytes and needs a few shifts, rotates
//    and one multiply per draw. A simulation that copies Game millions of
//    times also copies its generator, so small state matters.
//  - Reproducibility: the C++ standard fixes what mt19937 produces but not
//    how uniform_int_distribution turns it into a number in a range. GCC and
//    Clang's libraries do it differently, so the same seed gives different
//    games on the two compilers. below() below is ours, so a seed means the
//    same game everywhere, the WebAssembly build included.
//
// xoshiro256** is by David Blackman and Sebastiano Vigna (public domain,
// https://prng.di.unimi.it/). It is not for cryptography; it is for dice.

#include <cstdint>

namespace cae {

class Rng {
 public:
  explicit constexpr Rng(std::uint64_t seed) noexcept {
    // The four words of state must not all be zero, and similar seeds (1, 2,
    // 3...) should give unrelated streams. splitmix64 scrambles the seed into
    // the four words; this is the seeding the xoshiro authors recommend.
    for (auto& word : state_) word = splitmix64(seed);
  }

  // 64 random bits.
  constexpr std::uint64_t next() noexcept {
    const std::uint64_t result = rotl(state_[1] * 5, 7) * 9;
    const std::uint64_t t = state_[1] << 17;
    state_[2] ^= state_[0];
    state_[3] ^= state_[1];
    state_[1] ^= state_[2];
    state_[0] ^= state_[3];
    state_[2] ^= t;
    state_[3] = rotl(state_[3], 45);
    return result;
  }

  // A uniform integer in [0, n), n > 0, without modulo bias.
  //
  // Lemire's method: multiply a random 32-bit x by n as a 64-bit product.
  // The high 32 bits are floor(x * n / 2^32), a number in [0, n). Some
  // results would come up once more often than others when 2^32 is not a
  // multiple of n; the low 32 bits tell when x fell in that small excess, and
  // only then do we draw again. No division on the common path.
  // https://lemire.me/blog/2016/06/30/fast-random-shuffling/
  constexpr int below(int n) noexcept {
    const auto range = static_cast<std::uint32_t>(n);
    std::uint64_t product = (next() >> 32) * range;
    auto low = static_cast<std::uint32_t>(product);
    if (low < range) {
      const std::uint32_t threshold = (0U - range) % range;  // 2^32 mod n
      while (low < threshold) {
        product = (next() >> 32) * range;
        low = static_cast<std::uint32_t>(product);
      }
    }
    return static_cast<int>(product >> 32);
  }

  // True with probability p, like Python's `random() < p`.
  constexpr bool chance(double p) noexcept {
    // The top 53 bits as a double in [0, 1): 53 is the precision of a double.
    return static_cast<double>(next() >> 11) * 0x1.0p-53 < p;
  }

  // One die, 1..6.
  constexpr int die() noexcept { return 1 + below(6); }

 private:
  static constexpr std::uint64_t rotl(std::uint64_t x, int k) noexcept {
    return (x << k) | (x >> (64 - k));
  }
  static constexpr std::uint64_t splitmix64(std::uint64_t& x) noexcept {
    std::uint64_t z = (x += 0x9e3779b97f4a7c15);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9;
    z = (z ^ (z >> 27)) * 0x94d049bb133111eb;
    return z ^ (z >> 31);
  }

  std::uint64_t state_[4]{};
};

}  // namespace cae
