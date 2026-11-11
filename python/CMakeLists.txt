# The engine as a Python module, so tools/difftest.py can run it side by side
# with catan-project's game.py in one process.
pybind11_add_module(cae_py bindings.cpp)
target_link_libraries(cae_py PRIVATE catan_record catan_warnings)
