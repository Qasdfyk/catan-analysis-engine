add_executable(catan_replay replay.cpp)
target_link_libraries(catan_replay PRIVATE catan_record catan_warnings)

add_executable(catan_selfplay selfplay.cpp)
target_link_libraries(catan_selfplay PRIVATE catan catan_warnings Threads::Threads)

add_executable(catan_zoo_check zoo_check.cpp)
target_link_libraries(catan_zoo_check PRIVATE catan_record catan_warnings)

add_executable(catan_winrate winrate.cpp)
target_link_libraries(catan_winrate PRIVATE catan_record catan_warnings Threads::Threads)

# POSIX only (it starts Catanatron as a child process through pipes).
if(NOT EMSCRIPTEN)
  add_executable(catan_arena arena.cpp)
  target_link_libraries(catan_arena PRIVATE catan_record catan_warnings Threads::Threads)
endif()

add_executable(catan_setup setup.cpp)
target_link_libraries(catan_setup PRIVATE catan_record catan_warnings Threads::Threads)

add_executable(catan_validate validate.cpp)
target_link_libraries(catan_validate PRIVATE catan_record catan_warnings Threads::Threads)

add_executable(catan_map_elites map_elites.cpp)
target_link_libraries(catan_map_elites PRIVATE catan_record catan_warnings Threads::Threads)

add_executable(catan_mcts mcts.cpp)
target_link_libraries(catan_mcts PRIVATE catan_record catan_warnings Threads::Threads)

add_executable(catan_expert expert.cpp)
target_link_libraries(catan_expert PRIVATE catan_record catan_warnings Threads::Threads)

add_executable(catan_golden golden.cpp)
target_link_libraries(catan_golden PRIVATE catan_record catan_warnings Threads::Threads)

add_executable(catan_review review.cpp)
target_link_libraries(catan_review PRIVATE catan_record catan_warnings Threads::Threads)
