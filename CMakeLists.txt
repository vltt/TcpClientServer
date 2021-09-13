cmake_minimum_required(VERSION 3.1)
project(TcpClientServer)

set(CMAKE_CXX_STANDARD 17)
find_package(Boost REQUIRED COMPONENTS system thread)

include_directories(${CMAKE_SOURCE_PATH})

add_executable(TcpServer server.cpp)
target_link_libraries(TcpServer ${Boost_asio} ${Boost_LIBRARIES} Boost::thread)

add_executable(TcpClient client.cpp)
target_link_libraries(TcpClient ${Boost_asio} ${Boost_LIBRARIES} Boost::thread)
