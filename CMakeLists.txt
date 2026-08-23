cmake_minimum_required(VERSION 3.25)

project(RecallMemory VERSION 0.1.0 LANGUAGES C CXX)

option(RECALL_MEMORY_BUILD_TESTS "Build Recall Memory tests" ON)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(cmake/Dependencies.cmake)
include(cmake/Warnings.cmake)

add_library(recall_memory_core
    src/domain/json.cpp
    src/storage/sqlite.cpp
    src/storage/store.cpp
    src/codegraph/cpp_parser.cpp
    src/codegraph/indexer.cpp
    src/memory/service.cpp
)
target_include_directories(recall_memory_core PUBLIC include PRIVATE src)
target_link_libraries(recall_memory_core
    PUBLIC nlohmann_json::nlohmann_json
    PRIVATE recall_memory_sqlite tree-sitter tree-sitter-cpp
)
recall_memory_enable_warnings(recall_memory_core)

add_library(recall_memory_transport
    src/api/http_server.cpp
    src/mcp/server.cpp
)
target_include_directories(recall_memory_transport PUBLIC include)
target_link_libraries(recall_memory_transport
    PUBLIC recall_memory_core
    PRIVATE httplib::httplib
)
recall_memory_enable_warnings(recall_memory_transport)

if(WIN32)
    add_executable(recall-memory WIN32 apps/desktop/main.cpp)
    target_compile_definitions(recall-memory PRIVATE UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(recall-memory PRIVATE recall_memory_transport Comctl32 Comdlg32 Shell32)
    recall_memory_enable_warnings(recall-memory)
endif()

if(RECALL_MEMORY_BUILD_TESTS)
    include(CTest)
    enable_testing()
    add_subdirectory(tests)
endif()
