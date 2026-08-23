include(FetchContent)

set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build static dependencies" FORCE)
set(JSON_BuildTests OFF CACHE INTERNAL "")
set(HTTPLIB_COMPILE OFF CACHE BOOL "Use header-only cpp-httplib" FORCE)
set(CATCH_BUILD_TESTING OFF CACHE BOOL "Do not build Catch2 self tests" FORCE)

FetchContent_Declare(
    nlohmann_json
    URL https://codeload.github.com/nlohmann/json/tar.gz/refs/tags/v3.12.0
    URL_HASH SHA256=4b92eb0c06d10683f7447ce9406cb97cd4b453be18d7279320f7b2f025c10187
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_Declare(
    httplib
    URL https://codeload.github.com/yhirose/cpp-httplib/tar.gz/refs/tags/v0.53.1
    URL_HASH SHA256=185af9587e270de9a3bfee234c6740f02e82265da33c7a41f97e02ee42f979d2
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_Declare(
    tree_sitter
    URL https://codeload.github.com/tree-sitter/tree-sitter/tar.gz/refs/tags/v0.26.13
    URL_HASH SHA256=ece24c3c5e2a76384075e830c7139b59fce8fb01e4ef8436fab08bbe10444c89
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_Declare(
    tree_sitter_cpp
    URL https://codeload.github.com/tree-sitter/tree-sitter-cpp/tar.gz/refs/tags/v0.23.4
    URL_HASH SHA256=7a2c55afe3028f4105f25762ea58cc16537d1f5a1dcd9cca90410b3cd5d46051
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SOURCE_SUBDIR _recall_memory_prevent_grammar_regeneration
)
FetchContent_Declare(
    sqlite_amalgamation
    URL https://www.sqlite.org/2026/sqlite-amalgamation-3530400.zip
    URL_HASH SHA256=1e71ddf93849c6a6ecf58b827c0692073d2dd7ee40196158068f7b29f422e87d
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

FetchContent_MakeAvailable(nlohmann_json httplib tree_sitter tree_sitter_cpp sqlite_amalgamation)

add_library(tree-sitter-cpp STATIC
    ${tree_sitter_cpp_SOURCE_DIR}/src/parser.c
    ${tree_sitter_cpp_SOURCE_DIR}/src/scanner.c
)
target_include_directories(tree-sitter-cpp PRIVATE ${tree_sitter_cpp_SOURCE_DIR}/src)
target_link_libraries(tree-sitter-cpp PUBLIC tree-sitter)
set_target_properties(tree-sitter-cpp PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON)
if(MSVC)
    target_compile_options(tree-sitter-cpp PRIVATE /wd4244 /wd4267 /utf-8)
endif()

add_library(recall_memory_sqlite STATIC ${sqlite_amalgamation_SOURCE_DIR}/sqlite3.c)
target_include_directories(recall_memory_sqlite PUBLIC ${sqlite_amalgamation_SOURCE_DIR})
target_compile_definitions(recall_memory_sqlite PRIVATE
    SQLITE_ENABLE_FTS5
    SQLITE_ENABLE_JSON1
    SQLITE_THREADSAFE=1
    SQLITE_OMIT_LOAD_EXTENSION
)
if(MSVC)
    target_compile_definitions(recall_memory_sqlite PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_compile_options(recall_memory_sqlite PRIVATE /wd4244 /wd4267)
endif()

if(RECALL_MEMORY_BUILD_TESTS)
    FetchContent_Declare(
        Catch2
        URL https://codeload.github.com/catchorg/Catch2/tar.gz/refs/tags/v3.15.3
        URL_HASH SHA256=b0299ae552918220a7a6e21e7de5b714777f4e8c883fb70c4bb23fe01df8c6e3
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_MakeAvailable(Catch2)
endif()
