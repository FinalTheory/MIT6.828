cmake_minimum_required(VERSION 3.0)

file(GLOB_RECURSE XV6_SRC "*.c" "*.S" "*.h")

add_library(jos STATIC ${XV6_SRC})

add_definitions(-DJOS_KERNEL)

include_directories(.)
include_directories(net/lwip/include)
include_directories(net/lwip/jos)
