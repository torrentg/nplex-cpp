#include <ctime>
#include <sstream>
#include <iomanip>
#include <arpa/inet.h>
#include <fmt/core.h>
#include "utf8.h"
#include <uv.h>
#include "nplex-cpp/types.hpp"
#include "user.hpp"
#include "utils.hpp"

const gto::cstring nplex::value_t::EMPTY = "";

std::uint32_t nplex::ntohl_ptr(const char *ptr)
{
    return ntohl(*reinterpret_cast<const std::uint32_t *>(ptr));
}

bool nplex::is_valid_key(const std::string_view &key)
{
    const auto *ptr = reinterpret_cast<const utf8_int8_t *>(key.data());
    const auto *end = ptr + key.length();

    if (key.empty())
        return false;

    if (key.front() == ' ' || key.back() == ' ')
        return false;

    if (key.find("//") != std::string_view::npos)
        return false;

    if (key.find('\0') != std::string_view::npos)
        return false;

    if (utf8nvalid(ptr, key.length()) != 0)
        return false;

    // Check for control characters (C0, DEL, C1)
    while (ptr < end)
    {
        utf8_int32_t cp = 0;

        ptr = utf8codepoint(ptr, &cp);

        if ((cp >= 0x00 && cp <= 0x1F) || (cp >= 0x7F && cp <= 0x9F))
            return false;
    }

    return true;
}

std::string nplex::crud_to_string(std::uint8_t crud)
{
    return std::string{
        ((crud & CRUD_CREATE) ? 'c' : '-'),
        ((crud & CRUD_READ)   ? 'r' : '-'),
        ((crud & CRUD_UPDATE) ? 'u' : '-'),
        ((crud & CRUD_DELETE) ? 'd' : '-')
    };
}

std::uint8_t nplex::parse_crud(const std::string_view &str)
{
    static const char *crud_str = "crud";
    std::uint8_t crud = 0;

    if (str.size() != 4)
        throw std::invalid_argument(fmt::format("Invalid crud ({})", str));

    for (std::size_t i = 0; i < 4; i++)
    {
        char c = str[i];

        if (c != crud_str[i] && c != '-')
            throw std::invalid_argument(fmt::format("Invalid crud ({})", str));

        switch (c)
        {
            case 'c': crud |= CRUD_CREATE; break;
            case 'r': crud |= CRUD_READ;   break;
            case 'u': crud |= CRUD_UPDATE; break;
            case 'd': crud |= CRUD_DELETE; break;
            default: break;
        }
    }

    return crud;
}

std::string nplex::to_iso8601(std::chrono::milliseconds ms_since_epoch)
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

const char * nplex::to_str(transaction::state_e state)
{
    switch (state)
    {
        case transaction::state_e::OPEN:                return "OPEN";
        case transaction::state_e::SUBMITTED:           return "SUBMITTED";
        case transaction::state_e::ACCEPTED:            return "ACCEPTED";
        case transaction::state_e::REJECTED:            return "REJECTED";
        case transaction::state_e::COMMITTED:           return "COMMITTED";
        case transaction::state_e::DISCARDED:           return "DISCARDED";
        case transaction::state_e::ABORTED:             return "ABORTED";
        default:                                        return "UNKNOWN";
    }
}

const char * nplex::to_str(transaction::isolation_e isolation)
{
    switch (isolation)
    {
        case transaction::isolation_e::READ_COMMITTED:  return "READ_COMMITTED";
        case transaction::isolation_e::REPEATABLE_READ: return "REPEATABLE_READ";
        case transaction::isolation_e::SERIALIZABLE:    return "SERIALIZABLE";
        default:                                        return "UNKNOWN";
    }
}

const char * nplex::strerror(int error)
{
    if (error < 0)
        return uv_strerror(error);

    switch (error)
    {
        case ERR_CLOSED_BY_LOCAL:   return "closed by local";
        case ERR_CLOSED_BY_PEER:    return "closed by peer";
        case ERR_MSG_ERROR:         return "invalid message";
        case ERR_MSG_UNEXPECTED:    return "unexpected message";
        case ERR_QUEUE_EXCEEDED:    return "unack queue exceeded";
        case ERR_ALREADY_CONNECTED: return "already connected";
        case ERR_CON_LOST:          return "connection lost";
        case ERR_AUTH:              return "unauthorized";
        case ERR_LOAD:              return "snapshot request rejected";
        case ERR_SIGNAL:            return "signal received";
        default:                    return "unknown error";
    }
}
