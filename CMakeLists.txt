cmake_minimum_required(VERSION 3.16)
project(pcf-to-serenity)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_SKIP_BUILD_RPATH FALSE)
set(CMAKE_BUILD_WITH_INSTALL_RPATH FALSE)
# See slide 100 of the following ppt :^)
# https://crascit.com/wp-content/uploads/2019/09/Deep-CMake-For-Library-Authors-Craig-Scott-CppCon-2019.pdf
if (NOT APPLE)
    set(CMAKE_INSTALL_RPATH $ORIGIN)
endif()
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)

include (FetchContent)
include (FetchLagom.cmake)

set(SOURCES
    main.cpp
	PCFFile.cpp
)

add_executable(pcf-to-serenity ${SOURCES})
target_link_libraries(pcf-to-serenity Lagom::Core Lagom::Gfx Lagom::Main)
install(TARGETS pcf-to-serenity RUNTIME DESTINATION ${CMAKE_INSTALL_PREFIX})
