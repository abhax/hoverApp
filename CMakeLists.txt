cmake_minimum_required(VERSION 3.10)

# Set some basic project attributes
project (hoverApp
	VERSION 0.1
	DESCRIPTION "hovercraft application")

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# This project will output an executable file
set(SOURCES 
	hoverApp.cpp
	Video/src/videoStream.cpp 
	Graphics/src/opengl.cpp 
	)
LINK_DIRECTORIES(lib)
set(LIBRARIES drm EGL gbm GL)
add_executable(${PROJECT_NAME}  ${SOURCES})
target_link_libraries(${PROJECT_NAME} ${LIBRARIES})
# Create a simple configuration header
configure_file(config.h.in config.h)

# Include the configuration header in the build
target_include_directories(${PROJECT_NAME} PUBLIC 
										Video/inc/    
										Graphics/inc/
						  )
target_include_directories(${PROJECT_NAME} PUBLIC "${PROJECT_BINARY_DIR}")

