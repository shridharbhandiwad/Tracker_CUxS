# Findfastrtps.cmake
#
# Locates the eProsima Fast DDS 2.x library (CMake package name: fastrtps).
#
# Strategy
# --------
# 1. Delegate to the upstream config-file package (fastrtpsConfig.cmake /
#    fastrtps-config.cmake) using every well-known installation prefix so the
#    installed targets and version information are used when available.
# 2. Fall back to a classic header + library search when the config file is
#    absent (e.g. a custom build tree, missing installer step, etc.).
#
# The module honours the following environment / cache variables as extra
# search prefixes in both modes:
#
#   FASTRTPS_HOME   – root of a Fast DDS 2.x installation
#   FASTDDS_HOME    – alias accepted by newer eProsima installers
#   fastrtps_DIR    – directory containing fastrtpsConfig.cmake (CMake cache)
#
# Imported target created (when not already defined by the config file)
# -----------------------------------------------------------------------
#   fastrtps         – UNKNOWN IMPORTED target with include dirs + location
#
# Result variables
# ----------------
#   fastrtps_FOUND
#   fastrtps_INCLUDE_DIR
#   fastrtps_LIBRARY

cmake_policy(PUSH)
cmake_policy(SET CMP0057 NEW)   # IN_LIST support

# ---------------------------------------------------------------------------
# Helper: collect candidate prefixes from env vars and well-known paths
# ---------------------------------------------------------------------------
set(_fastrtps_PREFIX_HINTS)

foreach(_env_var FASTRTPS_HOME FASTDDS_HOME)
    if(DEFINED ENV{${_env_var}} AND EXISTS "$ENV{${_env_var}}")
        list(APPEND _fastrtps_PREFIX_HINTS "$ENV{${_env_var}}")
    endif()
endforeach()

if(WIN32)
    # eProsima Windows installer writes to "C:/Program Files/eProsima/fastrtps <ver>"
    # Try a broad glob-style list covering the most common version strings.
    foreach(_candidate
            "C:/Program Files/eProsima/fastrtps 2"
            "C:/Program Files/eProsima/fastrtps 2.6"
            "C:/Program Files/eProsima/fastrtps 2.10"
            "C:/Program Files/eProsima/fastrtps 2.11"
            "C:/Program Files/eProsima/fastrtps 2.12"
            "C:/Program Files/eProsima/fastrtps 2.13"
            "C:/Program Files/eProsima/fastrtps"
            "C:/eProsima/fastrtps"
    )
        if(EXISTS "${_candidate}")
            list(APPEND _fastrtps_PREFIX_HINTS "${_candidate}")
        endif()
    endforeach()
else()
    foreach(_candidate
            /usr/local
            /opt/ros/humble
            /opt/ros/iron
            /opt/ros/jazzy
    )
        if(EXISTS "${_candidate}")
            list(APPEND _fastrtps_PREFIX_HINTS "${_candidate}")
        endif()
    endforeach()
endif()

# ---------------------------------------------------------------------------
# 1. Try the upstream config-file package
# ---------------------------------------------------------------------------
find_package(fastrtps QUIET CONFIG
    HINTS
        "${fastrtps_DIR}"
        ${_fastrtps_PREFIX_HINTS}
    PATH_SUFFIXES
        cmake
        lib/cmake/fastrtps
        lib/cmake
        share/fastrtps/cmake
        share/cmake/fastrtps
)

if(fastrtps_FOUND)
    # Config-file succeeded — nothing more to do.
    cmake_policy(POP)
    return()
endif()

# ---------------------------------------------------------------------------
# 2. Manual header + library search (fallback)
# ---------------------------------------------------------------------------
set(_fastrtps_INCLUDE_SUFFIXES "")
foreach(_p ${_fastrtps_PREFIX_HINTS})
    list(APPEND _fastrtps_INCLUDE_SUFFIXES "${_p}/include")
endforeach()

find_path(fastrtps_INCLUDE_DIR
    NAMES
        fastrtps/fastrtps.h
        fastdds/dds/domain/DomainParticipant.hpp
    HINTS
        ${_fastrtps_INCLUDE_SUFFIXES}
    PATHS
        /usr/include
        /usr/local/include
    DOC "Fast DDS (fastrtps) include directory"
)

set(_fastrtps_LIB_SUFFIXES "")
foreach(_p ${_fastrtps_PREFIX_HINTS})
    list(APPEND _fastrtps_LIB_SUFFIXES "${_p}/lib")
endforeach()

find_library(fastrtps_LIBRARY
    NAMES fastrtps
    HINTS
        ${_fastrtps_LIB_SUFFIXES}
    PATHS
        /usr/lib
        /usr/local/lib
        /usr/lib/x86_64-linux-gnu
        /usr/lib/aarch64-linux-gnu
    DOC "Fast DDS (fastrtps) library"
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(fastrtps
    REQUIRED_VARS fastrtps_LIBRARY fastrtps_INCLUDE_DIR
    FAIL_MESSAGE
        "Could not find Fast DDS (fastrtps).  Install eProsima Fast DDS or set "
        "FASTRTPS_HOME / FASTDDS_HOME to the installation prefix, or point "
        "fastrtps_DIR at the directory containing fastrtpsConfig.cmake."
)

if(fastrtps_FOUND AND NOT TARGET fastrtps)
    add_library(fastrtps UNKNOWN IMPORTED)
    set_target_properties(fastrtps PROPERTIES
        IMPORTED_LOCATION             "${fastrtps_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${fastrtps_INCLUDE_DIR}"
    )
endif()

mark_as_advanced(fastrtps_INCLUDE_DIR fastrtps_LIBRARY)
cmake_policy(POP)
