cmake_minimum_required(VERSION 3.14)
project(CounterUAS_RadarTracker VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/W4 /WX- /MP)
    add_definitions(-D_CRT_SECURE_NO_WARNINGS -DNOMINMAX -DWIN32_LEAN_AND_MEAN)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -O2)
endif()

# ---------------------------------------------------------------------------
# CMake module path — cmake/ contains Findfastrtps.cmake / Findfastcdr.cmake
# fallback modules for platforms where Fast DDS is not installed to a
# standard system prefix (e.g. a user-local Windows installation).
# ---------------------------------------------------------------------------
list(APPEND CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake")

# Probe eProsima Fast DDS installation prefixes so that
# find_package(fastrtps) / find_package(fastcdr) can locate the upstream
# config files without requiring the user to set CMAKE_PREFIX_PATH manually.
#
# Override / extend by passing -DCMAKE_PREFIX_PATH=<path> to cmake, or by
# setting FASTRTPS_HOME, FASTDDS_HOME, FASTRTPS_ROOT, or FASTDDS_ROOT.
if(WIN32)
    # Env-var hints (any of the four conventional names is accepted).
    foreach(_env_var FASTRTPS_HOME FASTDDS_HOME FASTRTPS_ROOT FASTDDS_ROOT)
        if(DEFINED ENV{${_env_var}} AND EXISTS "$ENV{${_env_var}}")
            list(APPEND CMAKE_PREFIX_PATH "$ENV{${_env_var}}")
        endif()
    endforeach()

    # Resolve %ProgramFiles% so the path is correct on non-C: systems.
    set(_pf "$ENV{ProgramFiles}")
    if(NOT _pf)
        set(_pf "C:/Program Files")
    endif()
    file(TO_CMAKE_PATH "${_pf}" _pf)

    # Glob all installed Fast DDS / fastrtps versions automatically rather than
    # maintaining a hard-coded version list that goes stale with each release.
    foreach(_base "${_pf}/eProsima" "C:/eProsima")
        if(NOT IS_DIRECTORY "${_base}")
            continue()
        endif()
        file(GLOB _cml_eprosima_dirs LIST_DIRECTORIES true
            "${_base}/fastrtps*"
            "${_base}/Fast DDS*"
        )
        foreach(_d ${_cml_eprosima_dirs})
            if(IS_DIRECTORY "${_d}")
                list(APPEND CMAKE_PREFIX_PATH "${_d}")
            endif()
        endforeach()
        list(APPEND CMAKE_PREFIX_PATH "${_base}")
    endforeach()
    unset(_pf)

    # User-local (non-admin) installs.
    if(DEFINED ENV{LOCALAPPDATA})
        file(TO_CMAKE_PATH "$ENV{LOCALAPPDATA}" _localappdata)
        if(IS_DIRECTORY "${_localappdata}/eProsima")
            file(GLOB _cml_user_dirs LIST_DIRECTORIES true
                "${_localappdata}/eProsima/fastrtps*"
                "${_localappdata}/eProsima/Fast DDS*"
            )
            foreach(_d ${_cml_user_dirs})
                if(IS_DIRECTORY "${_d}")
                    list(APPEND CMAKE_PREFIX_PATH "${_d}")
                endif()
            endforeach()
        endif()
        unset(_localappdata)
    endif()
else()
    foreach(_prefix
            "$ENV{FASTRTPS_HOME}"
            "$ENV{FASTDDS_HOME}"
            "$ENV{FASTRTPS_ROOT}"
            "$ENV{FASTDDS_ROOT}"
            "/usr/local"
            "/opt/homebrew"
            "/opt/ros/humble"
            "/opt/ros/iron"
            "/opt/ros/jazzy"
            "/opt/ros/rolling"
    )
        if(EXISTS "${_prefix}")
            list(APPEND CMAKE_PREFIX_PATH "${_prefix}")
        endif()
    endforeach()
endif()

# ---------------------------------------------------------------------------
# Fast DDS (eProsima): required for IDL-generated types and DDS pub/sub
# ---------------------------------------------------------------------------
find_package(fastrtps REQUIRED)   # cmake package name for Fast DDS 2.x
find_package(fastcdr  REQUIRED)

# ---------------------------------------------------------------------------
# IDL code generation — fastddsgen reads idl/messages.idl and writes into
# ${CMAKE_BINARY_DIR}/generated/ at configure time (so downstream targets
# can depend on the generated files).
# ---------------------------------------------------------------------------
# On Windows the eProsima installer ships fastddsgen as a .bat wrapper;
# searching for "fastddsgen" finds it on PATH, but we also probe all known
# version-specific bin/ directories so an un-PATH'd install still works.
if(WIN32)
    set(_fastddsgen_names fastddsgen fastddsgen.bat)
else()
    set(_fastddsgen_names fastddsgen)
endif()

find_program(FASTDDSGEN_BIN
    NAMES ${_fastddsgen_names}
    HINTS
        "$ENV{FASTRTPS_HOME}/bin"
        "$ENV{FASTDDS_HOME}/bin"
        "C:/Program Files/eProsima/fastrtps 2/bin"
        "C:/Program Files/eProsima/fastrtps 2.6/bin"
        "C:/Program Files/eProsima/fastrtps 2.10/bin"
        "C:/Program Files/eProsima/fastrtps 2.11/bin"
        "C:/Program Files/eProsima/fastrtps 2.12/bin"
        "C:/Program Files/eProsima/fastrtps 2.13/bin"
        "C:/Program Files/eProsima/fastrtps 2.14/bin"
        "C:/Program Files/eProsima/fastrtps/bin"
        "C:/Program Files/eProsima/Fast DDS 2/bin"
        "C:/Program Files/eProsima/Fast DDS 2.14/bin"
        "C:/Program Files/eProsima/Fast DDS 2.14.0/bin"
        "C:/Program Files/eProsima/Fast DDS 3/bin"
        "C:/Program Files/eProsima/Fast DDS 3.0/bin"
        "C:/Program Files/eProsima/Fast DDS 3.0.0/bin"
        "C:/Program Files/eProsima/Fast DDS 3.1/bin"
        "C:/Program Files/eProsima/Fast DDS 3.1.0/bin"
        "C:/Program Files/eProsima/Fast DDS 3.2/bin"
        "C:/Program Files/eProsima/Fast DDS 3.2.0/bin"
        "C:/Program Files/eProsima/Fast DDS/bin"
        "C:/eProsima/fastrtps/bin"
        "C:/eProsima/Fast DDS/bin"
        /usr/bin
        /usr/local/bin
)
if(NOT FASTDDSGEN_BIN)
    message(FATAL_ERROR
        "fastddsgen not found.  Install eProsima Fast DDS tools and ensure "
        "its bin/ directory is on PATH, or set FASTRTPS_HOME / FASTDDS_HOME "
        "to the Fast DDS installation prefix.")
endif()
message(STATUS "fastddsgen: ${FASTDDSGEN_BIN}")

set(IDL_FILE    "${CMAKE_SOURCE_DIR}/idl/messages.idl")
set(IDL_GEN_DIR "${CMAKE_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${IDL_GEN_DIR}")

set(IDL_GEN_SRCS
    "${IDL_GEN_DIR}/messages.cxx"
    "${IDL_GEN_DIR}/messagesPubSubTypes.cxx"
)
set(IDL_GEN_HDRS
    "${IDL_GEN_DIR}/messages.h"
    "${IDL_GEN_DIR}/messagesPubSubTypes.h"
)

add_custom_command(
    OUTPUT  ${IDL_GEN_SRCS} ${IDL_GEN_HDRS}
    COMMAND ${FASTDDSGEN_BIN} -replace -d "${IDL_GEN_DIR}" "${IDL_FILE}"
    DEPENDS "${IDL_FILE}"
    COMMENT "Generating DDS types from idl/messages.idl via fastddsgen"
    VERBATIM
)
add_custom_target(generate_idl DEPENDS ${IDL_GEN_SRCS} ${IDL_GEN_HDRS})

# ---------------------------------------------------------------------------
# cuas_idl — thin library wrapping the fastddsgen output
# Every component that needs IDL types links against this.
# ---------------------------------------------------------------------------
add_library(cuas_idl STATIC ${IDL_GEN_SRCS})
add_dependencies(cuas_idl generate_idl)
target_include_directories(cuas_idl PUBLIC
    "${IDL_GEN_DIR}"
    ${CMAKE_SOURCE_DIR}/include   # for transitivity only
)
target_link_libraries(cuas_idl PUBLIC fastrtps fastcdr)

# Platform libraries
if(WIN32)
    set(PLATFORM_LIBS ws2_32)
else()
    set(PLATFORM_LIBS pthread)
endif()

include_directories(${CMAKE_SOURCE_DIR}/include)

# ---------------------------------------------------------------------------
# Common library
# ---------------------------------------------------------------------------
add_library(cuas_common STATIC
    src/common/config.cpp
    src/common/logger.cpp
    src/common/udp_socket.cpp
    src/common/dds_participant.cpp
)
target_link_libraries(cuas_common PUBLIC cuas_idl ${PLATFORM_LIBS})

# ---------------------------------------------------------------------------
# Receiver library
# ---------------------------------------------------------------------------
add_library(cuas_receiver STATIC
    src/receiver/detection_receiver.cpp
)
target_link_libraries(cuas_receiver PUBLIC cuas_common)

# ---------------------------------------------------------------------------
# Preprocessing library
# ---------------------------------------------------------------------------
add_library(cuas_preprocessing STATIC
    src/preprocessing/preprocessor.cpp
)
target_link_libraries(cuas_preprocessing PUBLIC cuas_common)

# ---------------------------------------------------------------------------
# Clustering library
# ---------------------------------------------------------------------------
add_library(cuas_clustering STATIC
    src/clustering/cluster_engine.cpp
    src/clustering/dbscan_clusterer.cpp
    src/clustering/range_clusterer.cpp
    src/clustering/range_strength_clusterer.cpp
)
target_link_libraries(cuas_clustering PUBLIC cuas_common)

# ---------------------------------------------------------------------------
# Prediction library
# ---------------------------------------------------------------------------
add_library(cuas_prediction STATIC
    src/prediction/cv_model.cpp
    src/prediction/ca_model.cpp
    src/prediction/ctr_model.cpp
    src/prediction/imm_filter.cpp
)
target_link_libraries(cuas_prediction PUBLIC cuas_common)

# ---------------------------------------------------------------------------
# Association library
# ---------------------------------------------------------------------------
add_library(cuas_association STATIC
    src/association/association_engine.cpp
    src/association/mahalanobis_associator.cpp
    src/association/gnn_associator.cpp
    src/association/jpda_associator.cpp
)
target_link_libraries(cuas_association PUBLIC cuas_common cuas_prediction)

# ---------------------------------------------------------------------------
# Track management library
# ---------------------------------------------------------------------------
add_library(cuas_track_management STATIC
    src/track_management/track.cpp
    src/track_management/track_initiator.cpp
    src/track_management/track_manager.cpp
)
target_link_libraries(cuas_track_management PUBLIC
    cuas_common
    cuas_preprocessing
    cuas_clustering
    cuas_prediction
    cuas_association
)

# ---------------------------------------------------------------------------
# Sender library
# ---------------------------------------------------------------------------
add_library(cuas_sender STATIC
    src/sender/track_sender.cpp
)
target_link_libraries(cuas_sender PUBLIC cuas_common)

# ---------------------------------------------------------------------------
# Pipeline library
# ---------------------------------------------------------------------------
add_library(cuas_pipeline STATIC
    src/pipeline/tracker_pipeline.cpp
)
target_link_libraries(cuas_pipeline PUBLIC
    cuas_common
    cuas_receiver
    cuas_track_management
    cuas_sender
)

# ---------------------------------------------------------------------------
# Main tracker executable
# ---------------------------------------------------------------------------
add_executable(cuas_tracker src/main.cpp)
target_link_libraries(cuas_tracker PRIVATE cuas_pipeline)

add_custom_command(TARGET cuas_tracker POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:cuas_tracker>/config"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/config/tracker_config.json"
        "$<TARGET_FILE_DIR:cuas_tracker>/config/tracker_config.json"
    COMMENT "Copying tracker_config.json to output directory"
)

# ---------------------------------------------------------------------------
# Simulators
# ---------------------------------------------------------------------------
add_executable(dsp_injector simulators/dsp_injector/dsp_injector.cpp)
target_link_libraries(dsp_injector PRIVATE cuas_common)

add_executable(display_module simulators/display_module/display_module.cpp)
target_link_libraries(display_module PRIVATE cuas_common)

add_executable(log_extractor simulators/log_extractor/log_extractor.cpp)
target_link_libraries(log_extractor PRIVATE cuas_common)
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_link_libraries(log_extractor PRIVATE stdc++fs)
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
enable_testing()

add_executable(test_log_som_eom tests/test_log_som_eom.cpp)
target_link_libraries(test_log_som_eom PRIVATE cuas_common)
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_link_libraries(test_log_som_eom PRIVATE stdc++fs)
endif()
add_test(NAME LogSomEomFraming COMMAND test_log_som_eom)

# ---------------------------------------------------------------------------
# Install
# ---------------------------------------------------------------------------
install(TARGETS cuas_tracker dsp_injector display_module log_extractor
        RUNTIME DESTINATION bin)
install(FILES config/tracker_config.json DESTINATION config)
# IDL file installed alongside binaries so integrators can generate bindings
# for any language without reading implementation headers.
install(FILES idl/messages.idl DESTINATION idl)

message(STATUS "=================================================")
message(STATUS "  Counter-UAS Radar Tracker v${PROJECT_VERSION}")
message(STATUS "  Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  Platform:   ${CMAKE_SYSTEM_NAME}")
message(STATUS "  IDL codegen: ${FASTDDSGEN_BIN}")
message(STATUS "=================================================")
