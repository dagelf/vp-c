cmake_minimum_required(VERSION 3.10)
project(vp VERSION 1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -pthread")

# Generate embedded web.html header
add_custom_command(
    OUTPUT ${CMAKE_CURRENT_SOURCE_DIR}/src/web_html.hpp
    COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/embed_web.sh
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/web.html
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    COMMENT "Embedding web.html into source"
)

# Source files
set(SOURCES
    src/main.cpp
    src/state.cpp
    src/process.cpp
    src/resource.cpp
    src/procutil.cpp
    src/api.cpp
)

# Header files
set(HEADERS
    src/types.hpp
    src/state.hpp
    src/process.hpp
    src/resource.hpp
    src/procutil.hpp
    src/api.hpp
    src/web_html.hpp
)

# Executable
add_executable(vp ${SOURCES} ${HEADERS})

# Include directories
target_include_directories(vp PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

# Link libraries
target_link_libraries(vp pthread)

# Install target
install(TARGETS vp DESTINATION bin)
