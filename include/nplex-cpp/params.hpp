#pragma once

#include "types.hpp"

namespace nplex {

struct params_t
{
    std::string servers;                        //!< Comma-separated list of host:port.
    std::string user;                           //!< User name.
    std::string password;                       //!< User password.
    std::uint32_t max_num_concurrent_tx;        //!< Maximum number of concurrent transactions.
    std::uint32_t max_num_queued_commands;      //!< Maximum number of pending commands.
    std::uint32_t max_msg_bytes;                //!< Maximum message size.
    std::uint32_t max_unakc_msgs;               //!< Maximum number of output infly messages.
    std::uint32_t max_unakc_bytes;              //!< Maximum bytes of output infly messages.

    std::uint32_t timeout;                      //!< Connection timeout.
    std::uint32_t reconnect;                    //!< Reconnect interval.
    std::uint32_t max_retries;                  //!< Maximum number of retries.
    std::uint32_t max_pending;                  //!< Maximum number of pending commands.
    std::uint32_t max_transactions;             //!< Maximum number of transactions.
    std::uint32_t max_tx_size;                  //!< Maximum number of items per transaction.
    std::uint32_t max_tx_time;                  //!< Maximum time per transaction.
    std::uint32_t max_tx_pending;               //!< Maximum number of pending transactions.
    std::uint32_t max_tx_queue_size;            //!< Maximum size of the transaction queue.
    std::uint32_t max_tx_queue_time;            //!< Maximum time of the transaction queue.


    // TODO: create constructor
    // TODO: add setters returning ref to obj
};

}; // namespace nplex
