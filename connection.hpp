#pragma once

#include <boost/asio.hpp>

#include "models/tcp_request.hpp"
#include "models/tcp_response.hpp"

class Connection {
 public:
  Connection(boost::asio::io_context& io_service) : sock_(io_service) {}

  static std::shared_ptr<Connection> create(
      boost::asio::io_context& io_service) {
    return std::make_shared<Connection>(io_service);
  }

  boost::asio::ip::tcp::socket& socket() { return sock_; }

  char* get_request_ptr() { return reinterpret_cast<char*>(&request_); }

  models::TcpPacketRequest get_request() { return request_; }

  models::TcpPacketResponse create_response(uint32_t packet_num,
                                            bool is_done) const {
    models::TcpPacketResponse response;
    response.done = is_done;
    response.num = packet_num;
    return response;
  }

 private:
  models::TcpPacketRequest request_;
  boost::asio::ip::tcp::socket sock_;
};
