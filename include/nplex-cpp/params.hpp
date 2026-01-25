#pragma once

#include <vector>
#include "types.hpp"

namespace nplex {

struct params_t
{
    std::vector<std::string> servers;           //!< List of servers (host:port).
    std::string user;                           //!< User name.
    std::string password;                       //!< User password.
    std::uint32_t max_active_txs = UINT32_MAX;  //!< Maximum number of concurrent transactions (0 = unlimited).
    std::uint32_t max_msg_bytes = UINT32_MAX;   //!< Maximum message size (0 = unlimited).
    std::uint32_t max_unack_msgs = UINT32_MAX;  //!< Maximum number of output infly messages (0 = unlimited).
    std::uint32_t max_unack_bytes = UINT32_MAX; //!< Maximum bytes of output infly messages (0 = unlimited).
    float timeout_factor = 2.5;                 //!< Timeout factor.

    // Default constructor.
    params_t() = default;

    /**
     * Create a new params object.
     * 
     * Server addresses have the format host:port where host is an IPv4, IPv6 or 
     * hostname an port is a numeric value in range [1, 65535].
     * 
     * @param servers_ List of comma-separated servers (host:port).
     * @param user_ Database user.
     * @param password_ Database password.
     */
    params_t(const std::string &servers_, const std::string &user_, const std::string &password_)
        : user(user_), password(password_)
    {
        std::string addr;
        std::istringstream ss(servers_);

        while (std::getline(ss, addr, ',')) {
            addr.erase(0, addr.find_first_not_of(' ')); // trim leading spaces
            addr.erase(addr.find_last_not_of(' ') + 1); // trim trailing spaces
            this->servers.push_back(addr);
        }
    }

};

} // namespace nplex
