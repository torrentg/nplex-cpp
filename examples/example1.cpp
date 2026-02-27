#include <stop_token>
#include <iostream>
#include <thread>
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
            cli->run(std::move(st));
        });

        cli->wait_for_synced();

        auto tx = cli->create_tx();

        std::cout << "Nplex database content at rev " << tx->rev() << std::endl;

        tx->for_each([](const nplex::key_t &key, const nplex::value_t &value) {
            std::cout << key << " = " << value.data() << std::endl;
            return true;
        });

        std::cout << "Press ENTER to stop..." << std::endl;
        std::string line;
        std::getline(std::cin, line);

        // Measure ping latency
        auto ping = cli->ping("Hello, Nplex!");
        std::cout << "Ping response received in " << ping.get().count() << " microseconds" << std::endl;

        // Request client shutdown and wait for thread to finish
        cli->close();
        worker.request_stop();

        return EXIT_SUCCESS;
    }
    catch (const std::exception &e) {
        std::cerr << "Error: " << e.what() << '\n';
        return EXIT_FAILURE;
    }
}
