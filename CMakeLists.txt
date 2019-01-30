cmake_minimum_required(VERSION 2.8.0)
project(weaver)

add_definitions(-std=c++11)

add_subdirectory(src)

#set_property (TARGET weaver PROPERTY CXX_STANDARD_REQUIRED YES)
#set_property (TARGET weaver PROPERTY CXX_STANDARD 11)

# set_target_properties (weaver PROPERTIES COMPILER_FLAGS -std=c++11)

