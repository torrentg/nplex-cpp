#include <stop_token>
#include <iostream>
#include <thread>
#include "nplex-cpp/client.hpp"
#include "logger.hpp"

static std::string to_iso8601(std::chrono::milliseconds ms_since_epoch)
{
    using namespace std::chrono;

    auto secs = duration_cast<seconds>(ms_since_epoch); 
    auto millis = duration_cast<milliseconds>(ms_since_epoch - secs).count(); 

    std::ostringstream oss; 
    std::time_t t = secs.count(); 
    std::tm tm_utc; 

    gmtime_r(&t, &tm_utc);

    oss << std::put_time(&tm_utc, "%Y-%m-%dT%H:%M:%S"); 
    oss << '.' << std::setw(3) << std::setfill('0') << millis << 'Z'; 

    return oss.str();
}

static void print_database_content(const nplex::client_ptr &cli)
{
    auto tx = cli->create_tx();

    std::cout << "Nplex database content at rev " << tx->rev() << std::endl;

    tx->for_each([](const nplex::key_t &key, const nplex::value_t &value) {
        std::cout << key << " = " << value.data() << std::endl;
        std::cout << "  rev: " << value.rev() << std::endl;
        std::cout << "  timestamp: " << to_iso8601(value.timestamp()) << std::endl;
        std::cout << "  user: " << value.user() << std::endl;
        std::cout << "  type: " << value.type() << std::endl;
        return true;
    });
}

static void measure_latency(const nplex::client_ptr &cli)
{
    auto ping = cli->ping("Hello, Nplex!");
    std::cout << "Ping response received in " << ping.get().count() << " microseconds" << std::endl;
}

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

        // Start the nplex event loop in a dedicated thread
        std::jthread worker([cli](std::stop_token st) {
            cli->run(std::move(st));
        });

        cli->wait_for_synced();
        print_database_content(cli);
        measure_latency(cli);

        // Close the database, alternatively you can call `cli->close();`
        worker.request_stop();

        return EXIT_SUCCESS;
    }
    catch (const std::exception &e) {
        std::cerr << "Error: " << e.what() << '\n';
        return EXIT_FAILURE;
    }
}
