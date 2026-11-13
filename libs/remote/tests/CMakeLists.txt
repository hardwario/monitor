# Unit and integration tests of libs/remote (GoogleTest, ADR-0010): the packet codecs, session codes
# and relay URLs, and the relay client, host and joiner against FakeRelay (a QWebSocketServer copy
# of ~/terminal/server/index.js). Limits are shrunk instead of a fake clock (as libs/mcp). Run under
# ASan/UBSan and TSan.

# The fake relay is also used by the CLI end-to-end tests (app/cli/tests).
add_library(hwio_remote_fake_relay STATIC fake_relay.cpp fake_relay.h)
hwio_apply_target_defaults(hwio_remote_fake_relay)
target_include_directories(hwio_remote_fake_relay PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(hwio_remote_fake_relay PUBLIC hwio::remote Qt6::WebSockets)
set_target_properties(hwio_remote_fake_relay PROPERTIES AUTOMOC ON)

add_executable(hwio_remote_tests
    fake_console.h
    main.cpp
    protocol_test.cpp
    relay_client_test.cpp
    remote_host_test.cpp
    remote_session_test.cpp
    remote_test_support.h
    session_code_test.cpp
    transcript_test.cpp
    tunnel_test.cpp)
hwio_apply_target_defaults(hwio_remote_tests)
target_include_directories(hwio_remote_tests PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/../src")
target_link_libraries(hwio_remote_tests PRIVATE hwio::remote hwio_remote_fake_relay GTest::gmock)
target_compile_definitions(hwio_remote_tests PRIVATE ${HWIO_TEST_OVERRIDES_DEFINE})

gtest_discover_tests(hwio_remote_tests
    DISCOVERY_TIMEOUT 60
    PROPERTIES LABELS "unit")
