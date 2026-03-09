# Findfastrtps.cmake
#
# Locates the eProsima Fast DDS library (CMake package names: fastrtps / fastdds).
#
# Strategy
# --------
# 1. Delegate to the upstream config-file package (fastrtpsConfig.cmake or
#    fastddsConfig.cmake) using every well-known installation prefix so the
#    installed targets and version information are used when available.
# 2. Fall back to a classic header + library search when the config file is
#    absent (e.g. a custom build tree, missing installer step, etc.).
#
# The module honours the following environment / cache variables as extra
# search prefixes in both modes:
#
#   FASTRTPS_HOME   – root of a Fast DDS 2.x installation
#   FASTDDS_HOME    – alias accepted by newer eProsima installers (2.14+, 3.x)
#   fastrtps_DIR    – directory containing fastrtpsConfig.cmake (CMake cache)
#
# Windows eProsima installer paths supported
# ------------------------------------------
#   Fast DDS 2.x  → "C:\Program Files\eProsima\fastrtps <ver>"
#   Fast DDS 2.14+→ may also use  "C:\Program Files\eProsima\Fast DDS <ver>"
#   Fast DDS 3.x  → "C:\Program Files\eProsima\Fast DDS <ver>"
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
    # eProsima Windows installer writes to one of two naming schemes:
    #   "C:/Program Files/eProsima/fastrtps <ver>"  (2.x legacy)
    #   "C:/Program Files/eProsima/Fast DDS <ver>"  (2.14+ / 3.x)
    foreach(_candidate
            "C:/Program Files/eProsima/fastrtps 2"
            "C:/Program Files/eProsima/fastrtps 2.6"
            "C:/Program Files/eProsima/fastrtps 2.10"
            "C:/Program Files/eProsima/fastrtps 2.11"
            "C:/Program Files/eProsima/fastrtps 2.12"
            "C:/Program Files/eProsima/fastrtps 2.13"
            "C:/Program Files/eProsima/fastrtps 2.14"
            "C:/Program Files/eProsima/fastrtps"
            "C:/Program Files/eProsima/Fast DDS 2"
            "C:/Program Files/eProsima/Fast DDS 2.14"
            "C:/Program Files/eProsima/Fast DDS 2.14.0"
            "C:/Program Files/eProsima/Fast DDS 3"
            "C:/Program Files/eProsima/Fast DDS 3.0"
            "C:/Program Files/eProsima/Fast DDS 3.0.0"
            "C:/Program Files/eProsima/Fast DDS 3.1"
            "C:/Program Files/eProsima/Fast DDS 3.1.0"
            "C:/Program Files/eProsima/Fast DDS 3.2"
            "C:/Program Files/eProsima/Fast DDS 3.2.0"
            "C:/Program Files/eProsima/Fast DDS"
            "C:/eProsima/fastrtps"
            "C:/eProsima/Fast DDS"
    )
        if(EXISTS "${_candidate}")
            list(APPEND _fastrtps_PREFIX_HINTS "${_candidate}")
        endif()
    endforeach()
else()
    foreach(_candidate
            /usr/local
            /opt/homebrew
            /opt/ros/humble
            /opt/ros/iron
            /opt/ros/jazzy
            /opt/ros/rolling
    )
        if(EXISTS "${_candidate}")
            list(APPEND _fastrtps_PREFIX_HINTS "${_candidate}")
        endif()
    endforeach()
endif()

# ---------------------------------------------------------------------------
# 1. Try the upstream config-file package
#    Fast DDS 2.x ships "fastrtpsConfig.cmake"; 3.x ships "fastddsConfig.cmake"
#    (which itself re-exports the fastrtps target for backward compat).
#    Try both, starting with the legacy name so version-pinned projects work.
# ---------------------------------------------------------------------------

# Common PATH_SUFFIXES used by eProsima installers
set(_fastrtps_CMAKE_SUFFIXES
    cmake
    lib/cmake/fastrtps
    lib/cmake/fastdds
    lib/cmake
    share/fastrtps/cmake
    share/fastdds/cmake
    share/cmake/fastrtps
    share/cmake/fastdds
)

# Also expand per-prefix subdirectories that Windows installers sometimes use:
#   <prefix>/lib/x64/Release/cmake  etc.
foreach(_p ${_fastrtps_PREFIX_HINTS})
    foreach(_sub
            "lib/x64/Release"
            "lib/x64/Debug"
            "lib/x64"
            "lib/Win32/Release"
            "lib/Win32/Debug"
            "lib/Win32"
    )
        if(EXISTS "${_p}/${_sub}")
            list(APPEND _fastrtps_CMAKE_SUFFIXES "${_sub}/cmake" "${_sub}")
        endif()
    endforeach()
endforeach()

find_package(fastrtps QUIET CONFIG
    HINTS
        "${fastrtps_DIR}"
        ${_fastrtps_PREFIX_HINTS}
    PATH_SUFFIXES
        ${_fastrtps_CMAKE_SUFFIXES}
)

if(fastrtps_FOUND)
    cmake_policy(POP)
    return()
endif()

# Fast DDS 3.x config package is named "fastdds", but still exports a
# "fastrtps" interface alias for backward compatibility.
if(NOT fastrtps_FOUND)
    find_package(fastdds QUIET CONFIG
        HINTS
            "$ENV{FASTDDS_HOME}"
            "$ENV{FASTRTPS_HOME}"
            ${_fastrtps_PREFIX_HINTS}
        PATH_SUFFIXES
            ${_fastrtps_CMAKE_SUFFIXES}
    )
    if(fastdds_FOUND)
        # Promote fastdds variables / targets so the rest of the build sees "fastrtps"
        set(fastrtps_FOUND TRUE)
        if(NOT TARGET fastrtps AND TARGET fastdds)
            add_library(fastrtps ALIAS fastdds)
        endif()
        cmake_policy(POP)
        return()
    endif()
endif()

# ---------------------------------------------------------------------------
# 2. Manual header + library search (fallback for non-config-file installs)
# ---------------------------------------------------------------------------
set(_fastrtps_INCLUDE_HINTS "")
set(_fastrtps_LIB_HINTS "")

foreach(_p ${_fastrtps_PREFIX_HINTS})
    list(APPEND _fastrtps_INCLUDE_HINTS "${_p}/include")
    list(APPEND _fastrtps_LIB_HINTS
        "${_p}/lib"
        "${_p}/lib/x64/Release"
        "${_p}/lib/x64/Debug"
        "${_p}/lib/x64"
        "${_p}/lib/Win32/Release"
        "${_p}/lib/Win32/Debug"
        "${_p}/lib/Win32"
    )
endforeach()

find_path(fastrtps_INCLUDE_DIR
    NAMES
        fastrtps/fastrtps.h
        fastdds/dds/domain/DomainParticipant.hpp
    HINTS
        ${_fastrtps_INCLUDE_HINTS}
    PATHS
        /usr/include
        /usr/local/include
        /opt/homebrew/include
    DOC "Fast DDS (fastrtps) include directory"
)

find_library(fastrtps_LIBRARY
    NAMES
        fastrtps
        fastdds          # Fast DDS 3.x renamed the shared lib
        fastrtps-2
    HINTS
        ${_fastrtps_LIB_HINTS}
    PATHS
        /usr/lib
        /usr/local/lib
        /opt/homebrew/lib
        /usr/lib/x86_64-linux-gnu
        /usr/lib/aarch64-linux-gnu
    DOC "Fast DDS (fastrtps) library"
)

include(FindPackageHandleStandardArgs)
if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.21")
    find_package_handle_standard_args(fastrtps
        REQUIRED_VARS fastrtps_LIBRARY fastrtps_INCLUDE_DIR
        REASON_FAILURE_MESSAGE
            "Install eProsima Fast DDS or set FASTRTPS_HOME / FASTDDS_HOME to the installation prefix, or point fastrtps_DIR at the directory containing fastrtpsConfig.cmake."
    )
else()
    find_package_handle_standard_args(fastrtps
        REQUIRED_VARS fastrtps_LIBRARY fastrtps_INCLUDE_DIR
    )
endif()

if(fastrtps_FOUND AND NOT TARGET fastrtps)
    add_library(fastrtps UNKNOWN IMPORTED)
    set_target_properties(fastrtps PROPERTIES
        IMPORTED_LOCATION             "${fastrtps_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${fastrtps_INCLUDE_DIR}"
    )
endif()

mark_as_advanced(fastrtps_INCLUDE_DIR fastrtps_LIBRARY)
cmake_policy(POP)
