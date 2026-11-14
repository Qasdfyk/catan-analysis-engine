# catan_engine.js + catan_engine.wasm for the web viewer.
#
# The output goes straight into web/src/wasm/ (gitignored), where Vite picks
# it up as an ES module. Build it with the `wasm` preset after sourcing
# emsdk_env.sh; see docs/web.md.
add_executable(catan_engine bindings.cpp)
target_link_libraries(catan_engine PRIVATE catan_record catan_warnings)
set_target_properties(catan_engine PROPERTIES
  SUFFIX ".js"
  RUNTIME_OUTPUT_DIRECTORY "${PROJECT_SOURCE_DIR}/web/src/wasm")
target_link_options(catan_engine PRIVATE
  -lembind
  # A factory function (createEngine) exported as an ES module, so the page
  # and Node load it with `import`, and no globals leak into the page.
  -sMODULARIZE=1
  -sEXPORT_ES6=1
  -sEXPORT_NAME=createEngine
  # Long games hold every state; let the heap grow instead of failing.
  -sALLOW_MEMORY_GROWTH=1
  # The page, its benchmark worker and the Node scripts in web/scripts.
  -sENVIRONMENT=web,worker,node)
