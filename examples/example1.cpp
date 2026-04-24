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
    size_t counter = 0;
    auto tx = cli->create_tx();

    std::cout << std::endl;
    std::cout << "Nplex database content at rev " << tx->rev() << std::endl;
    std::cout << "-------------------------------------------" << std::endl;

    tx->for_each([&counter](const nplex::key_t &key, const nplex::value_t &value) {
        std::cout << ++counter << ". ";
        std::cout << key << " = " << value.data() << std::endl;
        std::cout << "  rev: " << value.rev() << std::endl;
        std::cout << "  timestamp: " << to_iso8601(value.timestamp()) << std::endl;
        std::cout << "  user: " << value.user() << std::endl;
        std::cout << "  tx_type: " << value.tx_type() << std::endl;
        return true;
    });

    //tx->discard();
}

static void measure_latency(const nplex::client_ptr &cli)
{
    auto ping = cli->ping("Hello Nplex!");
    std::cout << "Ping response received in " << ping.get().count() << " microseconds" << std::endl;
}

static const char *to_str(nplex::session_t::code_e code)
{
    switch (code)
    {
        case nplex::session_t::code_e::CONNECTED:        return "CONNECTED";
        case nplex::session_t::code_e::CLOSED_BY_SERVER: return "CLOSED_BY_SERVER";
        case nplex::session_t::code_e::CLOSED_BY_USER:   return "CLOSED_BY_USER";
        case nplex::session_t::code_e::COMM_ERROR:       return "COMM_ERROR";
        case nplex::session_t::code_e::CON_LOST:         return "CON_LOST";
        case nplex::session_t::code_e::EXCD_LIMITS:      return "EXCD_LIMITS";
        default:                                         return "UNKNOWN";
    }
}

static void print_active_sessions(const nplex::client_ptr &cli)
{
    auto future = cli->fetch_sessions(false);
    auto sessions = future.get();

    std::cout << std::endl;
    std::cout << "List of active sessions" << std::endl;
    std::cout << "-----------------------" << std::endl;

    std::size_t index = 0;

    for (const auto &session : sessions)
    {
        std::cout << ++index << ". ";
        std::cout << "user: " << session.user << std::endl;
        std::cout << "  address: " << session.address << std::endl;
        std::cout << "  code: " << to_str(session.code) << std::endl;
        std::cout << "  since: " << to_iso8601(session.since) << std::endl;
        std::cout << "  until: " << (session.until.count() ? to_iso8601(session.until) : std::string{"-"}) << std::endl;
    }
}

static void update_content(const nplex::client_ptr &cli)
{

    auto tx = cli->create_tx(nplex::transaction::isolation_e::SERIALIZABLE, false);

    int num = tx->read_or("rct.gates.3.open", "0")->as_number_or<int>(42);

    num++;
    tx->upsert("rct.gates.3.open", std::to_string(num));
    tx->set_type(42);

    try {
        auto submit_result = tx->submit().get();

        if (submit_result == nplex::transaction::submit_e::COMMITTED)
            std::cout << "Transaction committed successfully" << std::endl;
        else
            std::cout << "Transaction rejected: " << static_cast<int>(submit_result) << std::endl;

    } catch (const std::exception &e) {
        std::cerr << "Transaction failed: " << e.what() << std::endl;
    }
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
        auto log = std::make_shared<logger>(nplex::logger::log_level_e::TRACE);
        cli->set_logger(log);

        // Start the nplex event loop in a dedicated thread
        std::jthread worker([cli](std::stop_token st) {
            cli->run(std::move(st));
        });

        cli->wait_for_synced();
        print_active_sessions(cli);
        print_database_content(cli);
        measure_latency(cli);
        update_content(cli);

        // Close the database, alternatively you can call `cli->close();`
        //worker.request_stop();

        return EXIT_SUCCESS;
    }
    catch (const std::exception &e) {
        std::cerr << "Error: " << e.what() << '\n';
        return EXIT_FAILURE;
    }
}
