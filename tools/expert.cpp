// catan_expert: the searching bot as the "expert" level of catan-project
// (include/catan/expert.hpp).
//
// As a server, for the Go bots of the platform: one request per line on
// stdin, one answer per line on stdout, in the order they finish (match them
// by id). Requests are answered concurrently, each on --threads threads.
//
//   {"id": 7, "seat": 2, "record": {...}}
//   {"id": 7, "seat": 2, "record": {...}, "time_ms": 500, "iterations": 800}
//
//   {"id": 7, "action": "build_road", "payload": {...}, "searched": true, "playouts": 1532, "ms": 1001.4}
//   {"id": 7, "action": null, "ms": 0.3}      the seat has no move now
//   {"id": 7, "error": "..."}                 a request it could not read
//
// By hand, one move of a recorded game (the record cut where you want):
//
//   build/release/tools/catan_expert --record tests/data/zoo-game-00088.json --seat 1
//
// Playouts seat MAP-Elites from --elites (default data/elites.json) as the
// opponents. --time-ms 0 searches exactly --iterations playouts, so the
// answer depends only on the request and --seed.

#include "catan/elites.hpp"
#include "catan/expert.hpp"
#include "catan/record.hpp"

#include <nlohmann/json.hpp>

#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

using json = nlohmann::json;

[[noreturn]] void usage(const char* argv0) {
  std::fprintf(stderr,
               "usage: %s [--elites PATH] [--threads N] [--iterations N] [--time-ms MS] [--seed S] [--c C]\n"
               "          [--record RECORD.json --seat S]\n",
               argv0);
  std::exit(2);
}

json answer(const json& request, const std::vector<cae::ZooBot>& pool, cae::ExpertSettings settings,
            std::uint64_t seed) {
  json out = {{"id", request.value("id", json())}};
  try {
    if (!request.contains("seat") || !request["seat"].is_number_integer()) throw std::runtime_error("no seat");
    if (!request.contains("record")) throw std::runtime_error("no record");
    if (request.contains("time_ms")) settings.time_limit = std::chrono::milliseconds(request["time_ms"].get<int>());
    if (request.contains("iterations")) settings.search.iterations = request["iterations"].get<int>();
    const cae::TablePosition position = cae::table_from_record(request["record"]);
    const cae::ExpertMove move = cae::expert_move(position, request["seat"].get<int>(), pool, settings, seed);
    if (move.action) {
      const json event = cae::event_from_action(*move.action);
      out["action"] = event["action"];
      out["payload"] = event["payload"];
      out["searched"] = move.searched;
      if (move.searched) out["playouts"] = move.search.iterations;
    } else {
      out["action"] = nullptr;
    }
    out["ms"] = move.milliseconds;
  } catch (const std::exception& e) {
    out["error"] = e.what();
  }
  return out;
}

}  // namespace

int main(int argc, char** argv) {
  std::string elites_path = "data/elites.json";
  std::string record_path;
  int seat = -1;
  std::uint64_t seed = 1;
  cae::ExpertSettings settings;
  settings.search.iterations = 1600;
  for (int i = 1; i < argc; ++i) {
    const std::string flag = argv[i];
    const auto value = [&]() -> std::string {
      if (i + 1 >= argc) usage(argv[0]);
      return argv[++i];
    };
    if (flag == "--elites") {
      elites_path = value();
    } else if (flag == "--threads") {
      settings.threads = std::atoi(value().c_str());
    } else if (flag == "--iterations") {
      settings.search.iterations = std::atoi(value().c_str());
    } else if (flag == "--time-ms") {
      settings.time_limit = std::chrono::milliseconds(std::atoi(value().c_str()));
    } else if (flag == "--seed") {
      seed = std::strtoull(value().c_str(), nullptr, 10);
    } else if (flag == "--c") {
      settings.search.exploration = std::atof(value().c_str());
    } else if (flag == "--record") {
      record_path = value();
    } else if (flag == "--seat") {
      seat = std::atoi(value().c_str());
    } else {
      usage(argv[0]);
    }
  }
  if (settings.threads < 1 || settings.search.iterations < 1 || settings.time_limit.count() < 0) usage(argv[0]);

  std::vector<cae::ZooBot> pool;
  try {
    pool = cae::load_elites(elites_path);
  } catch (const std::exception& e) {
    std::fprintf(stderr, "%s\n", e.what());
    return 1;
  }

  if (!record_path.empty()) {
    if (seat < 0) usage(argv[0]);
    try {
      const json request = {{"id", 0}, {"seat", seat}, {"record", cae::read_record_file(record_path)}};
      std::printf("%s\n", answer(request, pool, settings, seed).dump().c_str());
    } catch (const std::exception& e) {
      std::fprintf(stderr, "%s\n", e.what());
      return 1;
    }
    return 0;
  }

  // Server: a detached thread per request, answers written whole under a
  // lock. At the end of the input it waits for the requests still running.
  std::mutex lock;
  std::condition_variable idle;
  int running = 0;
  std::string line;
  while (std::getline(std::cin, line)) {
    if (line.empty()) continue;
    {
      const std::lock_guard<std::mutex> guard(lock);
      ++running;
    }
    std::thread([line, &pool, settings, seed, &lock, &idle, &running] {
      json reply;
      try {
        reply = answer(json::parse(line), pool, settings, seed);
      } catch (const std::exception& e) {
        reply = {{"id", nullptr}, {"error", e.what()}};
      }
      const std::string text = reply.dump() + "\n";
      const std::lock_guard<std::mutex> guard(lock);
      std::fwrite(text.data(), 1, text.size(), stdout);
      std::fflush(stdout);
      --running;
      idle.notify_all();
    }).detach();
  }
  std::unique_lock<std::mutex> guard(lock);
  idle.wait(guard, [&] { return running == 0; });
  return 0;
}
