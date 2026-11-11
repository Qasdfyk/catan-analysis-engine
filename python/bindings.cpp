// The engine as a Python module (cae_py), for tools/difftest.py.
//
// The interface is deliberately thin and string-based: events go in as JSON
// text, state comes out as JSON text. The difference test compares whole
// documents, so passing JSON costs nothing in clarity and keeps this file
// free of per-field bindings that would need updating with every new field.

#include "catan/record.hpp"

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <string>

namespace py = pybind11;
using nlohmann::json;

namespace {

class Engine {
 public:
  explicit Engine(const std::string& record_json) : game_(cae::game_from_record(json::parse(record_json))) {}

  // "" when accepted, otherwise the reason (cae::error_name).
  std::string apply(const std::string& event_json) {
    const cae::Error e = cae::apply_event(game_, json::parse(event_json));
    return e == cae::Error::kNone ? std::string() : std::string(cae::error_name(e));
  }

  // Test hook: sets a hand outside the rules, as the difference test does on
  // the Python side too, to reach states random play rarely reaches (an
  // empty bank).
  void set_hand(int seat, const std::array<int, cae::kResourceCount>& cards) {
    game_.players.at(static_cast<std::size_t>(seat)).resources = cards;
  }

  [[nodiscard]] std::string snapshot() const { return cae::snapshot(game_).dump(); }
  [[nodiscard]] std::string legal(int seat) const { return cae::legal_moves(game_, seat).dump(); }

 private:
  cae::Game game_;
};

}  // namespace

PYBIND11_MODULE(cae_py, m) {
  m.doc() = "catan-analysis-engine rules, for differential testing against catan-project";

  // C++ exceptions crossing into Python become Python exceptions.
  py::register_exception<cae::RecordError>(m, "RecordError", PyExc_ValueError);

  py::class_<Engine>(m, "Engine")
      .def(py::init<const std::string&>(), py::arg("record_json"))
      .def("apply", &Engine::apply, py::arg("event_json"))
      .def("set_hand", &Engine::set_hand, py::arg("seat"), py::arg("cards"))
      .def("snapshot", &Engine::snapshot)
      .def("legal", &Engine::legal, py::arg("seat"));

  // Position lookups, so the Python side can translate its own coordinates
  // into the engine's indices. -1 means "not on the board".
  m.def("hex_at", [](int q, int r) { return cae::hex_at(q, r); });
  m.def("node_at", [](int q, int r, int d) { return cae::node_at(q, r, d); });
  m.def("edge_at", [](int q, int r, int d) { return cae::edge_at(q, r, d); });
  m.def("node_neighbors", [](int node) {
    py::list out;
    for (const auto n : cae::kTopology.node_neighbors.at(static_cast<std::size_t>(node))) {
      if (n >= 0) out.append(static_cast<int>(n));
    }
    return out;
  });
  m.def("edge_nodes", [](int edge) {
    const auto& ends = cae::kTopology.edge_nodes.at(static_cast<std::size_t>(edge));
    return py::make_tuple(static_cast<int>(ends[0]), static_cast<int>(ends[1]));
  });
}
