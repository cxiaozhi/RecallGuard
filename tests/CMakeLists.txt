add_executable(recall_memory_tests
    codegraph_test.cpp
    mcp_test.cpp
    memory_test.cpp
)
target_link_libraries(recall_memory_tests PRIVATE recall_memory_transport Catch2::Catch2WithMain)
recall_memory_enable_warnings(recall_memory_tests)

include(Catch)
catch_discover_tests(recall_memory_tests)
