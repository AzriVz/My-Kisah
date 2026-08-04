# Locate the Capstone disassembly engine and expose Capstone::Capstone.

find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(PC_CAPSTONE QUIET capstone)
endif()

find_path(Capstone_INCLUDE_DIR
    NAMES capstone/capstone.h
    HINTS ${PC_CAPSTONE_INCLUDE_DIRS}
)

find_library(Capstone_LIBRARY
    NAMES capstone
    HINTS ${PC_CAPSTONE_LIBRARY_DIRS}
)

set(Capstone_VERSION "${PC_CAPSTONE_VERSION}")

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(Capstone
    REQUIRED_VARS
        Capstone_LIBRARY
        Capstone_INCLUDE_DIR
    VERSION_VAR Capstone_VERSION
)

if(Capstone_FOUND AND NOT TARGET Capstone::Capstone)
    add_library(Capstone::Capstone UNKNOWN IMPORTED)
    set_target_properties(Capstone::Capstone PROPERTIES
        IMPORTED_LOCATION "${Capstone_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${Capstone_INCLUDE_DIR}"
    )
endif()

mark_as_advanced(Capstone_INCLUDE_DIR Capstone_LIBRARY)

