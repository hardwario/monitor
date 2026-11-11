# libs/remote - remote support session (plan/features/0044-remote-support-session.md): Engine.IO v4 /
# Socket.IO v5 client of the HARDWARIO Terminal relay over QtWebSockets (ADR-0321), session codes
# and relay URLs (ADR-0334, ADR-0330), the host side (RemoteHost: console mapping, join approval
# ADR-0335, operations ADR-0336) and the joiner side (RemoteConsoleSession, Transport::Remote).
#
# Backend library: QtCore + QtNetwork + QtWebSockets (ADR-0004 extended by ADR-0321), static
# (ADR-0022).

hwio_add_backend_library(hwio_remote
    include/hwio/remote/consent.h
    include/hwio/remote/host_operations.h
    include/hwio/remote/protocol.h
    include/hwio/remote/relay_client.h
    include/hwio/remote/remote_console_session.h
    include/hwio/remote/remote_host.h
    include/hwio/remote/remote_operation_client.h
    include/hwio/remote/session_code.h
    include/hwio/remote/transcript.h
    include/hwio/remote/tunnel.h
    src/consent.cpp
    src/host_operations.cpp
    src/protocol.cpp
    src/relay_client.cpp
    src/relay_worker.h
    src/remote_console_session.cpp
    src/remote_host.cpp
    src/remote_operation_client.cpp
    src/session_code.cpp
    src/transcript.cpp
    src/tunnel.cpp)
add_library(hwio::remote ALIAS hwio_remote)
target_include_directories(hwio_remote PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_include_directories(hwio_remote PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(hwio_remote PUBLIC hwio::core hwio::device hwio::firmware Qt6::Core Qt6::Network
    Qt6::WebSockets)
target_compile_definitions(hwio_remote PRIVATE ${HWIO_TEST_OVERRIDES_DEFINE})
set_target_properties(hwio_remote PROPERTIES AUTOMOC ON)

if(HWIO_BUILD_TESTS)
    add_subdirectory(tests)
endif()
