# generated by cmake-generator
cmake_minimum_required(VERSION 3.16.3)

add_library(rectify STATIC 
  rectify.c)

include(Tornado.cmake)
set_tornado(rectify)

target_include_directories(rectify PUBLIC ../include)


target_link_libraries(rectify PUBLIC 
  transmute
  nimble-steps-serialize
  seer
  assent)

