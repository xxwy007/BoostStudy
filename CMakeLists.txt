cmake_minimum_required(VERSION 3.24)

if(POLICY CMP0144)
    cmake_policy(SET CMP0144 NEW)
endif()
if(POLICY CMP0167)
    # Keep using CMake's FindBoost module for a local Boost source tree.
    cmake_policy(SET CMP0167 OLD)
endif()

project(beast_task_server VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Boost can be supplied either by vcpkg or by an unpacked/local installation.
# Example:
#   cmake -S . -B build -DBOOST_ROOT=D:/Libraries/boost_1_90_0
set(BOOST_ROOT "" CACHE PATH "Root directory of a local Boost installation")
set(BOOST_LIBRARY_DIR "" CACHE PATH "Directory containing compiled Boost libraries")

# Update an empty value left in an older Visual Studio CMake cache.
if(NOT BOOST_ROOT)
    set(BOOST_ROOT "E:/boost_1_92_0" CACHE PATH
        "Root directory of a local Boost installation" FORCE)
endif()

if(BOOST_ROOT)
    file(TO_CMAKE_PATH "${BOOST_ROOT}" BOOST_ROOT)
    list(PREPEND CMAKE_PREFIX_PATH "${BOOST_ROOT}")

    if(EXISTS "${BOOST_ROOT}/boost/version.hpp")
        set(BOOST_INCLUDEDIR "${BOOST_ROOT}")
    elseif(EXISTS "${BOOST_ROOT}/include/boost/version.hpp")
        set(BOOST_INCLUDEDIR "${BOOST_ROOT}/include")
    else()
        message(FATAL_ERROR
            "BOOST_ROOT does not contain boost/version.hpp: ${BOOST_ROOT}")
    endif()

    if(BOOST_LIBRARY_DIR)
        file(TO_CMAKE_PATH "${BOOST_LIBRARY_DIR}" BOOST_LIBRARY_DIR)
        set(BOOST_LIBRARYDIR "${BOOST_LIBRARY_DIR}")
    elseif(EXISTS "${BOOST_ROOT}/stage/lib")
        set(BOOST_LIBRARYDIR "${BOOST_ROOT}/stage/lib")
    elseif(EXISTS "${BOOST_ROOT}/lib")
        set(BOOST_LIBRARYDIR "${BOOST_ROOT}/lib")
    endif()

    message(STATUS "Using local Boost root: ${BOOST_ROOT}")
    message(STATUS "Boost include hint: ${BOOST_INCLUDEDIR}")
    if(BOOST_LIBRARYDIR)
        message(STATUS "Boost library hint: ${BOOST_LIBRARYDIR}")

        if(WIN32)
            file(GLOB BOOST_JSON_LIBRARIES
                "${BOOST_LIBRARYDIR}/*boost_json*.lib")
            file(GLOB BOOST_PROGRAM_OPTIONS_LIBRARIES
                "${BOOST_LIBRARYDIR}/*boost_program_options*.lib")
            if(NOT BOOST_JSON_LIBRARIES OR NOT BOOST_PROGRAM_OPTIONS_LIBRARIES)
                message(FATAL_ERROR
                    "Boost headers were found at ${BOOST_INCLUDEDIR}, but compiled "
                    "Boost.JSON or Boost.Program_options libraries were not found in "
                    "${BOOST_LIBRARYDIR}. Run bootstrap.bat and then: "
                    "b2.exe --with-json --with-program_options "
                    "address-model=64 variant=debug,release stage")
            endif()
        endif()
    elseif(WIN32)
        message(FATAL_ERROR
            "Boost headers were found at ${BOOST_INCLUDEDIR}, but no compiled-library "
            "directory exists at ${BOOST_ROOT}/stage/lib or ${BOOST_ROOT}/lib. "
            "Run bootstrap.bat and then: b2.exe --with-json "
            "--with-program_options address-model=64 "
            "variant=debug,release stage")
    endif()
endif()

set(Boost_NO_WARN_NEW_VERSIONS ON)
find_package(Boost 1.75 REQUIRED COMPONENTS json program_options)
find_package(Threads REQUIRED)

message(STATUS "Boost version: ${Boost_VERSION}")
message(STATUS "Boost include directories: ${Boost_INCLUDE_DIRS}")
message(STATUS "Boost library directories: ${Boost_LIBRARY_DIRS}")

add_executable(task_server
    src/main.cpp
    src/listener.cpp
    src/router.cpp
    src/session.cpp
    src/task_repository.cpp
)

target_include_directories(task_server PRIVATE include)

target_link_libraries(task_server PRIVATE
    Boost::json
    Boost::program_options
    Threads::Threads
)

if(WIN32)
    target_compile_definitions(task_server PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
    target_link_libraries(task_server PRIVATE ws2_32 mswsock)
endif()

if(MSVC)
    target_compile_options(task_server PRIVATE /W4 /permissive- /EHsc)
else()
    target_compile_options(task_server PRIVATE -Wall -Wextra -Wpedantic)
endif()

