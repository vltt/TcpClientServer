#include <boost/archive/binary_oarchive.hpp>
#include <boost/asio.hpp>
#include <iostream>
#include <stdexcept>
#include <unordered_map>

#include "models/tcp_request.hpp"
#include "models/tcp_response.hpp"

class Client {
public:
    Client(boost::asio::io_context& io_service, int n)
        : socket_(io_service),
          num_of_packets_(n),
          packets_buffer_size_(std::min(100000, n)) {}

    void run();

private:
    const size_t packets_buffer_size_;

    boost::asio::ip::tcp::socket socket_;

    const int num_of_packets_;

    // Stored packets in memory. Since the number of packets can be too big
    // and we can't store them all, we store only part of them and send to the
    // Server only this part.
    std::unordered_map<int, models::TcpPacketRequest> packets_;

    models::TcpPacketRequest generate_packet(uint32_t num);

    void send_packet(uint32_t num);
};

void Client::send_packet(uint32_t num) {
    boost::system::error_code error;
    boost::asio::write(
        socket_,
        boost::asio::buffer(reinterpret_cast<char*>(&packets_[num]),
                            sizeof(models::TcpPacketRequest)),
        error);

    if (error) {
        std::cerr << "send failed: " << error.message() << std::endl;
    }
}

models::TcpPacketRequest Client::generate_packet(uint32_t num) {
    std::string data = "Hello from Client!\n";
    models::TcpPacketRequest packet;
    packet.num = num;
    packet.is_last = (num == (num_of_packets_ - 1));
    strcpy(packet.data, data.c_str());
    return packet;
}

void Client::run() {
    socket_.connect(boost::asio::ip::tcp::endpoint(
        boost::asio::ip::address::from_string("127.0.0.1"), 1234));

    uint32_t sent_paket_num;
    for (sent_paket_num = 0; sent_paket_num < packets_buffer_size_;
         sent_paket_num++) {
        packets_.emplace(sent_paket_num, generate_packet(sent_paket_num));
        send_packet(sent_paket_num);
    }

    for (int i = 0; i < num_of_packets_; i++) {
        boost::system::error_code error;

        // getting response from server
        char response_bytes[sizeof(models::TcpPacketResponse)];
        boost::asio::read(
            socket_, boost::asio::buffer(response_bytes,
                                         sizeof(models::TcpPacketResponse)));
        auto response =
            reinterpret_cast<models::TcpPacketResponse*>(response_bytes);

        if (!response->done) {
            throw std::logic_error("Server always returns done=true");
        }

        packets_.erase(response->num);

        // send new packet
        if (sent_paket_num < num_of_packets_) {
            models::TcpPacketRequest packet = generate_packet(sent_paket_num);
            packets_.emplace(sent_paket_num, packet);
            send_packet(sent_paket_num);
            sent_paket_num++;
        }
    }
}

int main(int argc, char** argv) {
    if (argc != 2) {
        std::cout << "Pass number of packets." << std::endl;
        return 0;
    }
    int n = std::atoi(argv[1]);

    try {
        boost::asio::io_context io_service;
        Client(io_service, n).run();
    } catch (std::exception& e) {
        std::cerr << e.what() << std::endl;
    }
    return 0;
}
