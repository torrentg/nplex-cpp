#include <arpa/inet.h>
#include "utils.hpp"

std::uint32_t nplex::ntohl_ptr(const char *ptr)
{
    return ntohl(*reinterpret_cast<const std::uint32_t *>(ptr));
}

const char * nplex::to_str(transaction::isolation_e isolation)
{
    switch (isolation)
    {
        case transaction::isolation_e::READ_COMMITTED:    return "READ_COMMITTED";
        case transaction::isolation_e::REPEATABLE_READ:   return "REPEATABLE_READ";
        case transaction::isolation_e::SERIALIZABLE:      return "SERIALIZABLE";
        default:                                            return "UNKNOWN";
    }
}
