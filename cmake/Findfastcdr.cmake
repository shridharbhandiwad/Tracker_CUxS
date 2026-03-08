# Findfastcdr.cmake
#
# Locates the eProsima Fast CDR serialization library (CMake package name:
# fastcdr).  Fast CDR is always co-installed with Fast DDS so the search
# paths mirror those used by Findfastrtps.cmake.
#
# Strategy
# --------
# 1. Delegate to the upstream config-file package (fastcdrConfig.cmake /
#    fastcdr-config.cmake) using every well-known installation prefix.
# 2. Fall back to a classic header + library search when the config file is
#    absent.
#
# The module honours the following environment / cache variables:
#
#   FASTRTPS_HOME   – root of a Fast DDS installation (fastcdr lives here too)
#   FASTDDS_HOME    – alias accepted by newer eProsima installers
#   fastcdr_DIR     – directory containing fastcdrConfig.cmake (CMake cache)
#
# Imported target created (when not already defined by the config file)
# -----------------------------------------------------------------------
#   fastcdr          – UNKNOWN IMPORTED target with include dirs + location
#
# Result variables
# ----------------
#   fastcdr_FOUND
#   fastcdr_INCLUDE_DIR
#   fastcdr_LIBRARY

cmake_policy(PUSH)
cmake_policy(SET CMP0057 NEW)   # IN_LIST support

# ---------------------------------------------------------------------------
# Helper: collect candidate prefixes from env vars and well-known paths
# ---------------------------------------------------------------------------
set(_fastcdr_PREFIX_HINTS)

foreach(_env_var FASTRTPS_HOME FASTDDS_HOME)
    if(DEFINED ENV{${_env_var}} AND EXISTS "$ENV{${_env_var}}")
        list(APPEND _fastcdr_PREFIX_HINTS "$ENV{${_env_var}}")
    endif()
endforeach()

if(WIN32)
    foreach(_candidate
            "C:/Program Files/eProsima/fastrtps 2"
            "C:/Program Files/eProsima/fastrtps 2.6"
            "C:/Program Files/eProsima/fastrtps 2.10"
            "C:/Program Files/eProsima/fastrtps 2.11"
            "C:/Program Files/eProsima/fastrtps 2.12"
            "C:/Program Files/eProsima/fastrtps 2.13"
            "C:/Program Files/eProsima/fastrtps"
            "C:/eProsima/fastrtps"
            "C:/Program Files/eProsima/fastcdr"
            "C:/eProsima/fastcdr"
    )
        if(EXISTS "${_candidate}")
            list(APPEND _fastcdr_PREFIX_HINTS "${_candidate}")
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
            list(APPEND _fastcdr_PREFIX_HINTS "${_candidate}")
        endif()
    endforeach()
endif()

# ---------------------------------------------------------------------------
# 1. Try the upstream config-file package
# ---------------------------------------------------------------------------
find_package(fastcdr QUIET CONFIG
    HINTS
        "${fastcdr_DIR}"
        ${_fastcdr_PREFIX_HINTS}
    PATH_SUFFIXES
        cmake
        lib/cmake/fastcdr
        lib/cmake
        share/fastcdr/cmake
        share/cmake/fastcdr
)

if(fastcdr_FOUND)
    cmake_policy(POP)
    return()
endif()

# ---------------------------------------------------------------------------
# 2. Manual header + library search (fallback)
# ---------------------------------------------------------------------------
set(_fastcdr_INCLUDE_SUFFIXES "")
foreach(_p ${_fastcdr_PREFIX_HINTS})
    list(APPEND _fastcdr_INCLUDE_SUFFIXES "${_p}/include")
endforeach()

find_path(fastcdr_INCLUDE_DIR
    NAMES
        fastcdr/Cdr.h
        fastcdr/cdr/fixed_size_string.hpp
    HINTS
        ${_fastcdr_INCLUDE_SUFFIXES}
    PATHS
        /usr/include
        /usr/local/include
    DOC "Fast CDR include directory"
)

set(_fastcdr_LIB_SUFFIXES "")
foreach(_p ${_fastcdr_PREFIX_HINTS})
    list(APPEND _fastcdr_LIB_SUFFIXES "${_p}/lib")
endforeach()

find_library(fastcdr_LIBRARY
    NAMES fastcdr
    HINTS
        ${_fastcdr_LIB_SUFFIXES}
    PATHS
        /usr/lib
        /usr/local/lib
        /usr/lib/x86_64-linux-gnu
        /usr/lib/aarch64-linux-gnu
    DOC "Fast CDR library"
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(fastcdr
    REQUIRED_VARS fastcdr_LIBRARY fastcdr_INCLUDE_DIR
    FAIL_MESSAGE
        "Could not find Fast CDR (fastcdr).  Install eProsima Fast DDS (which "
        "bundles fastcdr) or set FASTRTPS_HOME / FASTDDS_HOME to the "
        "installation prefix, or point fastcdr_DIR at the directory "
        "containing fastcdrConfig.cmake."
)

if(fastcdr_FOUND AND NOT TARGET fastcdr)
    add_library(fastcdr UNKNOWN IMPORTED)
    set_target_properties(fastcdr PROPERTIES
        IMPORTED_LOCATION             "${fastcdr_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${fastcdr_INCLUDE_DIR}"
    )
endif()

mark_as_advanced(fastcdr_INCLUDE_DIR fastcdr_LIBRARY)
cmake_policy(POP)
