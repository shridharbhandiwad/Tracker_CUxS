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
#   FASTRTPS_ROOT   – CMake-conventional alias for FASTRTPS_HOME
#   FASTDDS_ROOT    – CMake-conventional alias for FASTDDS_HOME
#   fastrtps_DIR    – directory containing fastrtpsConfig.cmake (CMake cache)
#
# Windows discovery
# -----------------
#   All subdirectories of %ProgramFiles%\eProsima\ and C:\eProsima\ matching
#   "fastrtps*" or "Fast DDS*" are probed automatically, so any installed
#   version (present or future) is found without hard-coding version numbers.
#   User-local (non-admin) installs under %LOCALAPPDATA%\eProsima\ are also
#   searched.
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

foreach(_env_var FASTRTPS_HOME FASTDDS_HOME FASTRTPS_ROOT FASTDDS_ROOT)
    if(DEFINED ENV{${_env_var}} AND EXISTS "$ENV{${_env_var}}")
        list(APPEND _fastrtps_PREFIX_HINTS "$ENV{${_env_var}}")
    endif()
endforeach()

if(WIN32)
    # Resolve %ProgramFiles% from the environment so non-C: installs are handled.
    set(_pf "$ENV{ProgramFiles}")
    if(NOT _pf)
        set(_pf "C:/Program Files")
    endif()
    file(TO_CMAKE_PATH "${_pf}" _pf)

    # Glob all installed Fast DDS / fastrtps versions under each eProsima base
    # directory.  This replaces a hard-coded version list and picks up any
    # present or future release automatically.
    foreach(_base "${_pf}/eProsima" "C:/eProsima")
        if(NOT IS_DIRECTORY "${_base}")
            continue()
        endif()
        file(GLOB _fastrtps_glob_dirs LIST_DIRECTORIES true
            "${_base}/fastrtps*"
            "${_base}/Fast DDS*"
        )
        foreach(_d ${_fastrtps_glob_dirs})
            if(IS_DIRECTORY "${_d}")
                list(APPEND _fastrtps_PREFIX_HINTS "${_d}")
            endif()
        endforeach()
        # Flat installs place config files directly under the eProsima base dir.
        list(APPEND _fastrtps_PREFIX_HINTS "${_base}")
    endforeach()
    unset(_pf)

    # User-local (non-admin) installs written to %LOCALAPPDATA%\eProsima\.
    if(DEFINED ENV{LOCALAPPDATA})
        file(TO_CMAKE_PATH "$ENV{LOCALAPPDATA}" _localappdata)
        if(IS_DIRECTORY "${_localappdata}/eProsima")
            file(GLOB _fastrtps_user_dirs LIST_DIRECTORIES true
                "${_localappdata}/eProsima/fastrtps*"
                "${_localappdata}/eProsima/Fast DDS*"
            )
            foreach(_d ${_fastrtps_user_dirs})
                if(IS_DIRECTORY "${_d}")
                    list(APPEND _fastrtps_PREFIX_HINTS "${_d}")
                endif()
            endforeach()
        endif()
        unset(_localappdata)
    endif()
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
            "$ENV{FASTDDS_ROOT}"
            "$ENV{FASTRTPS_HOME}"
            "$ENV{FASTRTPS_ROOT}"
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
            "eProsima Fast DDS was not found.\n"
            "  Download installer: https://www.eprosima.com/index.php/downloads-all\n"
            "  Docs (Windows):     https://fast-dds.docs.eprosima.com/en/latest/installation/binaries/windows_binaries.html\n"
            "  After installing, either:\n"
            "    - re-run cmake (the installer prefix is detected automatically), or\n"
            "    - set FASTRTPS_HOME / FASTDDS_HOME to the installation root, or\n"
            "    - pass -Dfastrtps_DIR=<dir> pointing at the folder containing fastrtpsConfig.cmake."
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
