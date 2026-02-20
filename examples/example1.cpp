#include <iostream>
#include <memory>
#include <thread>
#include <stop_token>
#include "nplex-cpp/client.hpp"
#include "logger.hpp"

int main()
{
    try {
        // Configure connection parameters
        nplex::params_t params = {
            .servers  = "localhost:14022, localhost:8081",
            .user     = "admin",
            .password = "s3cr3t"
        };

        // Create client instance
        auto cli = nplex::client::create(params);

        // Attach logger
        auto log = std::make_shared<logger>(nplex::logger::log_level_e::DEBUG);
        cli->set_logger(log);

        // Start client event loop in a dedicated thread and wait for it
        std::jthread worker([cli](std::stop_token st) {
            cli->run(st);
        });

        std::cout << "nplex-cpp client running. Press ENTER to stop..." << std::endl;
        std::string line;
        std::getline(std::cin, line);

        // Request client shutdown and wait for thread to finish
        cli->close();
        worker.request_stop();

        return 0;
    }
    catch (const std::exception &e) {
        std::cerr << "Error: " << e.what() << '\n';
        return 1;
    }
}
