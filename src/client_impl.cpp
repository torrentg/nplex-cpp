#include <cassert>
#include "client_impl.hpp"

/**
 * Notes on this compilation unit:
 * 
 * When we use the libuv library we apply C conventions (instead of C++ ones):
 *   - When in Rome, do as the Romans do.
 *   - calloc/free are used instead of new/delete.
 *   - pointers to static functions.
 *   - C-style pointer casting.
 */

nplex::client_t::impl_t::impl_t(client_t &parent_, const params_t &params_) : 
    parent(parent_),
    params(params_), 
    input_buffer{0},
    commands(params.max_num_queued_commands),
    state{client_t::state_e::INITIALIZING}
{
    if (params.servers.empty())
        throw nplex_exception("Invalid params: no servers");

    cache = std::make_shared<cache_t>();
    async = std::make_unique<uv_async_t>();
    loop = std::make_unique<uv_loop_t>();
    con = std::make_unique<uv_tcp_t>();
}

void nplex::client_t::impl_t::run() noexcept
{
    std::unique_lock<decltype(m_mutex)> guard(m_mutex);

    if (state != client_t::state_e::INITIALIZING)
        return;

    assert(loop);
    if (!loop || uv_loop_init(loop.get()) != 0) {
        error = "Error initializing event loop (uv_loop_init)";
        return;
    }

    loop->data = this;

    assert(async);
    if (!async || uv_async_init(loop.get(), async.get(), cb_process_async) != 0) {
        error = "Error initializing event loop (uv_async_init)";
        return;
    }

    try
    {
        connect(connect_cmd_t{params.servers});
        guard.unlock();
        uv_run(loop.get(), UV_RUN_DEFAULT);
    }
    catch (const std::exception &e) {
        error = e.what();
    }
    catch (...) {
        error = "Unknown error in the event loop";
    }

    uv_walk(loop.get(), cb_close_handle, NULL);
    while (uv_run(loop.get(), UV_RUN_NOWAIT));
    uv_loop_close(loop.get());

    parent.on_close();

    return;
}

void nplex::client_t::impl_t::connect(const nplex::connect_cmd_t &cmd)
{
    // TODO: Check if the client is already connected
    // TODO: manage on-fly messages (clear + reject tx)

    state = client_t::state_e::CONNECTING;

    int rc = 0;
    uv_connect_t* connect = NULL;
    struct sockaddr_storage addr_in = get_sockaddr(loop.get(), cmd.server);

    if ((rc = uv_tcp_init(loop.get(), con.get())) != 0)
        throw nplex_exception(uv_strerror(rc));

    if ((connect = (uv_connect_t*) malloc(sizeof(uv_connect_t))) == NULL)
        throw std::bad_alloc();

    if ((rc = uv_tcp_connect(connect, con.get(), (const struct sockaddr*) &addr_in, cb_tcp_connect)) != 0) {
        free(connect);
        throw nplex_exception(uv_strerror(rc));
    }
}

void nplex::client_t::impl_t::disconnect()
{
    switch (state)
    {
        case client_t::state_e::LOGGING_IN:
        case client_t::state_e::SYNCHRONIZING:
        case client_t::state_e::SYNCED:
            state = client_t::state_e::DISCONNECTING;
            uv_close((uv_handle_t *) con.get(), NULL);
            break;
        default:
            break;
    }
}

void nplex::client_t::impl_t::close(const close_cmd_t &cmd)
{
    UNUSED(cmd);

    switch (state)
    {
        case client_t::state_e::INITIALIZING:
            state = client_t::state_e::CLOSED;
            return;

        case client_t::state_e::CLOSING:
        case client_t::state_e::CLOSED:
            return;

        default:
            state = client_t::state_e::CLOSING;
            commands.clear();
            disconnect();
            break;
    }

    uv_stop(loop.get());
}

void nplex::client_t::impl_t::send(flatbuffers::DetachedBuffer &&buf)
{
    output_msg_t msg(std::move(buf));

    // TODO: add msg to msg_queue
    // rft_tcp_msg_t *msg = con->output_queue_front;

    uv_write(&msg.req, (uv_stream_t *) con.get(), msg.buf.data(), msg.buf.size(), cb_tcp_write);
}

void nplex::client_t::impl_t::load(const nplex::load_cmd_t &cmd)
{
    send(
        create_load_msg(
            correlation++, 
            cmd.load_mode, 
            cmd.rev
        )
    );
}
