cmake_minimum_required(VERSION 3.14)
project(FuzzyController LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

FetchContent_Declare(
  json
  URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
)

FetchContent_Declare(
  httplib
  URL https://github.com/yhirose/cpp-httplib/archive/refs/tags/v0.42.0.tar.gz
)

FetchContent_MakeAvailable(json httplib)

find_library(FUZZYLITE_LIB fuzzylite REQUIRED)
if(NOT FUZZYLITE_LIB)
    message(FATAL_ERROR "Fuzzylite not found! Install with: sudo apt install libfuzzylite-dev")
endif()

add_executable(fuzzy_service src/main.cpp)

target_link_libraries(fuzzy_service 
    PRIVATE 
        nlohmann_json::nlohmann_json
        httplib::httplib
        ${FUZZYLITE_LIB}
        pthread
)

target_include_directories(fuzzy_service PRIVATE include)