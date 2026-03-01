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

const char * nplex::to_str(transaction::isolation_e isolation)
{
    switch (isolation)
    {
        case transaction::isolation_e::READ_COMMITTED:   return "READ_COMMITTED";
        case transaction::isolation_e::REPEATABLE_READ:  return "REPEATABLE_READ";
        case transaction::isolation_e::SERIALIZABLE:     return "SERIALIZABLE";
        default:                                         return "UNKNOWN";
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
