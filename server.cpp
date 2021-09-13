#include <atomic>
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

#include "connection.hpp"
#include "models/tcp_request.hpp"
#include "models/tcp_response.hpp"

class Server {
public:
    Server(boost::asio::io_context& io_service, std::string dump_folder_path = {})
            : io_service_(io_service),
              acceptor_(io_service, boost::asio::ip::tcp::endpoint(
                      boost::asio::ip::tcp::v4(), 1234)),
              requests_pool_(kWorkersCount),
              dump_folder_path_(std::move(dump_folder_path)) {
        start_accept();
    }

    ~Server() {
        requests_pool_.stop();
        for (auto& f : open_clients_) {
            f.second.file_stream.close();
        }
    }

private:
    static constexpr size_t kWorkersCount = 4;

    boost::asio::io_context& io_service_;
    boost::asio::ip::tcp::acceptor acceptor_;

    boost::asio::thread_pool requests_pool_;
    std::string dump_folder_path_;

    // Store Connection in the map to pass it to the following functions
    // by ref, but not by value, cause creating share_ptr take a time
    struct ClientInfo {
        std::ofstream file_stream;
        std::shared_ptr<Connection> connection;
        std::unique_ptr<std::mutex> mutex_ptr;
    };
    std::unordered_map<std::string, ClientInfo> open_clients_;

    boost::uuids::basic_random_generator<boost::mt19937> uuid_gen;

    void start_accept();

    void handle_accept(std::shared_ptr<Connection> connection,
                       const boost::system::error_code& err);

    void async_read(const std::shared_ptr<Connection>& connection,
                    std::string file_name);

    void handle_read(const boost::system::error_code& err,
                     size_t bytes_transferred,
                     const std::shared_ptr<Connection>& connection,
                     std::string file_name);

    void do_work(const std::shared_ptr<Connection>& connection,
                 std::string file_name);
};

void Server::start_accept() {
    std::shared_ptr<Connection> connection = Connection::create(io_service_);

    acceptor_.async_accept(connection->socket(),
                           boost::bind(&Server::handle_accept, this, connection,
                                       boost::asio::placeholders::error));
}

void Server::handle_accept(std::shared_ptr<Connection> connection,
                           const boost::system::error_code& err) {
    if (!err) {
        std::string file_name = dump_folder_path_ + "vltt_" +
                                boost::uuids::to_string(uuid_gen()) + ".txt";
        std::ofstream oFile;
        oFile.open(file_name, std::ios::out | std::ios::app);

        open_clients_.insert(
                {file_name, ClientInfo{.file_stream = std::move(oFile),
                        .connection = std::move(connection),
                        .mutex_ptr = std::make_unique<std::mutex>()}});

        async_read(open_clients_[file_name].connection, file_name);
    }
    start_accept();
}

void Server::async_read(const std::shared_ptr<Connection>& connection,
                        std::string file_name) {
    boost::asio::async_read(
            connection->socket(),
            boost::asio::buffer(connection->get_request_ptr(),
                                sizeof(models::TcpPacketRequest)),
            boost::bind(&Server::handle_read, this, boost::asio::placeholders::error,
                        boost::asio::placeholders::bytes_transferred, connection,
                        file_name));
}

void Server::handle_read(const boost::system::error_code& err,
                         size_t bytes_transferred,
                         const std::shared_ptr<Connection>& connection,
                         std::string file_name) {
    if (!err) {
        boost::asio::post(requests_pool_, boost::bind(&Server::do_work, this,
                                                      connection, file_name));
    } else {
        std::cerr << "error: " << err.message() << std::endl;
    }
}

void Server::do_work(const std::shared_ptr<Connection>& connection,
                     std::string file_name) {
    models::TcpPacketRequest request = connection->get_request();

    // Read rest of packets in new thread. Before it, save info from request
    // in local variables, because it will be overwritten.
    if (!request.is_last) {
        async_read(connection, file_name);
    }

    auto client_info = open_clients_.find(file_name);

    {
        std::lock_guard<std::mutex> lk{*client_info->second.mutex_ptr};
        client_info->second.file_stream << request.data;
    }

    models::TcpPacketResponse response =
            connection->create_response(request.num, true);
    char* response_bytes = reinterpret_cast<char*>(&response);
    boost::asio::write(
            connection->socket(),
            boost::asio::buffer(response_bytes, sizeof(models::TcpPacketResponse)));

    if (request.is_last) {
        connection->socket().close();
        client_info->second.file_stream.close();
        open_clients_.erase(client_info);
    }
}

int main(int argc, char** argv) {
    std::string dump_folder_path;
    if (argc == 2) {
        dump_folder_path = std::string(argv[1]);
    }

    try {
        boost::asio::io_context io_service;
        Server server(io_service, std::move(dump_folder_path));
        io_service.run();
    } catch (std::exception& e) {
        std::cerr << e.what() << std::endl;
    }
    return 0;
}
