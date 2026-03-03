#include <ctime>
#include <sstream>
#include <iomanip>
#include <arpa/inet.h>
#include "nplex-cpp/types.hpp"
#include "utils.hpp"

const gto::cstring nplex::value_t::EMPTY = "";

std::uint32_t nplex::ntohl_ptr(const char *ptr)
{
    return ntohl(*reinterpret_cast<const std::uint32_t *>(ptr));
}

const char * nplex::to_str(client_impl::state_e state)
{
    switch (state)
    {
        case client_impl::state_e::OFFLINE:             return "OFFLINE";
        case client_impl::state_e::CONNECTING:          return "CONNECTING";
        case client_impl::state_e::AUTHENTICATED:       return "AUTHENTICATED";
        case client_impl::state_e::LOADING_SNAPSHOT:    return "LOADING_SNAPSHOT";
        case client_impl::state_e::INITIALIZED:         return "INITIALIZED";
        case client_impl::state_e::SYNCING:             return "SYNCING";
        case client_impl::state_e::SYNCED:              return "SYNCED";
        case client_impl::state_e::CLOSED:              return "CLOSED";
        default:                                        return "UNKNOWN";
    }
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
