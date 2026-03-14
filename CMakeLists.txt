cmake_minimum_required(VERSION 3.10)
project(MIA_P1)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Crow y cpp-httplib requieren threads
find_package(Threads REQUIRED)

# Incluir directorios de headers
include_directories(
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/external
    ${CMAKE_SOURCE_DIR}/Structs
    ${CMAKE_SOURCE_DIR}/Utilities
    ${CMAKE_SOURCE_DIR}/Analyzer
    ${CMAKE_SOURCE_DIR}/DiskManagement
    ${CMAKE_SOURCE_DIR}/FileSystem
    ${CMAKE_SOURCE_DIR}/UserSession
    ${CMAKE_SOURCE_DIR}/FileOperations
    ${CMAKE_SOURCE_DIR}/Reports
    ${CMAKE_SOURCE_DIR}/HttpServer
)

# Fuentes del ejecutable principal
add_executable(MIA_P1
    main.cpp
    Analyzer/Analyzer.cpp
    DiskManagement/DiskManagement.cpp
    FileSystem/FileSystem.cpp
    UserSession/UserSession.cpp
    FileOperations/FileOperations.cpp
    Reports/Reports.cpp
    Utilities/Utilities.cpp
    HttpServer/HttpServer.cpp
)

target_link_libraries(MIA_P1
    Threads::Threads
)

# Flags de compilacion
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(MIA_P1 PRIVATE -Wall -Wextra -O2)
endif()