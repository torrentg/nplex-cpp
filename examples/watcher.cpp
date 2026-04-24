#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <getopt.h>
#include <iomanip>
#include <iostream>
#include <string>
#include <thread>
#include <vector>
#include "nplex-cpp/client.hpp"
#include "json.hpp"

using namespace nplex;

struct config_t
{
    std::string user;
    std::string password;
    std::string servers;
    bool compact = false;
    bool data_only = false;
    bool sessions_only = false;
    bool once = false;
};

static nplex::client_ptr g_client;

static void signal_handler(int)
{
    if (g_client)
        g_client->close();
}

static void print_help(const char *prog)
{
    std::cout
        << "Description:\n"
        << "  Watch nplex events in real time.\n"
        << "\n"
        << "Usage: " << prog << " [options]\n"
        << "\n"
        << "Options:\n"
        << "  -h, --help           Show this help and exit\n"
        << "  -u, --user USER      User identifier (mandatory)\n"
        << "  -p, --password PWD   User password (mandatory)\n"
        << "  -s, --servers LIST   Comma-separated server list (mandatory)\n"
        << "  -c, --ndjson         Compact JSON output (default: indented)\n"
        << "      --data-only      Watch only database events\n"
        << "      --sessions-only  Watch only session events\n"
        << "      --once           Print initial snapshots and exit\n";
}

static bool parse_args(int argc, char *argv[], config_t &cfg)
{
    static struct option long_opts[] = {
        {"help",          no_argument,       nullptr, 'h'},
        {"user",          required_argument, nullptr, 'u'},
        {"password",      required_argument, nullptr, 'p'},
        {"servers",       required_argument, nullptr, 's'},
        {"ndjson",        no_argument,       nullptr, 'c'},
        {"data-only",     no_argument,       nullptr, 1000},
        {"sessions-only", no_argument,       nullptr, 1001},
        {"once",          no_argument,       nullptr, 1002},
        {nullptr,          0,                 nullptr, 0}
    };

    optind = 1;

    for (;;)
    {
        int idx = 0;
        int c = getopt_long(argc, argv, "hu:p:s:c", long_opts, &idx);

        if (c == -1)
            break;

        switch (c)
        {
            case 'h':
                print_help(argv[0]);
                std::exit(EXIT_SUCCESS);
            case 'u':
                cfg.user = optarg;
                break;
            case 'p':
                cfg.password = optarg;
                break;
            case 's':
                cfg.servers = optarg;
                break;
            case 'c':
                cfg.compact = true;
                break;
            case 1000:
                cfg.data_only = true;
                break;
            case 1001:
                cfg.sessions_only = true;
                break;
            case 1002:
                cfg.once = true;
                break;
            default:
                return false;
        }
    }

    if (cfg.user.empty() || cfg.password.empty() || cfg.servers.empty())
        return false;

    if (cfg.data_only && cfg.sessions_only)
        return false;

    return true;
}

class watcher_reactor final : public reactor
{
  public:

    watcher_reactor(char mode, bool print_data, bool print_sessions)
        : m_mode(mode), m_print_data(print_data), m_print_sessions(print_sessions) {}

    void on_initial_data(client &cli) override
    {
        if (!m_print_data)
            return;

        auto tx = cli.create_tx(transaction::isolation_e::READ_COMMITTED, true);

        tx->for_each([this](const nplex::key_t &key, const nplex::value_t &value) {
            std::cout << data_to_json(key, value, m_mode) << '\n';
            return true;
        });

        std::cout.flush();
    }

    void on_event_data(client &, const const_meta_ptr &meta, const std::vector<change_t> &changes) override
    {
        if (!m_print_data)
            return;

        std::cout << event_data_to_json(meta, changes, m_mode) << '\n';
        std::cout.flush();
    }

    void on_initial_sessions(client &, const std::vector<session_t> &sessions) override
    {
        if (!m_print_sessions)
            return;

        for (const auto &session : sessions)
            std::cout << session_to_json(session, m_mode) << '\n';

        std::cout.flush();
    }

    void on_event_session(client &, const session_t &session) override
    {
        if (!m_print_sessions)
            return;

        std::cout << event_session_to_json(session, m_mode) << '\n';
        std::cout.flush();
    }

  private:
    char m_mode;
    bool m_print_data;
    bool m_print_sessions;
};

int main(int argc, char *argv[])
{
    try
    {
        config_t cfg;

        if (!parse_args(argc, argv, cfg)) {
            std::cerr << "Error: invalid arguments. Use --help for usage.\n";
            return EXIT_FAILURE;
        }

        const bool print_data = !cfg.sessions_only;
        const bool print_sessions = !cfg.data_only;
        const char json_mode = cfg.compact ? 'c' : 'i';
        const params_t params = {
            .servers = cfg.servers,
            .user = cfg.user,
            .password = cfg.password
        };

        std::signal(SIGINT, signal_handler);
        std::signal(SIGTERM, signal_handler);

        g_client = client::create(params);
        
        auto reactor = std::make_shared<watcher_reactor>(json_mode, print_data, print_sessions);
        g_client->set_reactor(reactor);

        std::jthread worker([](std::stop_token st) {
            g_client->run(st);
        });

        g_client->wait_for_synced();

        if (print_sessions) {
            auto future = g_client->fetch_sessions(!cfg.once);
            future.get();
        }

        if (cfg.once)
            g_client->close();

        g_client->wait_for_closed();

        return EXIT_SUCCESS;
    }
    catch (const std::exception &e) {
        std::cerr << "Error: " << e.what() << '\n';
        return EXIT_FAILURE;
    }
}
