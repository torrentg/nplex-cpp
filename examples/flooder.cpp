#include <set>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <getopt.h>
#include <iomanip>
#include <iostream>
#include <limits>
#include <mutex>
#include <numeric>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#include "nplex-cpp/client.hpp"
#include "logger.hpp"

using namespace std::chrono;
using sclock = std::chrono::steady_clock;

// ---------------------------------------------------------------------------
// Types and classes
// ---------------------------------------------------------------------------

struct config_t
{
    std::string user;
    std::string password;
    std::string servers;
    std::uint32_t tx_per_sec     = 0;
    std::uint32_t refresh_secs   = 1;
    std::uint32_t num_keys       = 100;
    std::uint32_t data_size      = 25;
    std::uint32_t max_active_tx  = 100;
};

struct dict_entry_t
{
    const char *prefix;
    std::vector<const char *> suffixes;
};

struct reactor_stats_t
{
    std::uint64_t updates  = 0;
    std::uint64_t upd_keys = 0;
    std::uint64_t upd_bytes = 0;
};

struct submit_stats_t
{
    std::uint64_t submits   = 0;    // Number of submits sent during the period
    std::uint64_t commits   = 0;    // Number of tx committed during the period
    std::uint64_t rejects   = 0;    // Number of tx rejected during the period
    std::int64_t  sum_us    = 0;    // cumulated time of completed tx during the period (microseconds)
};

struct slot_t
{
    nplex::tx_ptr tx;
    std::future<nplex::transaction::submit_e> future;
    sclock::time_point start;
};

class flooder_reactor final : public nplex::reactor
{
  public:

    void on_snapshot(nplex::client &cli) override
    {
        auto tx = cli.create_tx(nplex::transaction::isolation_e::READ_COMMITTED, true);

        std::lock_guard lock(m_mutex);

        m_existing_keys.clear();

        tx->for_each([this](const nplex::key_t &key, const nplex::value_t &) {
            m_existing_keys.emplace_back(key.data(), key.size());
            return true;
        });
    }

    void on_update(nplex::client &,
                   const nplex::const_meta_ptr &,
                   const std::vector<nplex::change_t> &changes) override
    {
        std::lock_guard lock(m_mutex);

        m_stats.updates++;
        m_stats.upd_keys += changes.size();

        for (const auto &ch : changes) {
            if (ch.new_value)
                m_stats.upd_bytes += ch.new_value->data().size();
        }
    }

    reactor_stats_t swap_stats()
    {
        std::lock_guard lock(m_mutex);
        reactor_stats_t tmp = m_stats;
        m_stats = {};
        return tmp;
    }

    std::vector<std::string> take_existing_keys()
    {
        std::lock_guard lock(m_mutex);
        return std::move(m_existing_keys);
    }

  private:
    std::mutex m_mutex;
    reactor_stats_t m_stats;
    std::vector<std::string> m_existing_keys;
};

// ---------------------------------------------------------------------------
// Global variables
// ---------------------------------------------------------------------------

static const std::vector<dict_entry_t> DICTIONARY = {
    {"sensor",      {"value", "unit", "voltage", "current", "status", "range", "precision", "offset", "calibration", "timestamp"}},
    {"actuator",    {"pressure", "state", "active", "position", "torque", "speed", "power", "override", "fault", "control"}},
    {"alarm",       {"state", "priority", "active", "acknowledged", "triggered", "reset", "type", "timestamp", "source", "message"}},
    {"counter",     {"value", "increment", "decrement", "reset", "overflow", "limit", "status", "timestamp", "rate", "cycles"}},
    {"controller",  {"mode", "setpoint", "output", "input", "status", "error", "gain", "range", "override", "fault"}},
    {"valve",       {"position", "flow", "pressure", "state", "status", "override", "fault", "control", "open", "close"}},
    {"motor",       {"speed", "torque", "current", "voltage", "power", "status", "temperature", "efficiency", "fault", "control"}},
    {"pump",        {"flow", "pressure", "speed", "power", "status", "temperature", "efficiency", "fault", "control", "override"}},
    {"generator",   {"voltage", "current", "frequency", "power", "load", "efficiency", "status", "fault", "temperature", "speed"}},
    {"transformer", {"voltage", "current", "power", "temperature", "efficiency", "status", "fault", "load", "tap", "oil"}},
    {"relay",       {"state", "voltage", "current", "power", "trips", "resets", "cycles", "fault", "control", "override"}},
    {"switch",      {"state", "position", "voltage", "current", "power", "fault", "control", "override", "trips", "resets"}},
    {"meter",       {"voltage", "current", "power", "energy", "frequency", "status", "fault", "load", "efficiency", "timestamp"}},
    {"indicator",   {"status", "value", "range", "precision", "fault", "power", "brightness", "color", "mode", "timestamp"}},
    {"thermostat",  {"temperature", "setpoint", "mode", "status", "fault", "override", "control", "efficiency", "power", "cycles"}},
    {"compressor",  {"pressure", "temperature", "speed", "power", "status", "efficiency", "fault", "load", "control", "override"}},
    {"turbine",     {"speed", "power", "temperature", "pressure", "efficiency", "status", "fault", "load", "control", "override"}},
    {"heater",      {"temperature", "power", "status", "fault", "control", "override", "efficiency", "load", "cycles", "mode"}},
    {"fan",         {"speed", "power", "status", "fault", "control", "override", "efficiency", "load", "temperature", "pressure"}},
    {"scanner",     {"status", "mode", "efficiency", "cycles", "resets", "load", "control", "override", "power", "fault"}},
};
static std::atomic<bool> g_stop{false};
static thread_local std::mt19937 g_rng{std::random_device{}()};
static std::vector<std::string> keys;

// ---------------------------------------------------------------------------
// Functions
// ---------------------------------------------------------------------------

static void signal_handler(int /*sig*/)
{
    g_stop.store(true, std::memory_order_relaxed);
}

static void print_help(const char *prog)
{
    std::cout
        << "Description:\n"
        << "  Flooder is a performance testing tool for nplex.\n"
        << "\n"
        << "Usage: " << prog << " [options]\n"
        << "\n"
        << "Options:\n"
        << "  -h, --help               Show this help and exit\n"
        << "  -u, --user USER          User identifier (mandatory)\n"
        << "  -p, --password PWD       User password (mandatory)\n"
        << "  -s, --servers LIST       Comma-separated server list (mandatory)\n"
        << "  -r, --refresh SECS       Statistics refresh interval in seconds (default: 1)\n"
        << "  -n, --tx-per-second N    Transactions per second target (default: 1)\n"
        << "  -m, --max-active-tx N    Maximum active transactions (default: 100)\n"
        << "  -k, --num-keys N         Approximate number of managed keys (default: 100)\n"
        << "  -b, --data-size BYTES    Average data size in bytes (default: 25)\n"
        << "\n"
        << "Output format (one line per refresh interval):\n"
        << "  TIME       - current time\n"
        << "  #submits   - submits sent during the period\n"
        << "  #commits   - transactions committed during the period\n"
        << "  #rejects   - transactions rejected during the period\n"
        << "  avgtime    - average tx round-trip time (microseconds)\n"
        << "  #updates   - updates received during the period (from reactor)\n"
        << "  #updkeys   - keys updated during the period (from reactor)\n"
        << "  #updbytes  - data bytes updated during the period (from reactor)\n";
}

static bool parse_args(int argc, char *argv[], config_t &cfg)
{
    static struct option long_opts[] = {
        {"help",          no_argument,       nullptr, 'h'},
        {"user",          required_argument, nullptr, 'u'},
        {"password",      required_argument, nullptr, 'p'},
        {"servers",       required_argument, nullptr, 's'},
        {"refresh",       required_argument, nullptr, 'r'},
        {"tx-per-second", required_argument, nullptr, 'n'},
        {"max-active-tx", required_argument, nullptr, 'm'},
        {"num-keys",      required_argument, nullptr, 'k'},
        {"data-size",     required_argument, nullptr, 'b'},
        {nullptr,         0,                 nullptr,  0 }
    };

    // Reset getopt
    optind = 1;

    for (;;)
    {
        int idx = 0;
        int c = getopt_long(argc, argv, "hu:p:s:r:n:m:k:b:", long_opts, &idx);

        if (c == -1)
            break;

        switch (c)
        {
            case 'h': print_help(argv[0]); std::exit(EXIT_SUCCESS);
            case 'u': cfg.user          = optarg; break;
            case 'p': cfg.password      = optarg; break;
            case 's': cfg.servers       = optarg; break;
            case 'r': cfg.refresh_secs  = static_cast<std::uint32_t>(std::stoul(optarg)); break;
            case 'n': cfg.tx_per_sec    = static_cast<std::uint32_t>(std::stoul(optarg)); break;
            case 'm': cfg.max_active_tx = static_cast<std::uint32_t>(std::stoul(optarg)); break;
            case 'k': cfg.num_keys      = static_cast<std::uint32_t>(std::stoul(optarg)); break;
            case 'b': cfg.data_size     = static_cast<std::uint32_t>(std::stoul(optarg)); break;
            default:  return false;
        }
    }

    if (cfg.user.empty()) {
        std::cerr << "Error: --user is mandatory.\n";
        return false;
    }

    if (cfg.password.empty()) {
        std::cerr << "Error: --password is mandatory.\n";
        return false;
    }

    if (cfg.servers.empty()) {
        std::cerr << "Error: --servers is mandatory.\n";
        return false;
    }

    if (cfg.refresh_secs == 0) {
        std::cerr << "Error: --refresh must be greater than 0.\n";
        return false;
    }

    if (cfg.tx_per_sec == 0) {
        std::cerr << "Error: --tx-per-second must be greater than 0.\n";
        return false;
    }

    if (cfg.max_active_tx == 0) {
        std::cerr << "Error: --max-active-tx must be greater than 0.\n";
        return false;
    }

    if (cfg.num_keys == 0) {
        std::cerr << "Error: --num-keys must be greater than 0.\n";
        return false;
    }

    if (cfg.data_size == 0) {
        std::cerr << "Error: --data-size must be greater than 0.\n";
        return false;
    }

    return true;
}

static std::string generate_random_data(std::uint32_t size)
{
    static constexpr char charset[] =
        "abcdefghijklmnopqrstuvwxyz"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "0123456789";

    std::uniform_int_distribution<std::size_t> dist(0, sizeof(charset) - 2);
    std::string result;

    result.reserve(size);

    for (std::uint32_t i = 0; i < size; ++i)
        result.push_back(charset[dist(g_rng)]);

    return result;
}

static std::string create_random_key()
{
    static size_t counter = 0;

    std::uniform_int_distribution<std::size_t> prefix_dist(0, DICTIONARY.size() - 1);
    const auto &entry = DICTIONARY[prefix_dist(g_rng)];

    std::uniform_int_distribution<std::size_t> suffix_dist(0, entry.suffixes.size() - 1);
    const char *suffix = entry.suffixes[suffix_dist(g_rng)];

    return std::string(entry.prefix) + ".t" + std::to_string(++counter) + "." + suffix;
}

static std::vector<std::string> complete_keys(const std::vector<std::string> &existing_keys, std::uint32_t num)
{
    std::set<std::string> unique_keys(existing_keys.begin(), existing_keys.end());

    while (unique_keys.size() < num)
    {
        std::string key = create_random_key();
        unique_keys.insert(std::move(key));
    }

    return std::vector<std::string>(unique_keys.begin(), unique_keys.end());
}

static std::vector<std::string_view> pick_random_keys(size_t num)
{
    static std::vector<size_t> indices;

    if (num >= keys.size())
        return std::vector<std::string_view>(keys.begin(), keys.end());

    if (indices.size() != keys.size()) {
        indices.resize(keys.size());
        std::iota(indices.begin(), indices.end(), 0);
    }

    // Partial Fisher-Yates
    for (size_t i = 0; i < num; ++i) {
        std::uniform_int_distribution<size_t> dist(i, keys.size() - 1);
        std::swap(indices[i], indices[dist(g_rng)]);
    }

    std::vector<std::string_view> result;
    result.reserve(num);

    for (size_t i = 0; i < num; ++i)
        result.push_back(keys[indices[i]]);

    return result;
}

static std::string format_bytes(std::uint64_t bytes)
{
    if (bytes >= 1024ULL * 1024ULL * 1024ULL) {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(1) << static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0) << "Gb";
        return oss.str();
    }

    if (bytes >= 1024ULL * 1024ULL) {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(1) << static_cast<double>(bytes) / (1024.0 * 1024.0) << "Mb";
        return oss.str();
    }

    if (bytes >= 1024ULL) {
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(0) << static_cast<double>(bytes) / 1024.0 << "Kb";
        return oss.str();
    }

    return std::to_string(bytes) + "b";
}

static std::string current_time_str()
{
    auto now = std::chrono::system_clock::now();
    auto tt = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf{};

    localtime_r(&tt, &tm_buf);
    std::ostringstream oss;
    oss << std::put_time(&tm_buf, "%H:%M:%S");

    return oss.str();
}

static void print_header()
{
    std::cout << std::left
              << std::setw(10) << "TIME"
              << std::right
              << std::setw(9)  << "#submits"
              << std::setw(10) << "#commits"
              << std::setw(10) << "#rejects"
              << std::setw(8)  << "avgtime"
              << std::setw(9)  << "#updates"
              << std::setw(9)  << "#updkeys"
              << std::setw(10) << "#updbytes"
              << "\n";

    std::cout << std::string(102, '-') << "\n";
}

static void print_stats(const submit_stats_t &ss, const reactor_stats_t &rs)
{
    auto total = ss.commits + ss.rejects;
    std::int64_t avg_us = (total > 0) ? (ss.sum_us / static_cast<std::int64_t>(total)) : 0;

    std::cout << std::left
              << std::setw(10) << current_time_str()
              << std::right
              << std::setw(9)  << ss.submits
              << std::setw(10) << ss.commits
              << std::setw(10) << ss.rejects
              << std::setw(8)  << avg_us
              << std::setw(9)  << rs.updates
              << std::setw(9)  << rs.upd_keys
              << std::setw(10) << format_bytes(rs.upd_bytes)
              << "\n" << std::flush;
}

static nplex::tx_ptr create_tx(nplex::client_ptr &cli, const config_t &cfg)
{
    auto tx = cli->create_tx(nplex::transaction::isolation_e::READ_COMMITTED, false);
    auto tx_keys = pick_random_keys(1);

    for (const auto &key : tx_keys)
    {
        auto value = generate_random_data(cfg.data_size);
        tx->upsert(key.data(), value);
    }

    return tx;
}

static void collect_result(slot_t &slot, submit_stats_t &stats, sclock::time_point now)
{
    auto result = slot.future.get();

    if (result == nplex::transaction::submit_e::COMMITTED)
        stats.commits++;
    else
        stats.rejects++;

    stats.sum_us += duration_cast<microseconds>(now - slot.start).count();

    slot.tx = nullptr;
}

static bool is_ready(const slot_t &slot) {
    return (slot.tx && slot.future.wait_for(microseconds(0)) == std::future_status::ready);
}

static bool is_active(const slot_t &slot) {
    return slot.tx != nullptr;
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main(int argc, char *argv[])
{
    config_t cfg;

    if (!parse_args(argc, argv, cfg)) {
        print_help(argv[0]);
        return EXIT_FAILURE;
    }

    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    try {
        nplex::params_t params = {
            .servers        = cfg.servers,
            .user           = cfg.user,
            .password       = cfg.password,
            .max_active_txs = cfg.max_active_tx,
        };

        auto cli = nplex::client::create(params);

        auto log = std::make_shared<logger>(nplex::logger::log_level_e::WARN);
        cli->set_logger(log);

        auto reactor = std::make_shared<flooder_reactor>();
        cli->set_reactor(reactor);

        std::jthread worker([cli](std::stop_token st) {
            cli->run(std::move(st));
        });

        cli->wait_for_synced();

        // Create the set of keys
        auto existing_keys = reactor->take_existing_keys();
        keys = complete_keys(std::move(existing_keys), cfg.num_keys);

        print_header();

        // Create the pool of active transactions (slots)
        std::uint32_t pool_size = std::min(cfg.max_active_tx, cfg.tx_per_sec);
        std::vector<slot_t> slots(pool_size);

        submit_stats_t period_stats{};
        auto time_next_second = sclock::now() + seconds(1);
        auto time_next_refresh = sclock::now() + seconds(cfg.refresh_secs);
        std::uint64_t tps_target = cfg.tx_per_sec;
        std::uint64_t tps_current = 0;

        // Main loop
        while (!g_stop.load(std::memory_order_relaxed) && !cli->is_closed())
        {
            bool any_active = false;
            auto now = sclock::now();

            for (auto &slot : slots)
            {
                // Collect txs results
                if (is_ready(slot))
                    collect_result(slot, period_stats, now);

                // Place new txs
                if (!is_active(slot) && tps_current < tps_target)
                {
                    auto tx = create_tx(cli, cfg);

                    slot.tx = std::move(tx);
                    slot.start = now;
                    slot.future = slot.tx->submit(true);

                    period_stats.submits++;
                    tps_current++;
                }

                any_active |= is_active(slot);
            }

            auto time_next = std::min(time_next_second, time_next_refresh);
            auto delay = microseconds(30);

            if (time_next <= now)
                delay = microseconds(0);
            else if (!any_active && tps_current >= tps_target)
                delay = duration_cast<microseconds>(time_next - now);

            // Reset TPS counter every second
            if (now >= time_next_second) {
                time_next_second += seconds(1);
                tps_current = 0;
            }

            // Print stats every refresh_secs seconds
            if (now >= time_next_refresh) {
                time_next_refresh += seconds(cfg.refresh_secs);
                auto rs = reactor->swap_stats();
                print_stats(period_stats, rs);
                period_stats = {};
            }

            // Sleeping to avoid busy-waiting when possible
            if (delay > microseconds(0) && !g_stop.load(std::memory_order_relaxed)){
                std::this_thread::sleep_for(delay);
                continue;
            }
        }

        // Shutdown
        worker.request_stop();
        slots.clear();

        return EXIT_SUCCESS;
    }
    catch (const std::exception &e) {
        std::cerr << "Error: " << e.what() << '\n';
        return EXIT_FAILURE;
    }
}
