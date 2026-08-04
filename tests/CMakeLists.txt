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
