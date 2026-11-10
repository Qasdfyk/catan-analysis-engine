#include "catan/bits.hpp"

namespace cae {

std::string to_string(NodeMask mask) {
  std::string out = "{";
  bool first = true;
  for_each_node(mask, [&](int node) {
    if (!first) {
      out += ", ";
    }
    first = false;
    out += std::to_string(node);
  });
  out += "}";
  return out;
}

}  // namespace cae
