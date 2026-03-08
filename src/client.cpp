#include "nplex-cpp/client.hpp"
#include "client_impl.hpp"

// ==========================================================
// Internal (static) functions
// ==========================================================

static nplex::client_params_t convert_params(const nplex::params_t &params)
{
    nplex::client_params_t ret;

    if (params.user.empty())
        throw nplex::nplex_exception("User not found");

    if (params.password.empty())
        throw nplex::nplex_exception("Password not found");

    ret.user = params.user;
    ret.password = params.password;
    ret.max_active_txs = (params.max_active_txs == 0 ? UINT32_MAX : params.max_active_txs);

    if (params.servers.empty())
        throw nplex::nplex_exception("Servers not found");

    std::string addr;
    std::istringstream ss(params.servers);

    while (std::getline(ss, addr, ',')) {
        addr.erase(0, addr.find_first_not_of(' ')); // trim leading spaces
        addr.erase(addr.find_last_not_of(' ') + 1); // trim trailing spaces
        ret.servers.push_back(nplex::addr_t{addr}); // validate address format
    }

    if (ret.servers.empty())
        throw nplex::nplex_exception("No valid servers found");

    if (params.timeout_factor <= 1.0)
        throw nplex::nplex_exception("Timeout factor <= 1.0");

    ret.connection.timeout_factor = params.timeout_factor;

    ret.connection.max_unack_msgs = (params.max_unack_msgs == 0 ? UINT32_MAX : params.max_unack_msgs);
    ret.connection.max_unack_bytes = (params.max_unack_bytes == 0 ? UINT32_MAX : params.max_unack_bytes);

    return ret;
}

// ==========================================================
// client definitions
// ==========================================================

nplex::client_ptr nplex::client::create(const params_t &params)
{
    auto cli_params = convert_params(params);

    return std::make_shared<client_impl>(cli_params);
}
