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
#   FASTDDS_HOME    – alias accepted by newer eProsima installers (2.14+, 3.x)
#   FASTRTPS_ROOT   – CMake-conventional alias for FASTRTPS_HOME
#   FASTDDS_ROOT    – CMake-conventional alias for FASTDDS_HOME
#   fastcdr_DIR     – directory containing fastcdrConfig.cmake (CMake cache)
#
# Windows discovery
# -----------------
#   All subdirectories of %ProgramFiles%\eProsima\ and C:\eProsima\ matching
#   "fastrtps*", "Fast DDS*", or "fastcdr*" are probed automatically.
#   User-local (non-admin) installs under %LOCALAPPDATA%\eProsima\ are also
#   searched.
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

foreach(_env_var FASTRTPS_HOME FASTDDS_HOME FASTRTPS_ROOT FASTDDS_ROOT)
    if(DEFINED ENV{${_env_var}} AND EXISTS "$ENV{${_env_var}}")
        list(APPEND _fastcdr_PREFIX_HINTS "$ENV{${_env_var}}")
    endif()
endforeach()

if(WIN32)
    # Resolve %ProgramFiles% from the environment so non-C: installs are handled.
    set(_pf "$ENV{ProgramFiles}")
    if(NOT _pf)
        set(_pf "C:/Program Files")
    endif()
    file(TO_CMAKE_PATH "${_pf}" _pf)

    # Glob all installed Fast DDS / fastrtps / fastcdr versions under each
    # eProsima base directory.  fastcdr can be installed standalone or bundled
    # inside the Fast DDS installer tree.
    foreach(_base "${_pf}/eProsima" "C:/eProsima")
        if(NOT IS_DIRECTORY "${_base}")
            continue()
        endif()
        file(GLOB _fastcdr_glob_dirs LIST_DIRECTORIES true
            "${_base}/fastrtps*"
            "${_base}/Fast DDS*"
            "${_base}/fastcdr*"
        )
        foreach(_d ${_fastcdr_glob_dirs})
            if(IS_DIRECTORY "${_d}")
                list(APPEND _fastcdr_PREFIX_HINTS "${_d}")
            endif()
        endforeach()
        list(APPEND _fastcdr_PREFIX_HINTS "${_base}")
    endforeach()
    unset(_pf)

    # User-local (non-admin) installs written to %LOCALAPPDATA%\eProsima\.
    if(DEFINED ENV{LOCALAPPDATA})
        file(TO_CMAKE_PATH "$ENV{LOCALAPPDATA}" _localappdata)
        if(IS_DIRECTORY "${_localappdata}/eProsima")
            file(GLOB _fastcdr_user_dirs LIST_DIRECTORIES true
                "${_localappdata}/eProsima/fastrtps*"
                "${_localappdata}/eProsima/Fast DDS*"
                "${_localappdata}/eProsima/fastcdr*"
            )
            foreach(_d ${_fastcdr_user_dirs})
                if(IS_DIRECTORY "${_d}")
                    list(APPEND _fastcdr_PREFIX_HINTS "${_d}")
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
            list(APPEND _fastcdr_PREFIX_HINTS "${_candidate}")
        endif()
    endforeach()
endif()

# ---------------------------------------------------------------------------
# 1. Try the upstream config-file package
# ---------------------------------------------------------------------------

set(_fastcdr_CMAKE_SUFFIXES
    cmake
    lib/cmake/fastcdr
    lib/cmake
    share/fastcdr/cmake
    share/cmake/fastcdr
)

foreach(_p ${_fastcdr_PREFIX_HINTS})
    foreach(_sub
            "lib/x64/Release"
            "lib/x64/Debug"
            "lib/x64"
            "lib/Win32/Release"
            "lib/Win32/Debug"
            "lib/Win32"
    )
        if(EXISTS "${_p}/${_sub}")
            list(APPEND _fastcdr_CMAKE_SUFFIXES "${_sub}/cmake" "${_sub}")
        endif()
    endforeach()
endforeach()

find_package(fastcdr QUIET CONFIG
    HINTS
        "${fastcdr_DIR}"
        ${_fastcdr_PREFIX_HINTS}
    PATH_SUFFIXES
        ${_fastcdr_CMAKE_SUFFIXES}
)

if(fastcdr_FOUND)
    cmake_policy(POP)
    return()
endif()

# ---------------------------------------------------------------------------
# 2. Manual header + library search (fallback)
# ---------------------------------------------------------------------------
set(_fastcdr_INCLUDE_HINTS "")
set(_fastcdr_LIB_HINTS "")

foreach(_p ${_fastcdr_PREFIX_HINTS})
    list(APPEND _fastcdr_INCLUDE_HINTS "${_p}/include")
    list(APPEND _fastcdr_LIB_HINTS
        "${_p}/lib"
        "${_p}/lib/x64/Release"
        "${_p}/lib/x64/Debug"
        "${_p}/lib/x64"
        "${_p}/lib/Win32/Release"
        "${_p}/lib/Win32/Debug"
        "${_p}/lib/Win32"
    )
endforeach()

find_path(fastcdr_INCLUDE_DIR
    NAMES
        fastcdr/Cdr.h
        fastcdr/cdr/fixed_size_string.hpp
    HINTS
        ${_fastcdr_INCLUDE_HINTS}
    PATHS
        /usr/include
        /usr/local/include
        /opt/homebrew/include
    DOC "Fast CDR include directory"
)

find_library(fastcdr_LIBRARY
    NAMES
        fastcdr
        fastcdr-2       # some builds suffix with API version
    HINTS
        ${_fastcdr_LIB_HINTS}
    PATHS
        /usr/lib
        /usr/local/lib
        /opt/homebrew/lib
        /usr/lib/x86_64-linux-gnu
        /usr/lib/aarch64-linux-gnu
    DOC "Fast CDR library"
)

include(FindPackageHandleStandardArgs)
if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.21")
    find_package_handle_standard_args(fastcdr
        REQUIRED_VARS fastcdr_LIBRARY fastcdr_INCLUDE_DIR
        REASON_FAILURE_MESSAGE
            "eProsima Fast CDR was not found (it is bundled with Fast DDS).\n"
            "  Download installer: https://www.eprosima.com/index.php/downloads-all\n"
            "  After installing, either:\n"
            "    - re-run cmake (the installer prefix is detected automatically), or\n"
            "    - set FASTRTPS_HOME / FASTDDS_HOME to the Fast DDS installation root, or\n"
            "    - pass -Dfastcdr_DIR=<dir> pointing at the folder containing fastcdrConfig.cmake."
    )
else()
    find_package_handle_standard_args(fastcdr
        REQUIRED_VARS fastcdr_LIBRARY fastcdr_INCLUDE_DIR
    )
endif()

if(fastcdr_FOUND AND NOT TARGET fastcdr)
    add_library(fastcdr UNKNOWN IMPORTED)
    set_target_properties(fastcdr PROPERTIES
        IMPORTED_LOCATION             "${fastcdr_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${fastcdr_INCLUDE_DIR}"
    )
endif()

mark_as_advanced(fastcdr_INCLUDE_DIR fastcdr_LIBRARY)
cmake_policy(POP)
