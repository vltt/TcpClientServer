cmake_minimum_required(VERSION 3.20)
project(TcpClientServer)

set(CMAKE_CXX_STANDARD 17)
find_package(Boost REQUIRED system)

add_executable(TcpServer server.cpp)
target_link_libraries(TcpServer ${Boost_asio} ${Boost_LIBRARIES})

add_executable(TcpClient client.cpp)
target_link_libraries(TcpClient ${Boost_asio} ${Boost_LIBRARIES})