#----------------------------------------------------------------------------
# Setup the project
cmake_minimum_required(VERSION 3.16...3.27)
project(gammaConversionSi)

#----------------------------------------------------------------------------
# Find Geant4 package, activating all available UI and Vis drivers by default
# You can set WITH_GEANT4_UIVIS to OFF via the command line or ccmake/cmake-gui
# to build a batch mode only executable
#
option(WITH_GEANT4_UIVIS "Build with Geant4 UI and Vis drivers" ON)
if(WITH_GEANT4_UIVIS)
  find_package(Geant4 REQUIRED ui_all vis_all)
else()
  find_package(Geant4 REQUIRED)
endif()

#----------------------------------------------------------------------------
# Setup Geant4 include directories and compile definitions
#
include(${Geant4_USE_FILE})

#----------------------------------------------------------------------------
# ONNX Runtime, used by the normalising-flow fast-simulation model
# (ConversionFlowInference). Homebrew ships neither a CMake config nor a .pc
# file, so locate the header and library directly. Install with
#   brew install onnxruntime
#
find_path(ONNXRuntime_INCLUDE_DIR onnxruntime_cxx_api.h
  HINTS /opt/homebrew/opt/onnxruntime/include /opt/homebrew/include /usr/local/include
  PATH_SUFFIXES onnxruntime)
find_library(ONNXRuntime_LIBRARY onnxruntime
  HINTS /opt/homebrew/opt/onnxruntime/lib /opt/homebrew/lib /usr/local/lib)

if(NOT ONNXRuntime_INCLUDE_DIR OR NOT ONNXRuntime_LIBRARY)
  message(FATAL_ERROR
    "ONNX Runtime not found (needed by the flow fast-sim model). "
    "Install it with 'brew install onnxruntime', or pass "
    "-DONNXRuntime_INCLUDE_DIR=<dir> -DONNXRuntime_LIBRARY=<lib>.")
endif()
message(STATUS "ONNX Runtime: ${ONNXRuntime_LIBRARY}")

#----------------------------------------------------------------------------
# Locate sources and headers
#
include_directories(${PROJECT_SOURCE_DIR}/include
                    ${Geant4_INCLUDE_DIR})
file(GLOB sources ${PROJECT_SOURCE_DIR}/src/*.cc)
file(GLOB headers ${PROJECT_SOURCE_DIR}/include/*.hh)

#----------------------------------------------------------------------------
# Add the executable, and link it to the Geant4 libraries
#
add_executable(gammaConversionSi gammaConversionSi.cc ${sources} ${headers})
target_include_directories(gammaConversionSi PRIVATE ${ONNXRuntime_INCLUDE_DIR})
target_link_libraries(gammaConversionSi ${Geant4_LIBRARIES} ${ONNXRuntime_LIBRARY})

#----------------------------------------------------------------------------
# Copy the vis macro and the run configurations to the build directory, so
# that the executable can be run directly from there
#
configure_file(
  ${PROJECT_SOURCE_DIR}/vis.mac
  ${PROJECT_BINARY_DIR}/vis.mac
  COPYONLY
  )

file(GLOB configs ${PROJECT_SOURCE_DIR}/config/*.cfg)
foreach(_config ${configs})
  get_filename_component(_name ${_config} NAME)
  configure_file(
    ${_config}
    ${PROJECT_BINARY_DIR}/config/${_name}
    COPYONLY
    )
endforeach()

#----------------------------------------------------------------------------
# Copy the exported flow (ONNX graphs + constants) next to the executable, so
# the default flowModelDir "models/onnx" resolves from the build directory.
# Run training/export_flow_onnx.py before configuring if this is empty.
#
file(GLOB flow_models ${PROJECT_SOURCE_DIR}/training/models/onnx/*)
foreach(_model ${flow_models})
  get_filename_component(_name ${_model} NAME)
  configure_file(
    ${_model}
    ${PROJECT_BINARY_DIR}/models/onnx/${_name}
    COPYONLY
    )
endforeach()

#----------------------------------------------------------------------------
# Copy the benchmark configs and scripts to the build directory too
#
file(GLOB benchmark_cfgs ${PROJECT_SOURCE_DIR}/benchmark/*.cfg)
foreach(_cfg ${benchmark_cfgs})
  get_filename_component(_name ${_cfg} NAME)
  configure_file(${_cfg} ${PROJECT_BINARY_DIR}/benchmark/${_name} COPYONLY)
endforeach()

#----------------------------------------------------------------------------
# Install the executable to 'bin' directory under CMAKE_INSTALL_PREFIX
#
install(TARGETS gammaConversionSi DESTINATION bin)
