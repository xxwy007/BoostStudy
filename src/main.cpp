#include "listener.hpp"
#include "router.hpp"
#include "server_stats.hpp"
#include "task_repository.hpp"

#include <boost/asio.hpp>
#include <boost/program_options.hpp>

#include <algorithm>
#include <csignal>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace net = boost::asio;
namespace po = boost::program_options;
using tcp = net::ip::tcp;

struct Config {
    std::string address{"127.0.0.1"};
    std::uint16_t port{8080};
    unsigned int threads{std::max(1u, std::thread::hardware_concurrency())};
    std::size_t body_limit{64 * 1024};
};

int main(int argc, char* argv[]) {
    try {
        Config config;
        unsigned int port = config.port;

        po::options_description options("Options");
        options.add_options()
            ("help,h", "Show this help")
            ("address,a", po::value<std::string>(&config.address)->default_value(config.address),
             "Listen address")
            ("port,p", po::value<unsigned int>(&port)->default_value(config.port),
             "Listen port (1-65535)")
            ("threads,t", po::value<unsigned int>(&config.threads)->default_value(config.threads),
             "Number of I/O threads")
            ("body-limit", po::value<std::size_t>(&config.body_limit)->default_value(config.body_limit),
             "Maximum HTTP request body size in bytes");

        po::variables_map variables;
        po::store(po::parse_command_line(argc, argv, options), variables);
        po::notify(variables);

        if (variables.contains("help")) {
            std::cout << options << '\n';
            return 0;
        }
        if (port == 0 || port > 65535) {
            throw std::invalid_argument("port must be between 1 and 65535");
        }
        if (config.threads == 0) {
            throw std::invalid_argument("threads must be at least 1");
        }
        if (config.body_limit == 0) {
            throw std::invalid_argument("body-limit must be greater than 0");
        }
        config.port = static_cast<std::uint16_t>(port);

        const auto address = net::ip::make_address(config.address);
        net::io_context io_context(static_cast<int>(config.threads));

        auto repository = std::make_shared<TaskRepository>();
        auto stats = std::make_shared<ServerStats>();
        auto router = std::make_shared<Router>(repository, stats);

        std::make_shared<Listener>(
            io_context,
            tcp::endpoint{address, config.port},
            router,
            stats,
            config.body_limit)->run();

        net::signal_set signals(io_context, SIGINT, SIGTERM);
        signals.async_wait([&io_context](const boost::system::error_code&, int) {
            io_context.stop();
        });

        std::cout << "Listening on http://" << config.address << ':' << config.port
                  << " with " << config.threads << " thread(s)\n";

        std::vector<std::thread> workers;
        workers.reserve(config.threads - 1);
        for (unsigned int index = 1; index < config.threads; ++index) {
            workers.emplace_back([&io_context] { io_context.run(); });
        }
        io_context.run();
        for (auto& worker : workers) {
            worker.join();
        }
    } catch (const std::exception& error) {
        std::cerr << "Fatal error: " << error.what() << '\n';
        return 1;
    }
    return 0;
}

