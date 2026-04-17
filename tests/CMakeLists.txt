# Tests for the st80-2026 core.
#
# Phase 1 tests are lightweight — no gtest yet. They run as plain
# executables and CTest checks their exit code. Add gtest (or
# Catch2) only when test volume justifies it.

add_executable(core_smoke_test core_smoke_test.cpp)
target_link_libraries(core_smoke_test PRIVATE st80core)
add_test(NAME core_smoke_test COMMAND core_smoke_test)

if(APPLE)
    add_executable(bridge_api_test bridge_api_test.cpp)
    target_link_libraries(bridge_api_test PRIVATE st80_apple)
    # Must run from the project root so the skip-check can find the
    # `reference/xerox-image/VirtualImage` path.
    add_test(NAME bridge_api_test
             COMMAND bridge_api_test
             WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
    set_tests_properties(bridge_api_test PROPERTIES SKIP_RETURN_CODE 77)
endif()

# Phase 1 gate regression check: diff st80_run output vs the Xerox
# trace2 reference. Skipped when the image or trace file isn't
# present in reference/xerox-image/.
add_test(
    NAME trace2_check
    COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/trace2_check.sh
            ${CMAKE_SOURCE_DIR}/reference/xerox-image/VirtualImage
            ${CMAKE_SOURCE_DIR}/reference/xerox-image/trace2
            $<TARGET_FILE:st80_run>
)
set_tests_properties(trace2_check PROPERTIES SKIP_RETURN_CODE 77)
