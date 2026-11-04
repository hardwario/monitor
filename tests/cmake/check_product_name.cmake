# ADR-0237 (plan/features/0033 A1): the product is called "HARDWARIO Monitor". "2" may follow the name only as part
# of a version number ("HARDWARIO Monitor 2.0", "HARDWARIO Monitor 2.1.0") — never "HARDWARIO Monitor 2" as a name.
# Scans README.md, docs/**/*.md and the release notes from v2.0.0-rc.3 on (the published rc.2 text stays, D6).
#   cmake -DHWIO_SOURCE_DIR=<repository root> -P check_product_name.cmake

if(NOT IS_DIRECTORY "${HWIO_SOURCE_DIR}/docs")
    message(FATAL_ERROR "product_name_check: HWIO_SOURCE_DIR has no docs/: '${HWIO_SOURCE_DIR}'")
endif()

file(GLOB_RECURSE _files RELATIVE "${HWIO_SOURCE_DIR}"
    "${HWIO_SOURCE_DIR}/docs/*.md" "${HWIO_SOURCE_DIR}/packaging/release-notes/*.md")
list(APPEND _files "README.md")
list(FILTER _files EXCLUDE REGEX "release-notes/v2\\.0\\.0-rc\\.[12]\\.md$")
list(SORT _files)
set(_violations 0)
set(_checked 0)
foreach(_file IN LISTS _files)
    math(EXPR _checked "${_checked} + 1")
    file(STRINGS "${HWIO_SOURCE_DIR}/${_file}" _lines)
    foreach(_line IN LISTS _lines)
        # "Monitor 2" not followed by ".<digit>" (a version) or another digit.
        if(_line MATCHES "Monitor 2([^.0-9]|$)" OR _line MATCHES "Monitor 2\\.([^0-9]|$)")
            message(SEND_ERROR "${_file}: \"HARDWARIO Monitor 2\" used as a name (ADR-0237): ${_line}")
            math(EXPR _violations "${_violations} + 1")
        endif()
    endforeach()
endforeach()

if(_violations GREATER 0)
    message(FATAL_ERROR "product_name_check: ${_violations} problem(s)")
endif()
message(STATUS "product_name_check: ${_checked} files OK")
