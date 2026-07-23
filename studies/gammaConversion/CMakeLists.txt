#----------------------------------------------------------------------------
# Setup the project
cmake_minimum_required(VERSION 3.16...3.27)
project(gammaConversion)

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
# Locate sources and headers
#
include_directories(${PROJECT_SOURCE_DIR}/include
                    ${Geant4_INCLUDE_DIR})
file(GLOB sources ${PROJECT_SOURCE_DIR}/src/*.cc)
file(GLOB headers ${PROJECT_SOURCE_DIR}/include/*.hh)

#----------------------------------------------------------------------------
# Add the executable, and link it to the Geant4 libraries
#
add_executable(gammaConversion gammaConversion.cc ${sources} ${headers})
target_link_libraries(gammaConversion ${Geant4_LIBRARIES})

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
# Install the executable to 'bin' directory under CMAKE_INSTALL_PREFIX
#
install(TARGETS gammaConversion DESTINATION bin)
