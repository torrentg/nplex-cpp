#pragma once

#include <vector>
#include "nplex-cpp/types.hpp"
#include "addr.hpp"

namespace nplex {

struct connection_params_t
{
    std::uint32_t max_unack_msgs = UINT32_MAX;  //!< Maximum number of output infly messages (> 0).
    std::uint32_t max_unack_bytes = UINT32_MAX; //!< Maximum bytes of output infly messages (> 0).
    float timeout_factor = 2.5;                 //!< Timeout factor (> 1.0).
};

struct client_params_t
{
    std::string user;                           //!< User name.
    std::string password;                       //!< User password.
    std::vector<addr_t> servers;                //!< List of servers.
    std::uint32_t max_active_txs = UINT32_MAX;  //!< Maximum number of concurrent transactions (> 0).
    connection_params_t connection;             //!< Connection parameters.
};

} // namespace nplex
