add_executable(catan_replay replay.cpp)
target_link_libraries(catan_replay PRIVATE catan_record catan_warnings)

add_executable(catan_selfplay selfplay.cpp)
target_link_libraries(catan_selfplay PRIVATE catan catan_warnings Threads::Threads)
