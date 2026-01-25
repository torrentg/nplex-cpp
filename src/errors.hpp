#pragma once

#include <string>
#include <uv.h>
#include <fmt/format.h>

#define ERR_CLOSED_BY_LOCAL     1000
#define ERR_CLOSED_BY_PEER      1001
#define ERR_MSG_ERROR           1002
#define ERR_MSG_UNEXPECTED      1003
#define ERR_MSG_SIZE            1004
#define ERR_ALREADY_CONNECTED   1005
#define ERR_KEEPALIVE           1006
#define ERR_AUTH                1007
#define ERR_LOAD                1008

namespace nplex {

inline std::string strerror(int error)
{
    if (error < 0)
        return uv_strerror(error);

    switch (error)
    {
        case ERR_CLOSED_BY_LOCAL: return "closed by local";
        case ERR_CLOSED_BY_PEER: return "closed by peer";
        case ERR_MSG_ERROR: return "invalid message";
        case ERR_MSG_UNEXPECTED: return "unexpected message";
        case ERR_MSG_SIZE: return "message too large";
        case ERR_ALREADY_CONNECTED: return "already connected";
        case ERR_KEEPALIVE: return "keepalive not received";
        case ERR_AUTH: return "unauthorized";
        case ERR_LOAD: return "load request rejected";
        default: return fmt::format("unknow error -{}-", error);
    }
}

}