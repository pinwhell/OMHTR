cmake_minimum_required(VERSION 3.15)

project(OMHTRHPPWriter)

find_package(MHTR REQUIRED PATHS ${MHTR_DIR})

if(PLUGIN_DIR)

set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${PLUGIN_DIR})
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PLUGIN_DIR})

endif()

add_library(OMHTRHPPWriter SHARED src/PluginMain.cpp)
target_include_directories(OMHTRHPPWriter PUBLIC include)
target_link_libraries(OMHTRHPPWriter MHTR::MHTRPluginSDK MHTR::Synthesizer MHTR::MetadataSynthers MHTR::Json MHTR::MHTRRandom)
