#include <regex>
#include "nplex-cpp/exception.hpp"
#include "addr.hpp"

// IPv4 address (see https://digitalfortress.tech/tricks/top-15-commonly-used-regex/)
#define IPV4_PATTERN "^(([0-9]|[1-9][0-9]|1[0-9]{2}|2[0-4][0-9]|25[0-5])\\.){3}([0-9]|[1-9][0-9]|1[0-9]{2}|2[0-4][0-9]|25[0-5])$"
// IPv6 address (see https://ihateregex.io/expr/ipv6/)
#define IPV6_PATTERN "^(([0-9a-fA-F]{1,4}:){7,7}[0-9a-fA-F]{1,4}|([0-9a-fA-F]{1,4}:){1,7}:|([0-9a-fA-F]{1,4}:){1,6}:[0-9a-fA-F]{1,4}|([0-9a-fA-F]{1,4}:){1,5}(:[0-9a-fA-F]{1,4}){1,2}|([0-9a-fA-F]{1,4}:){1,4}(:[0-9a-fA-F]{1,4}){1,3}|([0-9a-fA-F]{1,4}:){1,3}(:[0-9a-fA-F]{1,4}){1,4}|([0-9a-fA-F]{1,4}:){1,2}(:[0-9a-fA-F]{1,4}){1,5}|[0-9a-fA-F]{1,4}:((:[0-9a-fA-F]{1,4}){1,6})|:((:[0-9a-fA-F]{1,4}){1,7}|:)|fe80:(:[0-9a-fA-F]{0,4}){0,4}%[0-9a-zA-Z]{1,}|::(ffff(:0{1,4}){0,1}:){0,1}((25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9])\\.){3,3}(25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9])|([0-9a-fA-F]{1,4}:){1,4}:((25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9])\\.){3,3}(25[0-5]|(2[0-4]|1{0,1}[0-9]){0,1}[0-9]))$"
// Hostname pattern (according to rfc-1123).
#define HOSTNAME_PATTERN "^(([a-zA-Z0-9]|[a-zA-Z0-9][a-zA-Z0-9\\-]{0,61}[a-zA-Z0-9])\\.)*([A-Za-z0-9]|[A-Za-z0-9][A-Za-z0-9\\-]{0,61}[A-Za-z0-9])$"

nplex::addr_t::addr_t(const std::string &str)
{
    auto pos = str.find_last_of(':');
    if (pos == str.npos)
        throw nplex_exception("'{}' is an invalid address (port not found)", str);

    if (str.size() - pos > 6)
        throw nplex_exception("'{}' is an invalid address (port too long)", str);

    for (size_t i = pos + 1; i < str.size(); i++) {
        if (!std::isdigit(str[i]))
            throw nplex_exception("'{}' is an invalid address (invalid port)", str);
    }

    int num = std::atoi(str.c_str() + pos + 1);
    if (num <= 0 || num > 65535)
        throw nplex_exception("'{}' is an invalid address (invalid port)", str);

    m_port = static_cast<std::uint16_t>(num);
    m_host = str.substr(0, pos);

    if (m_host.starts_with('[') && m_host.ends_with(']'))
    {
        m_host = m_host.substr(1, m_host.size() - 2);

        if (!std::regex_match(m_host, std::regex{IPV6_PATTERN, std::regex::extended | std::regex::nosubs}))
            throw nplex_exception("'{}' is an invalid IP6 address", str);

        m_family = AF_INET6;
    }
    else if (std::regex_match(m_host, std::regex{IPV4_PATTERN, std::regex::extended | std::regex::nosubs}))
    {
        m_family = AF_INET;
    }
    else if (std::regex_match(m_host, std::regex{HOSTNAME_PATTERN, std::regex::extended | std::regex::nosubs}))
    {
        m_family = AF_UNSPEC;
    }
    else
    {
        throw nplex_exception("'{}' is an invalid address (invalid host)", str);
    }
}
