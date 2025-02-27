#include <map>
#include <iostream>
#include <uv.h>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <thread>
#include <fmt/std.h>
#include "nplex-cpp/client.hpp"

std::size_t count_key_parts(const nplex::key_t &key, char delimiter = KEY_DELIMITER)
{
    std::string_view key_view(key.c_str());
    std::size_t start = 0;
    std::size_t end = 0;
    std::size_t part_count = 0;

    if (!key_view.empty() && key_view[0] == delimiter)
        key_view.remove_prefix(1);

    while ((end = key_view.find(delimiter, start)) != std::string_view::npos) {
        part_count++;
        start = end + 1;
    }

    if (start < key_view.size()) {
        part_count++;
    }

    return part_count;
}

std::string_view get_key_part(const nplex::key_t &key, std::size_t index, char delimiter = KEY_DELIMITER)
{
    std::string_view key_view(key.c_str());
    std::size_t start = 0;
    std::size_t end = 0;
    std::size_t part_index = 0;

    while ((end = key_view.find(delimiter, start)) != std::string_view::npos) {
        if (part_index == index) {
            return key_view.substr(start, end - start);
        }
        part_index++;
        start = end + 1;
    }

    if (start < key_view.size() && part_index == index) {
        return key_view.substr(start);
    }

    return std::string_view{};
}

struct sensor_t
{
    std::string name;
    struct {
        std::uint32_t x;
        std::uint32_t y;
        std::uint32_t z;
    } position;
    std::uint32_t min_val;
    std::uint32_t max_val;
    std::uint32_t threshold;
    std::uint32_t value;
    bool active;
};

class my_listener_t : public nplex::listener_t
{
  private:

    const std::int32_t millis_between_attempts = 3000;
    const std::size_t max_failed_attempts = 3;

    std::size_t num_failed_attempts = 0;
    nplex::rev_t last_rev = 0;

    std::map<std::string, sensor_t> sensors;

  private:

    bool update_sensors(const nplex::key_t &key, const nplex::value_t &value)
    {
        if (get_key_part(key, 0) != "sensors")
            return false;

        auto num_parts = count_key_parts(key);
        if (num_parts != 3 && num_parts != 4)
            return false;

        std::string id{get_key_part(key, 1)};

        auto it = sensors.find(id);
        if (it == sensors.end()) {
            it = sensors.emplace(id, sensor_t{}).first;
            it->second.name = id;
        }

        auto &obj = it->second;

        auto attr = get_key_part(key, 2);

        if (num_parts == 3)
        {
            if (attr == "min_val") {
                obj.min_val = value.as_number<std::uint32_t>();
            } else if (attr == "max_val") {
                obj.max_val = value.as_number<std::uint32_t>();
            } else if (attr == "threshold") {
                obj.threshold = value.as_number<std::uint32_t>();
            } else if (attr == "value") {
                obj.value = value.as_number<std::uint32_t>();
            } else if (attr == "active") {
                obj.active = value.as_bool();
            } else {
                return false;
            }
        }
        else // 4 parts
        {
            if (attr == "position") {
                auto dim = get_key_part(key, 3);
                if (dim == "x")
                    obj.position.x = value.as_number<std::uint32_t>();
                else if (dim == "y")
                    obj.position.y = value.as_number<std::uint32_t>();
                else if (dim == "z")
                    obj.position.z = value.as_number<std::uint32_t>();
                else
                    return false;
            } else {
                return false;
            }
        }

        return true;
    }

    bool remove_sensor(const nplex::key_t &key)
    {
        if (get_key_part(key, 0) != "sensors")
            return false;

        std::string id{get_key_part(key, 1)};

        return (sensors.erase(id) > 0);
    }

  public:

    using listener_t::listener_t;

  protected:

    load_cmd_t on_connected([[maybe_unused]] nplex::client_t &client, const std::string &server, nplex::rev_t oldest_rev, nplex::rev_t newest_rev) override
    {
        fmt::print("Nplex client connected to server {}, available revs = [{}, {}]\n", server, oldest_rev, newest_rev);

        num_failed_attempts = 0;

        if (last_rev == 0)
            return {load_mode_e::SNAPSHOT_AT_LAST_REV, 0};
        else
            return {load_mode_e::ONLY_UPDATES_FROM_REV, last_rev};
    }

    std::int32_t on_connection_lost([[maybe_unused]] nplex::client_t &client, const std::string &server) override
    {
        // Case: failed to connect to cluster
        if (server.empty())
        {
            fmt::print(stderr, "Error: unable to connect to nplex cluster\n");

            if (++num_failed_attempts >= max_failed_attempts) {
                fmt::print(stderr, "Error: closing nplex client after {} reconnection attempts\n", max_failed_attempts);
                return -1;
            }

            fmt::print("Next connection attempt in {} ms\n", millis_between_attempts);
            return millis_between_attempts;
        }

        // Case: current connection was lost
        fmt::print("Connection lost to {}\n", server);

        // try to reconnect immediately
        return 0;
    }

    void on_closed([[maybe_unused]] nplex::client_t &client) override
    {
        sensors.clear();
        fmt::print("Nplex client closed\n");
    }

    void on_snapshot(nplex::client_t &client) override
    {
        fmt::print("Received snapshot at rev {}\n", client.rev());

        // we need a tx to access the database content
        auto tx = client.create_tx(nplex::transaction_t::isolation_e::READ_COMMITTED, true);

        // initializing bussiness objects with database content
        tx->for_each("/sensors/*/*", [this](const gto::cstring &key, const nplex::value_t &value) {
            update_sensors(key, value);
            return true;
        });

        last_rev = tx->rev();

        fmt::print("Created {} sensors at rev {}\n", sensors.size(), last_rev);

        for (const auto &[key, obj] : sensors)
            fmt::print("{} = [ activated={}, position=[{},{},{}], amount={} ]\n", 
                key, obj.active, obj.position.x, obj.position.y, obj.position.z , obj.value);
    }

    void on_update([[maybe_unused]] nplex::client_t &client, const nplex::meta_ptr &meta, const std::vector<nplex::change_t> &changes) override
    {
        fmt::print("Received update at rev {}\n", meta->rev);

        for (const auto &change : changes)
        {
            switch (change.action)
            {
                case nplex::change_t::action_e::CREATE:
                    fmt::print("CREATE: key={}, value={}\n", change.key, change.value->data());
                    update_sensors(change.key, *change.value);
                    break;
                case nplex::change_t::action_e::UPDATE:
                    fmt::print("UPDATE: key={}, new_value={}, prev_value={}\n", change.key, change.value->data(), change.old_value->data());
                    update_sensors(change.key, *change.value);
                    // if increment > theshold -> create alarm
                    break;
                case nplex::change_t::action_e::DELETE:
                    fmt::print("DELETE: key={}\n", change.key);
                    remove_sensor(change.key);
                    break;
            }
        }

        last_rev = meta->rev;
    }

    void log([[maybe_unused]] nplex::client_t &client, log_level_e severity, const std::string &msg) override
    {
        // Get the current time
        auto now = std::chrono::system_clock::now();
        auto now_time_t = std::chrono::system_clock::to_time_t(now);
        auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

        // Format the time
        std::ostringstream time_stream;
        time_stream << std::put_time(std::localtime(&now_time_t), "%Y-%m-%d %H:%M:%S")
                    << '.' << std::setfill('0') << std::setw(3) << now_ms.count();

        // Get the thread ID
        auto thread_id = std::this_thread::get_id();

        // Format the severity
        const char *str_severity = nullptr;
        switch (severity)
        {
            case log_level_e::DEBUG: str_severity = "[DEBUG]"; break;
            case log_level_e::INFO:  str_severity = "[INFO] ";  break;
            case log_level_e::WARN:  str_severity = "[WARN] ";  break;
            case log_level_e::ERROR: str_severity = "[ERROR]"; break;
            default: str_severity = "[PANIC]"; break;
        }

        // Format and print the log message
        fmt::print("{} [{}] {} - {}\n", time_stream.str(), thread_id, str_severity, msg);
    }
};

int main(int argc, char *argv[])
{
    std::string servers = "localhost:14022, localhost:8081";
    std::string user = "admin";
    std::string passwd = "s3cr3t";

    try {
        my_listener_t listener{nplex::listener_t::log_level_e::DEBUG};
        nplex::client_t nplex{{servers, user, passwd}, listener};
        nplex.join();
    }
    catch(const std::exception &e) {
        fmt::print(stderr, "Error: {}\n", e.what());
        return 1;
    }
}
