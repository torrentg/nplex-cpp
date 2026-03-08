#include <ctime>
#include <sstream>
#include <iomanip>
#include <arpa/inet.h>
#include <fmt/format.h>
#include "nplex-cpp/types.hpp"
#include "user.hpp"
#include "utils.hpp"

const gto::cstring nplex::value_t::EMPTY = "";

std::uint32_t nplex::ntohl_ptr(const char *ptr)
{
    return ntohl(*reinterpret_cast<const std::uint32_t *>(ptr));
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

std::string nplex::crud_to_string(std::uint8_t mode)
{
    return std::string{
        ((mode & CRUD_CREATE) ? 'c' : '-'),
        ((mode & CRUD_READ)   ? 'r' : '-'),
        ((mode & CRUD_UPDATE) ? 'u' : '-'),
        ((mode & CRUD_DELETE) ? 'd' : '-')
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
