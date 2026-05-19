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
# present in reference/xerox-image/. The check is a bash script —
# on Windows it runs from Git Bash or WSL, so gate it to UNIX
# hosts and let CI add a separate Windows smoke test below.
if(UNIX)
    add_test(
        NAME trace2_check
        COMMAND bash ${CMAKE_CURRENT_SOURCE_DIR}/trace2_check.sh
                ${CMAKE_SOURCE_DIR}/reference/xerox-image/VirtualImage
                ${CMAKE_SOURCE_DIR}/reference/xerox-image/trace2
                $<TARGET_FILE:st80_run>
    )
    set_tests_properties(trace2_check PROPERTIES SKIP_RETURN_CODE 77)
endif()

# Headless "fake GUI" gate (pharo-headless-test analog, tools/
# st80_gui_test): boots the real image through Bridge.h, injects
# synthetic input, asserts the live Smalltalk-80 environment visibly
# reacts, and drops PNG screenshots in the build dir for inspection.
# Runs on every host (the platform bridge keeps the framebuffer in
# memory — no window). Not under DJGPP: that .exe runs under dosiz,
# not natively (same split as trace2's RUNNER path).
if(NOT DJGPP)
    add_test(
        NAME st80_gui_test
        COMMAND st80_gui_test
                ${CMAKE_SOURCE_DIR}/reference/xerox-image/VirtualImage
                ${CMAKE_CURRENT_BINARY_DIR}
    )
    set_tests_properties(st80_gui_test PROPERTIES SKIP_RETURN_CODE 77)
endif()

# Image-backed correctness gates. Cross-platform (no shell), so they
# run on every host; the DJGPP build runs the same .exe under dosiz
# for the DOS-port gate (see docs/dos-plan.md). Added only when the
# Xerox image is actually present, so no skip plumbing is needed.
set(ST80_XEROX_IMAGE ${CMAKE_SOURCE_DIR}/reference/xerox-image/VirtualImage)
if(NOT DJGPP AND EXISTS ${ST80_XEROX_IMAGE})
    # Fast structural gate: walk the OT, verify class refs + word
    # lengths after load. (docs/testing.md asked for this as CTest.)
    add_test(NAME st80_validate_check
             COMMAND st80_validate check ${ST80_XEROX_IMAGE})

    # D4 regression gate: load->saveSnapshot->reload must yield a
    # bit-identical object graph. Guards the create_file/write path
    # (incl. the dosiz AH=40 + O_BINARY path on the DOS port) and
    # byte-order / OT-layout stability.
    add_test(NAME st80_validate_roundtrip
             COMMAND st80_validate roundtrip ${ST80_XEROX_IMAGE})
endif()
