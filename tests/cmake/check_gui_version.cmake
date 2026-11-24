# Plan 0060 A3 (D11): `hwio-monitor --version` and `--help` print English text on stdout and exit 0
# before any window: no QGuiApplication, so no display is needed. Run in script mode:
#   cmake -DHWIO_GUI=<hwio-monitor> -DHWIO_CLI=<hwio> -P <this file>
# Each run has a time limit: a build that opened its window instead would never exit by itself.
# Two environments: no platform and no display (Qt would abort without one on Linux), and the
# offscreen platform (Qt would open the window there and the run would time out).
execute_process(COMMAND "${HWIO_CLI}" --version OUTPUT_VARIABLE _cli RESULT_VARIABLE _rc
    TIMEOUT 30)
if(NOT _rc EQUAL 0 OR NOT _cli MATCHES "^hwio ([^\r\n]+)")
    message(FATAL_ERROR "gui_version_check: hwio --version failed (${_rc}): ${_cli}")
endif()
set(_version "${CMAKE_MATCH_1}")

# "|" separates the arguments of `cmake -E env` inside one list element.
set(_environments
    "--unset=QT_QPA_PLATFORM|--unset=DISPLAY|--unset=WAYLAND_DISPLAY"
    "QT_QPA_PLATFORM=offscreen")
foreach(_shown IN LISTS _environments)
    string(REPLACE "|" ";" _env_args "${_shown}")
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env ${_env_args} "${HWIO_GUI}" --version
        OUTPUT_VARIABLE _out ERROR_VARIABLE _err RESULT_VARIABLE _rc TIMEOUT 30)
    string(REPLACE "\r\n" "\n" _out "${_out}")
    if(NOT _rc EQUAL 0)
        message(FATAL_ERROR "gui_version_check: hwio-monitor --version (${_shown}) ended with "
            "'${_rc}' (a timeout means that it opened its window): ${_out}${_err}")
    endif()
    string(FIND "${_out}" "HARDWARIO Monitor ${_version} (" _at)
    if(NOT _at EQUAL 0 OR NOT _out MATCHES "^HARDWARIO Monitor [^\n]+ \\([^)\n]+\\)\n$")
        message(FATAL_ERROR "gui_version_check: unexpected --version output (${_shown}), "
            "expected 'HARDWARIO Monitor ${_version} (<git hash>)': '${_out}'")
    endif()

    execute_process(COMMAND "${CMAKE_COMMAND}" -E env ${_env_args} "${HWIO_GUI}" --help
        OUTPUT_VARIABLE _out ERROR_VARIABLE _err RESULT_VARIABLE _rc TIMEOUT 30)
    if(NOT _rc EQUAL 0)
        message(FATAL_ERROR "gui_version_check: hwio-monitor --help (${_shown}) ended with "
            "'${_rc}' (a timeout means that it opened its window): ${_out}${_err}")
    endif()
    if(NOT _out MATCHES "Usage: hwio-monitor" OR NOT _out MATCHES "--version"
       OR NOT _out MATCHES "--help")
        message(FATAL_ERROR "gui_version_check: unexpected --help output (${_shown}): '${_out}'")
    endif()
endforeach()
message(STATUS "gui_version_check: --version and --help OK (${_version})")
