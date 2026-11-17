add_executable(catan_replay replay.cpp)
target_link_libraries(catan_replay PRIVATE catan_record catan_warnings)

add_executable(catan_selfplay selfplay.cpp)
target_link_libraries(catan_selfplay PRIVATE catan catan_warnings Threads::Threads)

add_executable(catan_zoo_check zoo_check.cpp)
target_link_libraries(catan_zoo_check PRIVATE catan_record catan_warnings)

add_executable(catan_winrate winrate.cpp)
target_link_libraries(catan_winrate PRIVATE catan_record catan_warnings Threads::Threads)
