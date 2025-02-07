#include <map>
#include <iostream>
#include <uv.h>
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

class my_client_t : public nplex::client_t
{
  private:

    const std::uint32_t millis_between_attempts = 1000;
    const std::size_t max_failed_attempts = 3;

    std::size_t num_failed_attempts = 0;
    std::uint64_t connection_timestamp = 0;
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

    using client_t::client_t;
    ~my_client_t() override  { fmt::print("my_client_t::~my_client_t()\n"); }

  protected:

    void on_close() override
    {
        sensors.clear();
        fmt::print("All objects removed\n");
    }
#if 0
    load_cmd_t on_connect(const std::string &server, nplex::rev_t oldest_rev, nplex::rev_t newest_rev) override
    {
        fmt::print("Connected to server {} having revs [{}, {}]\n", server, oldest_rev, newest_rev);

        num_failed_attempts = 0;
        connection_timestamp = uv_now(loop());

        if (last_rev == 0)
            return {load_mode_e::SNAPSHOT_AT_LAST_REV, 0};
        else
            return {load_mode_e::ONLY_UPDATES_FROM_REV, last_rev};
    }

    uint32_t on_connection_lost(const std::string &server) override
    {
        // Case: failed to connect to cluster
        if (server.empty())
        {
            fmt::print(stderr, "Error: unable to connect to nplex cluster\n");

            if (++num_failed_attempts >= max_failed_attempts) {
                fmt::print(stderr, "Error: closing nplex client after {} reconnection attempts\n", max_failed_attempts);
                return 0;
            }

            fmt::print("Next connection attempt in {} ms\n", millis_between_attempts);
            return millis_between_attempts;
        }

        // Case: current connection was lost
        auto millis = uv_now(loop()) - connection_timestamp;
        fmt::print("Connection lost to {} after {} ms\n", server, millis);
        connection_timestamp = 0;
        // try to reconnect immediately (1 ms delay)
        return 1;
    }

    void on_snapshot() override
    {
        fmt::print("Received snapshot at rev {}\n", rev());

        // we need a tx to access the database content
        auto tx = create_tx(nplex::transaction_t::isolation_e::READ_COMMITTED, true);

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

    void on_update(const nplex::meta_ptr &meta, const std::vector<nplex::change_t> &changes, nplex::tx_ptr tx) override
    {
        fmt::print("Received update at rev {}", meta->rev);

        if (tx)
            fmt::print(" from local tx\n", tx->rev());
        else
            fmt::print(" from user {}\n", meta->user);

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

    void on_reject(nplex::tx_ptr tx) override 
    {
        fmt::print("Transaction {} was rejected", tx->type());
    }
#endif
};

int main(int argc, char *argv[])
{
    std::string servers = "localhost:8080, localhost:8081";
    std::string user = "jdoe";
    std::string passwd = "s3cr3t";

    my_client_t nplex{{servers, user, passwd}};

    try {
        nplex.start();
    }
    catch(...) {
        fmt::print(stderr, "Error: unable to start nplex client\n");
        return 1;
    }
}
