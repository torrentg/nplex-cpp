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

// ==========================================================
// Internal (static) functions
// ==========================================================

static void cb_process_async(uv_async_t *handle)
{
    nplex::client_t::impl_t *impl = (nplex::client_t::impl_t *) handle->data;
    impl->process_commands();
}

static void cb_close_handle(uv_handle_t *handle, void *arg)
{
    UNUSED(arg);

    if (uv_is_closing(handle))
        return;

    switch(handle->type)
    {
        case UV_TCP:
        case UV_ASYNC:
            uv_close(handle, NULL);
            break;
        default:
            uv_close(handle, (uv_close_cb) free);
    }
}

static void cb_timer_timeout(uv_timer_t *handle)
{
    nplex::client_t::impl_t *impl = (nplex::client_t::impl_t *) handle->data;
    impl->on_keepalive_timeout();
}

// ==========================================================
// client_t::impl_t methods
// ==========================================================

nplex::client_t::impl_t::impl_t(client_t &parent_, const params_t &params_) : 
    parent(parent_),
    m_state{client_t::state_e::CONNECTING},
    params(params_)
{
    if (params.servers.empty())
        throw nplex_exception("Invalid params: no servers");

    cache = std::make_shared<cache_t>();

    loop = std::make_unique<uv_loop_t>();

    if (uv_loop_init(loop.get()) != 0)
        throw nplex_exception("Error initializing event loop (uv_loop_init)");

    loop->data = this;

    async = std::make_unique<uv_async_t>();

    if (uv_async_init(loop.get(), async.get(), ::cb_process_async) != 0)
        throw nplex_exception("Error initializing event loop (uv_async_init)");

    timer = std::make_unique<uv_timer_t>();

    if (uv_timer_init(loop.get(), timer.get()) != 0)
        throw nplex_exception("Error initializing event loop (uv_timer_init)");

    for (auto &server : params.servers)
        connections.push_back(std::make_unique<connection_t>(server, loop.get(), params));
}

void nplex::client_t::impl_t::run() noexcept
{
    assert (m_state == client_t::state_e::CONNECTING);

    try
    {
        connect();
        uv_run(loop.get(), UV_RUN_DEFAULT);
    }
    catch (const std::exception &e) {
        error = e.what();
    }
    catch (...) {
        error = "Unknown error in the event loop";
    }

    uv_walk(loop.get(), ::cb_close_handle, NULL);
    while (uv_run(loop.get(), UV_RUN_NOWAIT));
    uv_loop_close(loop.get());

    parent.on_close();

    return;
}

void nplex::client_t::impl_t::report_server_activity()
{
    if (!m_con || m_state == client_t::state_e::CLOSED || m_state == client_t::state_e::DISCONNECTED) {
        close_timer();
        return;
    }

    auto handle = reinterpret_cast<uv_handle_t*>(timer.get());

    if (uv_is_active(handle) && !uv_is_closing(handle))
        uv_timer_again(timer.get());
}

void nplex::client_t::impl_t::connect()
{
    m_state = client_t::state_e::CONNECTING;
    m_con = nullptr;

    for (auto &con : connections)
    {
        assert(con->state == connection_t::state_e::CLOSED);
        con->connect();
    }
}

void nplex::client_t::impl_t::on_connection_established(connection_t *con)
{
    if (m_state != client_t::state_e::CONNECTING) {
        con->disconnect(0);
        return;
    }

    if (m_con != nullptr) {
        con->disconnect(0);
        return;
    }

    con->send(
        create_login_msg(
            ++correlation, 
            params.user, 
            params.password
        )
    );
}

void nplex::client_t::impl_t::on_connection_closed(connection_t *con)
{
    // case: trying to connect (connection failed)
    if (con != m_con)
    {
        for (auto &server : connections) {
            if (server->state != connection_t::state_e::CLOSED)
                return;  // there is hope
        }

        // all connections failed
        m_state = client_t::state_e::CLOSED;
        uv_stop(loop.get());
        return;
    }

    // case: connection lost
    close_timer();
    m_con = nullptr;
    m_state = client_t::state_e::DISCONNECTED;

    bool try_reconnection = parent.on_connection_lost(con->addr.str());

    if (try_reconnection) {
        connect();
        return;
    }

    m_state = client_t::state_e::CLOSED;
    uv_stop(loop.get());
}

void nplex::client_t::impl_t::on_keepalive_timeout()
{
    close_timer();

    if (m_con) {
        m_con->disconnect(UV_ETIMEDOUT);
        return;
    }
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

    if (m_state == client_t::state_e::CLOSED)
        return;

    if(!uv_loop_alive(loop.get())) {
        m_state = client_t::state_e::CLOSED;
        return;
    }

    uv_stop(loop.get());
}

void nplex::client_t::impl_t::process_ping_cmd(const nplex::ping_cmd_t &cmd)
{
    UNUSED(cmd);
    // TODO: implement
}

void nplex::client_t::impl_t::on_msg_delivered(connection_t *con, const msgs::Message *msg)
{
    UNUSED(msg);

    if (m_con != con)
        return;

    report_server_activity();
}

void nplex::client_t::impl_t::on_msg_received(connection_t *con, const msgs::Message *msg)
{
    if (!msg || !msg->content()) {
        con->disconnect(0);
        return;
    }

    if (msg->content_type() == msgs::MsgContent::LOGIN_RESPONSE) {
        process_login_resp(con, msg->content_as_LOGIN_RESPONSE());
        return;
    }

    if (con != m_con) {
        con->disconnect(0);
        return;
    }

    report_server_activity();

    switch (msg->content_type())
    {
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
            con->disconnect(0);
    }
}

void nplex::client_t::impl_t::process_login_resp(connection_t *con, const nplex::msgs::LoginResponse *resp)
{
    if (m_con) {
        con->disconnect(0);
        return;
    }

    assert(m_state == client_t::state_e::CONNECTING);

    if (resp->code() != msgs::LoginCode::AUTHORIZED) {
        con->disconnect(0);
        return;
    }

    m_con = con;
    m_state = client_t::state_e::SYNCHRONIZING;
    can_force = resp->can_force();
    // TODO: get permissions

    if (resp->keepalive()) {
        auto timeout = static_cast<uint64_t>(resp->keepalive() * static_cast<double>(params.timeout_factor));

        uv_timer_start(timer.get(), ::cb_timer_timeout, timeout, timeout);
    }

    for (auto &server : connections)
        if (server.get() != m_con)
            server->disconnect(0);

    auto cmd = parent.on_connect(m_con->addr.str(), resp->rev0(), resp->crev());

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

    m_con->send(
        create_load_msg(
            ++correlation, 
            mode, 
            cmd.second
        )
    );
}

void nplex::client_t::impl_t::process_load_resp(const nplex::msgs::LoadResponse *resp)
{
    if (m_state != client_t::state_e::SYNCHRONIZING) {
        // unexpected message
        m_con->disconnect(UV_EPROTO);
        return;
    }

    assert(resp->cid() == correlation);

    if (!resp->accepted())
        throw nplex_exception("Load rejected");

    if (resp->snapshot())
        cache->restore(resp->snapshot());

    parent.on_snapshot();

    if (cache->m_rev == resp->crev())
        m_state = client_t::state_e::SYNCHRONIZED;
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
    std::lock_guard<decltype(cache->m_mutex)> lock(cache->m_mutex);

    auto changes = cache->update(resp->update());

    for (auto it = transactions.begin(); it != transactions.end(); )
    {
        auto tx = *it;

        switch (tx->state())
        {
            case transaction_t::state_e::OPEN:
                tx->update(changes);
                ++it;
                break;

            case transaction_t::state_e::REJECTED:
            case transaction_t::state_e::COMMITTED:
            case transaction_t::state_e::DISCARDED:
            case transaction_t::state_e::ABORTED:
                it = transactions.erase(it);
                break;

            case transaction_t::state_e::SUBMITTING:
            case transaction_t::state_e::SUBMITTED:
                ++it;
                break;

            case transaction_t::state_e::ACCEPTED:
                // TODO: try to match it with the update
                ++it;
                break;
        }
    }

    auto meta = cache->m_metas.rbegin();
    assert(meta != cache->m_metas.rend());

    parent.on_update(meta->second, changes, nullptr);

    if (cache->m_rev == resp->crev())
        m_state = client_t::state_e::SYNCHRONIZED;

    assert(m_state == client_t::state_e::SYNCHRONIZING || cache->m_rev == resp->crev());
}

void nplex::client_t::impl_t::process_keepalive_push(const nplex::msgs::KeepAlivePush *resp)
{
    UNUSED(resp);
    // TODO: implement
}

void nplex::client_t::impl_t::process_ping_resp(const nplex::msgs::PingResponse *resp)
{
    UNUSED(resp);
}

void nplex::client_t::impl_t::close_timer()
{
    auto handle = reinterpret_cast<uv_handle_t*>(timer.get());

    if (uv_is_active(handle) && !uv_is_closing(handle))
        uv_close(handle, nullptr);
}
