cmake_minimum_required(VERSION 3.10)
project(SearchSystem)

set(CMAKE_CXX_STANDARD 17)

if(DEFINED ENV{VCPKG_ROOT} AND NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" CACHE STRING "")
endif()

add_definitions(-D_WIN32_WINNT=0x0601)

find_package(Boost REQUIRED COMPONENTS system)
find_package(OpenSSL REQUIRED)
find_package(libpqxx REQUIRED)
find_package(pugixml REQUIRED)

include_directories(${Boost_INCLUDE_DIRS} ${pugixml_INCLUDE_DIRS})

add_library(common STATIC config.cpp db.cpp utils.cpp)
target_link_libraries(common libpqxx::pqxx Boost::system pugixml::pugixml)

add_executable(spider spider.cpp)
target_link_libraries(spider common OpenSSL::SSL OpenSSL::Crypto)

add_executable(searcher searcher.cpp)
target_link_libraries(searcher common)