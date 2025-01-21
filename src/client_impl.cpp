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
        connect(params.servers);
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

void nplex::client_t::impl_t::connect(const addr_t &addr)
{
    int rc = 0;

    // TODO: Check if the client is already connected
    // TODO: manage on-fly messages (clear + reject tx)

    state = client_t::state_e::CONNECTING;

    struct sockaddr_storage addr_in = get_sockaddr(loop.get(), addr);

    assert(con);
    if ((rc = uv_tcp_init(loop.get(), con.get())) != 0)
        throw nplex_exception(uv_strerror(rc));

    uv_connect_t* connect = NULL;
    if ((connect = (uv_connect_t*) malloc(sizeof(uv_connect_t))) == NULL)
        throw std::bad_alloc();

    if ((rc = uv_tcp_connect(connect, con.get(), (const struct sockaddr*) &addr_in, cb_tcp_connect)) != 0) {
        free(connect);
        throw nplex_exception(uv_strerror(rc));
    }
}

void nplex::client_t::impl_t::do_login()
{
    state = nplex::client_t::state_e::LOGGING_IN;

    send(
        create_login_msg(
            correlation++, 
            params.user, 
            params.password
        )
    );
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

void nplex::client_t::impl_t::close()
{
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

void nplex::client_t::impl_t::process_commands()
{
    nplex::command_t cmd;

    while (commands.try_pop(cmd))
    {
        std::visit([this, &cmd](auto&& arg)
        {
            using T = std::decay_t<decltype(arg)>;

            if constexpr (std::is_same_v<T, nplex::submit_cmd_t>)
                process_submit_cmd(get<T>(cmd));
            else if constexpr (std::is_same_v<T, nplex::close_cmd_t>)
                process_close_cmd(get<T>(cmd));
            else if constexpr (std::is_same_v<T, nplex::ping_cmd_t>)
                process_ping_cmd(get<T>(cmd));
            else
                static_assert(false, "non-exhaustive visitor!");
        }, cmd);
    }
}

void nplex::client_t::impl_t::process_submit_cmd(const nplex::submit_cmd_t &cmd)
{
    UNUSED(cmd);
    // TODO: implement
}

void nplex::client_t::impl_t::process_close_cmd(const nplex::close_cmd_t &cmd)
{
    UNUSED(cmd);
    // TODO: implement
}

void nplex::client_t::impl_t::process_ping_cmd(const nplex::ping_cmd_t &cmd)
{
    UNUSED(cmd);
    // TODO: implement
}

void nplex::client_t::impl_t::process_recv_msg(const nplex::msgs::Message *msg)
{
    if (!msg || !msg->content()) {
        // TODO: process error -> disconnect
        error = "Invalid message";
        return;
    }

    switch (msg->content_type())
    {
        case msgs::MsgContent::LOGIN_RESPONSE:
            process_login_resp(msg->content_as_LOGIN_RESPONSE());
            break;

        case msgs::MsgContent::LOAD_RESPONSE:
            process_load_resp(msg->content_as_LOAD_RESPONSE());
            break;

        case msgs::MsgContent::SUBMIT_RESPONSE:
            process_submit_resp(msg->content_as_SUBMIT_RESPONSE());
            break;

        case msgs::MsgContent::UPDATE_PUSH:
            process_update_push(msg->content_as_UPDATE_PUSH());
            break;

        case msgs::MsgContent::KEEPALIVE_PUSH:
            process_keepalive_push(msg->content_as_KEEPALIVE_PUSH());
            break;

        case msgs::MsgContent::PING_RESPONSE:
            process_ping_resp(msg->content_as_PING_RESPONSE());
            break;

        default:
            // TODO: process error
            error = "Invalid message";
    }
}

void nplex::client_t::impl_t::process_login_resp(const nplex::msgs::LoginResponse *resp)
{
    // save server.crev
    // retrieve request using cid

    if (resp->code() != msgs::LoginCode::AUTHORIZED)
    {
        // TODO: process error
        error = "Invalid message";
        return;
    }

    auto cmd = parent.on_connect(server_addr.str(), resp->rev0(), resp->crev());

    msgs::LoadMode mode = msgs::LoadMode::SNAPSHOT_AT_LAST_REV;

    switch (cmd.first)
    {
        case load_mode_e::SNAPSHOT_AT_FIXED_REV:
            mode = msgs::LoadMode::SNAPSHOT_AT_FIXED_REV;
            break;

        case load_mode_e::SNAPSHOT_AT_LAST_REV:
            mode = msgs::LoadMode::SNAPSHOT_AT_LAST_REV;
            break;

        case load_mode_e::ONLY_UPDATES_FROM_REV:
            mode = msgs::LoadMode::ONLY_UPDATES_FROM_REV;
            break;

        default:
            break;
    }

    send(
        create_load_msg(
            correlation++, 
            mode, 
            cmd.second
        )
    );
}

void nplex::client_t::impl_t::process_load_resp(const nplex::msgs::LoadResponse *resp)
{
    // save server.crev
    // retrieve request using cid

    if (!resp->accepted())
    {
        // TODO: process error
        error = "Load rejected";
        return;
    }

    // TODO: manage state

    if (resp->snapshot())
        cache->restore(resp->snapshot());
}

void nplex::client_t::impl_t::process_submit_resp(const nplex::msgs::SubmitResponse *resp)
{
    // save server.crev
    // retrieve request using cid
    // retrieve tx using cid

    if (resp->code() != msgs::SubmitCode::ACCEPTED)
    {
        // TODO: process error
        error = "Load rejected";
        //parent.on_reject(tx);
        return;
    }

    // update tx status
}

void nplex::client_t::impl_t::process_update_push(const nplex::msgs::UpdatePush *resp)
{
    UNUSED(resp);
    // TODO: implement
}

void nplex::client_t::impl_t::process_keepalive_push(const nplex::msgs::KeepAlivePush *resp)
{
    UNUSED(resp);
    // TODO: implement
}

void nplex::client_t::impl_t::process_ping_resp(const nplex::msgs::PingResponse *resp)
{
    UNUSED(resp);
    // TODO: implement
}
