#include <boost/asio.hpp>
#include <boost/bind.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <chrono>
#include <fstream>
#include <iostream>
#include <memory>
#include <thread>
#include <unordered_map>
#include <atomic>

#pragma pack(push, 1)
struct TcpPacketRequest {
    uint32_t num;
    bool is_last;
    char data[4 * 1024 - sizeof(uint32_t) - sizeof(bool)];
};
#pragma pack(pop)

#pragma pack(push, 1)
struct TcpPacketResponse {
    uint32_t num;
    bool done;
};
#pragma pack(pop)

class Connection {
public:
    Connection(boost::asio::io_context& io_service) : sock_(io_service) {}

    static std::shared_ptr<Connection> create(
        boost::asio::io_context& io_service) {
        return std::make_shared<Connection>(io_service);
    }

    boost::asio::ip::tcp::socket& socket() { return sock_; }

    char* read_request_to() { return reinterpret_cast<char*>(&request_); }

    TcpPacketRequest get_request() { return request_; }

    TcpPacketResponse create_response(uint32_t packet_num, bool is_done) const {
        TcpPacketResponse response;
        response.done = is_done;
        response.num = packet_num;
        return response;
    }

private:
    TcpPacketRequest request_;
    boost::asio::ip::tcp::socket sock_;
};

class Server {
public:
    Server(boost::asio::io_context& io_service)
        : io_service_(io_service),
          acceptor_(io_service, boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), port)),
          requests_pool_(workers_cnt) {
        start_accept();
    }

    ~Server() {
        requests_pool_.wait();
        for (auto& f : open_clients_) {
            f.second.file_stream.close();
        }
    }

private:
    const int port = 1234;
    boost::asio::io_context& io_service_;
    boost::asio::ip::tcp::acceptor acceptor_;

    const size_t workers_cnt = 4;
    boost::asio::thread_pool requests_pool_;

    struct ClientInfo {
        std::ofstream file_stream;
        std::shared_ptr<Connection> connection;
        std::unique_ptr<std::mutex> mutex_ptr;
    };
    std::unordered_map<std::string, ClientInfo> open_clients_;

    boost::uuids::basic_random_generator<boost::mt19937> uuid_gen;

    void start_accept() {
        std::shared_ptr<Connection> connection =
            Connection::create(io_service_);
        // asynchronous accept operation and wait for a new connection.
        acceptor_.async_accept(
            connection->socket(),
            boost::bind(&Server::handle_accept, this, connection,
                        boost::asio::placeholders::error));
    }

    void handle_accept(std::shared_ptr<Connection> connection,
                       const boost::system::error_code& err) {
        if (!err) {
            std::string file_name =
                "vltt_" + boost::uuids::to_string(uuid_gen()) + ".txt";
            std::ofstream oFile;
            oFile.open(file_name,
                       std::ios::out |
                           std::ios::app);  // TODO: what if the same client
                                            // will open a new socket?
            // Store Connection in the map to pass it to the following functions
            // by ref, but not by value, cause creating share_ptr take a time
            ClientInfo client_info;
            client_info.file_stream = std::move(oFile);
            client_info.connection = std::move(connection);
            client_info.mutex_ptr = std::make_unique<std::mutex>();
            open_clients_.emplace(file_name, std::move(client_info));

            async_read(open_clients_[file_name].connection, file_name);
        }
        start_accept();
    }

    void async_read(std::shared_ptr<Connection>& connection,
                    const std::string file_name) {
        boost::asio::async_read(
            connection->socket(),
            boost::asio::buffer(connection->read_request_to(),
                                sizeof(TcpPacketRequest)),
            boost::bind(&Server::handle_read, this,
                        boost::asio::placeholders::error,
                        boost::asio::placeholders::bytes_transferred,
                        connection, file_name));
    }

    void handle_read(const boost::system::error_code& err,
                     size_t bytes_transferred,
                     std::shared_ptr<Connection>& connection,
                     const std::string file_name) {
        if (!err) {
            boost::asio::post(
                requests_pool_,
                boost::bind(&Server::do_work, this, connection, file_name));
        } else {
            std::cerr << "error: " << err.message() << std::endl;
        }
    }

    void do_work(std::shared_ptr<Connection>& connection,
                 const std::string file_name) {
        TcpPacketRequest request = connection->get_request();
//        std::cout << "is_last_packet  " << request.is_last << std::endl;
//        std::cout << "packet_num  " << request.num << std::endl;
//        std::cout << "useful_data  " << request.data << std::endl;

        // Read rest of packets in new thread. Before it, save info from request
        // in local variables, because it will be overwritten.
        if (!request.is_last) {
            async_read(connection, file_name);
        }

//        std::this_thread::sleep_for(std::chrono::milliseconds(3000));
        auto client_info = open_clients_.find(file_name);
        client_info->second.mutex_ptr->lock();
        client_info->second.file_stream << request.data;
        client_info->second.mutex_ptr->unlock();

        TcpPacketResponse response =
            connection->create_response(request.num, true);
        char* response_bytes = reinterpret_cast<char*>(&response);
        boost::asio::write(
            connection->socket(),
            boost::asio::buffer(response_bytes, sizeof(TcpPacketResponse)));

        if (request.is_last) {
            connection->socket().close();
            client_info->second.file_stream.close();
            open_clients_.erase(client_info);
        }
    }
};

int main() {
    try {
        boost::asio::io_context io_service;
        Server server(io_service);
        io_service.run();
    } catch (std::exception& e) {
        std::cerr << e.what() << std::endl;
    }
    return 0;
}
