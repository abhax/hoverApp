cmake_minimum_required(VERSION 3.10)

# Set some basic project attributes
project (hoverApp
	VERSION 0.1
	DESCRIPTION "hovercraft application")

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Proper RPATH handling for cross-compilation
set(CMAKE_SKIP_RPATH TRUE)
set(CMAKE_SKIP_BUILD_RPATH TRUE)
set(CMAKE_SKIP_INSTALL_RPATH TRUE)

# This project will output an executable file
set(SOURCES 
	#hoverApp.cpp
	#Video/src/videoStream.cpp
	Test/bluetooth_test.c
	#Test/Graphics/src/graphics.cpp
	#Test/Graphics/src/opengl.cpp
	)

# Use proper lib directory handling instead of hardcoded paths
set(CMAKE_LIBRARY_PATH ${CMAKE_LIBRARY_PATH} ${CMAKE_CURRENT_SOURCE_DIR}/lib)

# Link to system libraries without hardcoded paths
find_package(Threads REQUIRED)
set(LIBRARIES ${CMAKE_THREAD_LIBS_INIT} bluetooth)

add_executable(${PROJECT_NAME} ${SOURCES})
target_link_libraries(${PROJECT_NAME} ${LIBRARIES})

# Create a simple configuration header
configure_file(config.h.in config.h)

# Include the configuration header in the build
target_include_directories(${PROJECT_NAME} PUBLIC Video/inc/)
target_include_directories(${PROJECT_NAME} PUBLIC Test/Graphics/inc/)
target_include_directories(${PROJECT_NAME} PUBLIC "${PROJECT_BINARY_DIR}")

