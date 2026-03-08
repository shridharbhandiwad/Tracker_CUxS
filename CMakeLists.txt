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
# Fast DDS (eProsima): required for IDL-generated types and DDS pub/sub
# ---------------------------------------------------------------------------
find_package(fastrtps REQUIRED)   # cmake package name for Fast DDS 2.x
find_package(fastcdr  REQUIRED)

# ---------------------------------------------------------------------------
# IDL code generation — fastddsgen reads idl/messages.idl and writes into
# ${CMAKE_BINARY_DIR}/generated/ at configure time (so downstream targets
# can depend on the generated files).
# ---------------------------------------------------------------------------
find_program(FASTDDSGEN_BIN fastddsgen REQUIRED
    HINTS /usr/bin /usr/local/bin)
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
