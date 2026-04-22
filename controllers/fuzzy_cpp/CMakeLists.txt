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

FetchContent_Declare(
  fuzzylite
  URL https://github.com/fuzzylite/fuzzylite/archive/refs/tags/v6.0.tar.gz
)

FetchContent_MakeAvailable(json httplib fuzzylite)

file(GLOB_RECURSE FUZZYLITE_SOURCES
    "${fuzzylite_SOURCE_DIR}/fuzzylite/src/*.cpp"
)

add_library(fuzzylite STATIC ${FUZZYLITE_SOURCES})
target_include_directories(fuzzylite
    PUBLIC
        "${fuzzylite_SOURCE_DIR}/fuzzylite"
)

add_executable(fuzzy_service src/main.cpp)

target_link_libraries(fuzzy_service 
    PRIVATE 
        nlohmann_json::nlohmann_json
        httplib::httplib
        fuzzylite
        pthread
)

target_include_directories(fuzzy_service PRIVATE include)