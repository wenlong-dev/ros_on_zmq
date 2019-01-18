cmake_minimum_required(VERSION 2.8.3)
project(zmq_bridge)

add_compile_options(-std=c++11)

find_package(catkin REQUIRED COMPONENTS
  roscpp
  std_msgs
  topic_tools
)


find_package(PkgConfig)
pkg_check_modules(PC_ZeroMQ QUIET zmq)
find_path(ZeroMQ_INCLUDE_DIR
        NAMES zmq.hpp
        PATHS ${PC_ZeroMQ_INCLUDE_DIRS}
        )
find_library(ZeroMQ_LIBRARY
        NAMES zmq
        PATHS ${PC_ZeroMQ_LIBRARY_DIRS}
        )

include_directories(
  include
  ${catkin_INCLUDE_DIRS}
  ${ZeroMQ_INCLUDE_DIR}
)

add_executable(testnode src/testnode.cpp)
add_executable(broker src/broker.cpp)
target_link_libraries(testnode ${catkin_LIBRARIES} ${ZeroMQ_LIBRARY} )
target_link_libraries(broker ${catkin_LIBRARIES} ${ZeroMQ_LIBRARY} )