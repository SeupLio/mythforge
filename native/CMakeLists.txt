set(MFC_SOURCES
  mfc/src/common.cpp
  mfc/src/json.cpp
  mfc/src/hash.cpp
  mfc/src/events.cpp
  mfc/src/lexer.cpp
  mfc/src/parser.cpp
  mfc/src/sema.cpp
  mfc/src/ir.cpp
  mfc/src/codegen.cpp
  mfc/src/predicate.cpp
  mfc/src/module.cpp
  mfc/src/graph.cpp
)

add_library(mfc STATIC ${MFC_SOURCES})
target_include_directories(mfc PUBLIC mfc/include)

set(MFVM_SOURCES
  mfvm/src/value.cpp
  mfvm/src/vm.cpp
)

add_library(mfvm STATIC ${MFVM_SOURCES})
target_include_directories(mfvm PUBLIC mfvm/include)
target_link_libraries(mfvm PUBLIC mfc)

add_library(mfagent STATIC tools/mf/agent.cpp)
target_include_directories(mfagent PUBLIC tools/mf)
target_link_libraries(mfagent PUBLIC mfc)
if(NOT MSVC)
  target_compile_options(mfagent PRIVATE -Wall -Wextra -Werror)
else()
  target_compile_options(mfagent PRIVATE /W4 /WX)
endif()

add_executable(mf tools/mf/main.cpp)
target_link_libraries(mf PRIVATE mfc mfvm mfagent)
target_include_directories(mf PRIVATE tools/mf)

# Content service (M8): dependency-free HTTP/1.1 daemon over the same compiler.
add_executable(mfd tools/mfd/main.cpp)
target_link_libraries(mfd PRIVATE mfc mfagent)
target_include_directories(mfd PRIVATE tools/mf)
if(WIN32)
  target_link_libraries(mfd PRIVATE ws2_32)
endif()
if(NOT MSVC)
  target_compile_options(mfd PRIVATE -Wall -Wextra -Werror)
else()
  target_compile_options(mfd PRIVATE /W4 /WX)
endif()

# Runtime C ABI (M7). PREFIX is cleared so Windows/MinGW both produce
# mythforge.dll, which is what Unity/Kotlin/Swift hosts load.
add_library(mythforge_sdk SHARED sdk/src/mf_sdk.cpp)
target_include_directories(mythforge_sdk PUBLIC sdk/include)
target_link_libraries(mythforge_sdk PRIVATE mfc mfvm)
set_target_properties(mythforge_sdk PROPERTIES
  PREFIX ""
  OUTPUT_NAME "mythforge"
  CXX_VISIBILITY_PRESET hidden
  VISIBILITY_INLINES_HIDDEN ON
)
if(NOT MSVC)
  # Avoid a runtime dependency on MinGW's libstdc++/libgcc DLLs so a Unity
  # Editor can load the plugin without shipping extra DLLs.
  target_link_options(mythforge_sdk PRIVATE -static -static-libgcc -static-libstdc++)
  target_compile_options(mythforge_sdk PRIVATE -Wall -Wextra -Werror)
else()
  target_compile_options(mythforge_sdk PRIVATE /W4 /WX)
endif()

if(MSVC)
  target_compile_options(mfc PRIVATE /W4 /WX)
  target_compile_options(mfvm PRIVATE /W4 /WX)
  target_compile_options(mf PRIVATE /W4 /WX)
else()
  target_compile_options(mfc PRIVATE -Wall -Wextra -Werror)
  target_compile_options(mfvm PRIVATE -Wall -Wextra -Werror)
  target_compile_options(mf PRIVATE -Wall -Wextra -Werror)
endif()

enable_testing()

add_executable(mythforge_tests
  tests/test_main.cpp
  tests/test_json.cpp
  tests/test_hash.cpp
  tests/test_lexer.cpp
  tests/test_parser.cpp
  tests/test_sema.cpp
  tests/test_compiler.cpp
  tests/test_module.cpp
  tests/test_vm.cpp
  tests/test_graph.cpp
  tests/test_sdk.cpp
  tests/test_agent.cpp
)
target_link_libraries(mythforge_tests PRIVATE mfc mfvm mfagent mythforge_sdk)
target_include_directories(mythforge_tests PRIVATE tests sdk/include tools/mf)
target_compile_definitions(mythforge_tests PRIVATE
  MYTHFORGE_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)
if(NOT MSVC)
  target_compile_options(mythforge_tests PRIVATE -Wall -Wextra -Werror)
else()
  target_compile_options(mythforge_tests PRIVATE /W4 /WX)
endif()
add_test(NAME unit COMMAND mythforge_tests)
add_test(
  NAME content
  COMMAND ${CMAKE_COMMAND}
    -DMF=$<TARGET_FILE:mf>
    -DCONTENT=${CMAKE_SOURCE_DIR}/content
    -DSCHEMA=${CMAKE_SOURCE_DIR}/schema/node_types.json
    -P ${CMAKE_SOURCE_DIR}/scripts/run_content_tests.cmake)

option(MYTHFORGE_BUILD_FUZZERS "Build libFuzzer harnesses (Clang only)" OFF)
if(MYTHFORGE_BUILD_FUZZERS)
  if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "MYTHFORGE_BUILD_FUZZERS requires Clang")
  endif()
  add_executable(fuzz_module tests/fuzz_module.cpp)
  target_link_libraries(fuzz_module PRIVATE mfc)
  target_compile_options(fuzz_module PRIVATE -fsanitize=fuzzer,address,undefined)
  target_link_options(fuzz_module PRIVATE -fsanitize=fuzzer,address,undefined)
endif()
