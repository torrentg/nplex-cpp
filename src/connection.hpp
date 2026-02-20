#pragma once

#include <memory>
#include "addr.hpp"

#define ERR_CLOSED_BY_LOCAL     1000
#define ERR_CLOSED_BY_PEER      1001
#define ERR_MSG_ERROR           1002
#define ERR_MSG_UNEXPECTED      1003
#define ERR_MSG_SIZE            1004
#define ERR_ALREADY_CONNECTED   1005
#define ERR_CON_LOST            1006
#define ERR_AUTH                1007
#define ERR_LOAD                1008
#define ERR_SIGNAL              1009

// Forward declarations
struct uv_loop_s;
typedef struct uv_loop_s uv_loop_t;

// Forward declaration
namespace flatbuffers {
    class DetachedBuffer;
}

namespace nplex {

// Forward declarations
struct connection_params_t;

/**
 * Class representing a network connection.
 * 
 * This class knows nothing about bussiness logic.
 * Deals with the event loop, messages, etc.
 * 
 * Main dutties are:
 * - Manage libuv resources
 * 
 * Callbacks to the client:
 * - client()->on_msg_received()
 * - client()->on_msg_delivered()
 * - client()->on_connection_established()
 * - client()->on_connection_closed()
 */
class connection
{
  public:

    /**
     * Create a new connection object.
     * 
     * @param[in] addr Remote address.
     * @param[in] loop Event loop to use (loop->data contains a client_impl *).
     * @param[in] params Connection parameters.
     * 
     * @return The connection object.
     * 
     * @exception nplex_exception If invalid address or resources could not be allocated.
     */
    static std::unique_ptr<connection> create(const addr_t &addr, uv_loop_t *loop, const connection_params_t &params);

    virtual ~connection() = default;
    virtual const addr_t & addr() const = 0;
    virtual bool is_connected() const = 0;
    virtual bool is_closed() const = 0;
    virtual int error() const = 0;

    /**
     * Connect to the remote server.
     * 
     * This method returns immediately. 
     * Connection result is notified via:
     *   - client()->on_connection_established() on success.
     *   - client()->on_connection_closed() on failure.
     * 
     * @exception nplex_exception If connection could not be started.
     */
    virtual void connect() = 0;

    /**
     * Disconnect from the remote server.
     * 
     * This method returns immediately.
     * Disconnection is notified via:
     *   - client()->on_connection_closed().
     * 
     * @param[in] rc Disconnection cause (0 = graceful).
     */
    virtual void disconnect(int rc = 0) = 0;

    /**
     * Send a message to the remote server.
     * 
     * This method returns immediately.
     * Message delivery is notified via:
     *   - client()->on_msg_delivered().
     * 
     * @param[in] buf Message buffer.
     * 
     * @exception nplex_exception If connection is not established or
     *                             output queue is full.
     */
    virtual void send(flatbuffers::DetachedBuffer &&buf) = 0;

  protected:

    connection() = default;

  private:

    // non-copyable class
    connection(const connection &) = delete;
    connection & operator=(const connection &) = delete;
    // non-movable class
    connection(connection &&) = delete;
    connection & operator=(connection &&) = delete;
};

using connection_ptr = std::unique_ptr<connection>;

} // namespace nplex
