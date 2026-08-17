add_executable(bootstrap-smoke
    bootstrap_smoke.cpp
)

target_link_libraries(bootstrap-smoke
    PRIVATE
        decompiler_gui
        Capstone::Capstone
)

enable_project_warnings(bootstrap-smoke)

add_test(
    NAME bootstrap_smoke
    COMMAND bootstrap-smoke
)

set_tests_properties(bootstrap_smoke PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    TIMEOUT 10
)

add_executable(elf-loader-tests
    elf_loader_tests.cpp
)

target_link_libraries(elf-loader-tests
    PRIVATE
        decompiler_core
)

enable_project_warnings(elf-loader-tests)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_executable(phase2-sample
        samples/arithmetic.cpp
    )

    target_compile_options(phase2-sample
        PRIVATE
            -O1
            -fno-inline
            -fno-omit-frame-pointer
            -fno-sanitize=all
    )

    target_link_options(phase2-sample PRIVATE -no-pie -fno-sanitize=all)
    enable_project_warnings(phase2-sample)

    add_test(
        NAME elf_loader
        COMMAND elf-loader-tests $<TARGET_FILE:phase2-sample>
    )
else()
    add_test(
        NAME elf_loader
        COMMAND elf-loader-tests
    )
endif()

add_executable(main-window-phase2-test
    main_window_phase2_test.cpp
)

target_link_libraries(main-window-phase2-test
    PRIVATE
        decompiler_gui
)

enable_project_warnings(main-window-phase2-test)

add_test(
    NAME main_window_phase2
    COMMAND main-window-phase2-test
)

set_tests_properties(main_window_phase2 PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    TIMEOUT 10
)

add_executable(pseudocode-view-tests
    pseudocode_view_tests.cpp
)

target_link_libraries(pseudocode-view-tests
    PRIVATE
        decompiler_gui
)

enable_project_warnings(pseudocode-view-tests)

add_test(
    NAME pseudocode_view
    COMMAND pseudocode-view-tests
)

set_tests_properties(pseudocode_view PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    TIMEOUT 10
)

add_executable(disassembler-tests
    disassembler_tests.cpp
)

target_link_libraries(disassembler-tests
    PRIVATE
        decompiler_core
)

enable_project_warnings(disassembler-tests)

add_test(
    NAME disassembler
    COMMAND disassembler-tests
)

add_executable(register-abi-ir-tests
    register_abi_ir_tests.cpp
)

target_link_libraries(register-abi-ir-tests
    PRIVATE
        decompiler_core
)

enable_project_warnings(register-abi-ir-tests)

add_test(
    NAME register_abi_ir
    COMMAND register-abi-ir-tests
)

add_executable(pseudocode-tests
    pseudocode_tests.cpp
)

target_link_libraries(pseudocode-tests
    PRIVATE
        decompiler_core
)

enable_project_warnings(pseudocode-tests)

add_test(
    NAME pseudocode
    COMMAND pseudocode-tests
)

add_executable(call-graph-tests
    call_graph_tests.cpp
)

target_link_libraries(call-graph-tests
    PRIVATE
        decompiler_core
)

enable_project_warnings(call-graph-tests)

add_test(
    NAME call_graph
    COMMAND call-graph-tests
)

add_executable(call-graph-view-tests
    call_graph_view_tests.cpp
)

target_link_libraries(call-graph-view-tests
    PRIVATE
        decompiler_gui
)

enable_project_warnings(call-graph-view-tests)

add_test(
    NAME call_graph_view
    COMMAND call-graph-view-tests
)

set_tests_properties(call_graph_view PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    TIMEOUT 10
)

add_executable(basic-block-cfg-tests
    basic_block_cfg_tests.cpp
)

target_link_libraries(basic-block-cfg-tests
    PRIVATE
        decompiler_core
)

enable_project_warnings(basic-block-cfg-tests)

add_test(
    NAME basic_block_cfg
    COMMAND basic-block-cfg-tests
)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_executable(analysis-phase3-tests
        analysis_phase3_tests.cpp
    )

    target_link_libraries(analysis-phase3-tests
        PRIVATE
            decompiler_core
    )

    enable_project_warnings(analysis-phase3-tests)

    add_test(
        NAME analysis_phase3
        COMMAND analysis-phase3-tests $<TARGET_FILE:phase2-sample>
    )

    add_executable(main-window-phase3-test
        main_window_phase3_test.cpp
    )

    target_link_libraries(main-window-phase3-test
        PRIVATE
            decompiler_gui
    )

    enable_project_warnings(main-window-phase3-test)

    add_test(
        NAME main_window_phase3
        COMMAND main-window-phase3-test $<TARGET_FILE:phase2-sample>
    )

    set_tests_properties(main_window_phase3 PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        TIMEOUT 10
    )

    add_executable(phase4-loop-sample
        samples/simple_loop.cpp
    )

    target_compile_options(phase4-loop-sample
        PRIVATE
            -O1
            -fno-inline
            -fno-omit-frame-pointer
            -fno-sanitize=all
    )

    target_link_options(phase4-loop-sample PRIVATE -no-pie -fno-sanitize=all)
    enable_project_warnings(phase4-loop-sample)

    add_executable(phase5-stack-sample
        samples/stack_local.cpp
    )

    target_compile_options(phase5-stack-sample
        PRIVATE
            -O1
            -fno-inline
            -fno-omit-frame-pointer
            -fno-sanitize=all
    )

    target_link_options(phase5-stack-sample PRIVATE -no-pie -fno-sanitize=all)
    enable_project_warnings(phase5-stack-sample)

    add_executable(phase6-branch-sample
        samples/branching.cpp
    )

    target_compile_options(phase6-branch-sample
        PRIVATE
            -O1
            -fno-inline
            -fno-omit-frame-pointer
            -fno-optimize-sibling-calls
            -fno-sanitize=all
    )

    target_link_options(phase6-branch-sample PRIVATE -no-pie -fno-sanitize=all)
    enable_project_warnings(phase6-branch-sample)

    function(add_mandatory_sample target source)
        add_executable(${target} ${source})
        target_compile_options(${target}
            PRIVATE
                -O1
                -fno-inline
                -fno-omit-frame-pointer
                -fno-optimize-sibling-calls
                -fno-sanitize=all
        )
        target_link_options(${target} PRIVATE -no-pie -fno-sanitize=all)
        enable_project_warnings(${target})
    endfunction()

    add_mandatory_sample(spec-simple-if-sample samples/simple_if.cpp)
    add_mandatory_sample(spec-if-else-sample samples/if_else.cpp)
    add_mandatory_sample(spec-function-call-sample samples/function_call.cpp)
    add_mandatory_sample(spec-nested-condition-sample samples/nested_condition.cpp)
    add_mandatory_sample(spec-recursion-sample samples/recursion.cpp)
    add_mandatory_sample(spec-strings-sample samples/strings.cpp)

    add_executable(spec-acceptance-tests
        spec_acceptance_tests.cpp
    )

    target_link_libraries(spec-acceptance-tests
        PRIVATE
            decompiler_core
    )

    enable_project_warnings(spec-acceptance-tests)

    add_test(
        NAME specification_acceptance
        COMMAND spec-acceptance-tests
            $<TARGET_FILE:phase2-sample>
            $<TARGET_FILE:spec-simple-if-sample>
            $<TARGET_FILE:spec-if-else-sample>
            $<TARGET_FILE:phase4-loop-sample>
            $<TARGET_FILE:spec-function-call-sample>
            $<TARGET_FILE:spec-nested-condition-sample>
            $<TARGET_FILE:spec-recursion-sample>
            $<TARGET_FILE:spec-strings-sample>
    )

    add_executable(cfg-integration-tests
        cfg_integration_tests.cpp
    )

    target_link_libraries(cfg-integration-tests
        PRIVATE
            decompiler_core
    )

    enable_project_warnings(cfg-integration-tests)

    add_test(
        NAME cfg_integration
        COMMAND cfg-integration-tests $<TARGET_FILE:phase4-loop-sample>
    )

    add_executable(ir-integration-tests
        ir_integration_tests.cpp
    )

    target_link_libraries(ir-integration-tests
        PRIVATE
            decompiler_core
    )

    enable_project_warnings(ir-integration-tests)

    add_test(
        NAME ir_integration
        COMMAND ir-integration-tests
            $<TARGET_FILE:phase2-sample>
            $<TARGET_FILE:phase5-stack-sample>
    )

    add_executable(pseudocode-integration-tests
        pseudocode_integration_tests.cpp
    )

    target_link_libraries(pseudocode-integration-tests
        PRIVATE
            decompiler_core
    )

    enable_project_warnings(pseudocode-integration-tests)

    add_test(
        NAME pseudocode_integration
        COMMAND pseudocode-integration-tests
            $<TARGET_FILE:phase2-sample>
            $<TARGET_FILE:phase4-loop-sample>
            $<TARGET_FILE:phase5-stack-sample>
            $<TARGET_FILE:phase6-branch-sample>
    )

    add_executable(main-window-phase6-test
        main_window_phase6_test.cpp
    )

    target_link_libraries(main-window-phase6-test
        PRIVATE
            decompiler_gui
    )

    enable_project_warnings(main-window-phase6-test)

    add_test(
        NAME main_window_phase6
        COMMAND main-window-phase6-test $<TARGET_FILE:phase6-branch-sample>
    )

    set_tests_properties(main_window_phase6 PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        TIMEOUT 10
    )

    add_executable(main-window-workspace-test
        main_window_workspace_test.cpp
    )

    target_link_libraries(main-window-workspace-test
        PRIVATE
            decompiler_gui
    )

    enable_project_warnings(main-window-workspace-test)

    add_test(
        NAME main_window_workspace
        COMMAND main-window-workspace-test $<TARGET_FILE:phase6-branch-sample>
    )

    set_tests_properties(main_window_workspace PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        TIMEOUT 10
    )

    add_executable(main-window-phase7-test
        main_window_phase7_test.cpp
    )

    target_link_libraries(main-window-phase7-test
        PRIVATE
            decompiler_gui
    )

    enable_project_warnings(main-window-phase7-test)

    add_test(
        NAME main_window_phase7
        COMMAND main-window-phase7-test
            $<TARGET_FILE:phase6-branch-sample>
            $<TARGET_FILE:phase2-sample>
    )

    set_tests_properties(main_window_phase7 PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        TIMEOUT 10
    )

    add_executable(phase8-pie-o2-sample
        samples/optimized_pie.cpp
    )

    target_compile_options(phase8-pie-o2-sample
        PRIVATE
            -O2
            -fPIE
            -fno-inline
            -fno-omit-frame-pointer
            -fno-optimize-sibling-calls
            -fno-sanitize=all
    )

    target_link_options(phase8-pie-o2-sample PRIVATE -pie -fno-sanitize=all)
    enable_project_warnings(phase8-pie-o2-sample)

    add_executable(phase8-pie-o3-sample
        samples/optimized_pie.cpp
    )

    target_compile_options(phase8-pie-o3-sample
        PRIVATE
            -O3
            -fPIE
            -fno-inline
            -fno-omit-frame-pointer
            -fno-optimize-sibling-calls
            -fno-sanitize=all
    )

    target_link_options(phase8-pie-o3-sample PRIVATE -pie -fno-sanitize=all)
    enable_project_warnings(phase8-pie-o3-sample)

    add_executable(optimized-pie-tests
        optimized_pie_tests.cpp
    )

    target_link_libraries(optimized-pie-tests
        PRIVATE
            decompiler_core
    )

    enable_project_warnings(optimized-pie-tests)

    add_test(
        NAME optimized_pie
        COMMAND optimized-pie-tests
            $<TARGET_FILE:phase8-pie-o2-sample>
            $<TARGET_FILE:phase8-pie-o3-sample>
    )

    add_executable(binary-patcher-tests
        binary_patcher_tests.cpp
    )

    target_link_libraries(binary-patcher-tests
        PRIVATE
            decompiler_core
    )

    enable_project_warnings(binary-patcher-tests)

    add_test(
        NAME binary_patcher
        COMMAND binary-patcher-tests $<TARGET_FILE:phase8-pie-o3-sample>
    )

    add_executable(main-window-phase8-test
        main_window_phase8_test.cpp
    )

    target_link_libraries(main-window-phase8-test
        PRIVATE
            decompiler_gui
    )

    enable_project_warnings(main-window-phase8-test)

    add_test(
        NAME main_window_phase8
        COMMAND main-window-phase8-test $<TARGET_FILE:phase8-pie-o3-sample>
    )

    set_tests_properties(main_window_phase8 PROPERTIES
        ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
        TIMEOUT 20
    )
endif()
