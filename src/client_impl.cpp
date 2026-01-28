#include <cassert>
#include <fmt/ranges.h>
#include <fmt/format.h>
#include "client_impl.hpp"

// ==========================================================
// Internal (static) functions
// ==========================================================

static std::string error2str(int error)
{
    if (error < 0)
        return uv_strerror(error);

    switch (error)
    {
        case ERR_CLOSED_BY_LOCAL:   return "closed by local";
        case ERR_CLOSED_BY_PEER:    return "closed by peer";
        case ERR_MSG_ERROR:         return "invalid message";
        case ERR_MSG_UNEXPECTED:    return "unexpected message";
        case ERR_MSG_SIZE:          return "message too large";
        case ERR_ALREADY_CONNECTED: return "already connected";
        case ERR_CON_LOST:          return "connection lost";
        case ERR_AUTH:              return "unauthorized";
        case ERR_LOAD:              return "snapshot request rejected";
        case ERR_SIGNAL:            return "signal received";
        default:                    return fmt::format("unknown error -{}-", error);
    }
}

static std::string mode_to_string(std::uint8_t mode)
{
    return std::string{
        ((mode & NPLEX_CREATE) ? 'c' : '-'),
        ((mode & NPLEX_READ)   ? 'r' : '-'),
        ((mode & NPLEX_UPDATE) ? 'u' : '-'),
        ((mode & NPLEX_DELETE) ? 'd' : '-')
    };
}

template <>
struct fmt::formatter<nplex::acl_t>
{
    constexpr auto parse (format_parse_context& ctx) { 
        return ctx.begin();
    }

    template <typename Context>
    constexpr auto format (nplex::acl_t const& obj, Context& ctx) const {
        return format_to(ctx.out(), "{}:{}", ::mode_to_string(obj.mode), obj.pattern);
    }
};

static void cb_close_handle(uv_handle_t *handle, void *arg)
{
    UNUSED(arg);

    if (uv_is_closing(handle))
        return;

    switch(handle->type)
    {
        case UV_TCP:
        case UV_ASYNC:
        case UV_TIMER:
        case UV_SIGNAL:
            uv_close(handle, nullptr);
            break;
        default:
            fmt::print("Warning: unhandled uv handle type {}\n", uv_handle_type_name(handle->type));
            uv_close(handle, nullptr);
    }
}

static void cb_process_async(uv_async_t *handle)
{
    auto impl = static_cast<nplex::client_t::impl_t *>(handle->loop->data);
    impl->process_commands();
}

static void cb_timer_connection_lost(uv_timer_t *timer)
{
    auto impl = static_cast<nplex::client_t::impl_t *>(timer->loop->data);
    impl->on_connection_lost();
}

static void cb_timer_reconnect(uv_timer_t *timer)
{
    auto impl = static_cast<nplex::client_t::impl_t *>(timer->loop->data);
    uv_timer_stop(timer);
    impl->connect();
}

static void cb_signal_sigint(uv_signal_t *handle, int signum)
{
    auto impl = static_cast<nplex::client_t::impl_t *>(handle->loop->data);
    uv_signal_stop(handle);
    impl->abort(fmt::format("Signal {} received, stopping event loop", signum));
}

// ==========================================================
// client_t::impl_t methods
// ==========================================================

nplex::client_t::impl_t::impl_t(const params_t &params, listener_t &listener, client_t &parent) : 
    m_parent(parent),
    m_listener(listener),
    m_params(params),
    m_state{state_e::DISCONNECTED}
{
    int rc = 0;

    if (m_params.servers.empty())
        throw invalid_config("no servers");

    cache = std::make_shared<cache_t>();

    // initialize the event loop
    m_loop = std::make_unique<uv_loop_t>();
    if ((rc = uv_loop_init(m_loop.get())) != 0)
        throw nplex_exception(uv_strerror(rc));
    m_loop->data = this;

    // initialize the connection-lost timer
    m_timer_con_lost = std::make_unique<uv_timer_t>();
    if ((rc = uv_timer_init(m_loop.get(), m_timer_con_lost.get())) != 0)
        throw nplex_exception(uv_strerror(rc));
    m_timer_con_lost->data = this;

    // initialize the reconnect timer
    m_timer_reconnect = std::make_unique<uv_timer_t>();
    if ((rc = uv_timer_init(m_loop.get(), m_timer_reconnect.get())) != 0)
        throw nplex_exception(uv_strerror(rc));
    m_timer_reconnect->data = this;

    // install the async handler
    async = std::make_unique<uv_async_t>();
    if ((rc = uv_async_init(m_loop.get(), async.get(), ::cb_process_async)) != 0)
        throw nplex_exception(uv_strerror(rc));
    async->data = this;

    // install the SIGINT (Ctrl-C) handler
    m_signal_sigint = std::make_unique<uv_signal_t>();
    if ((rc = uv_signal_init(m_loop.get(), m_signal_sigint.get())) != 0)
        throw nplex_exception(uv_strerror(rc));
    m_signal_sigint->data = this;
    if ((rc = uv_signal_start(m_signal_sigint.get(), ::cb_signal_sigint, SIGINT)) != 0)
        throw nplex_exception(uv_strerror(rc));

    // create connections
    for (const auto &server : m_params.servers)
        m_connections.push_back(connection_t::create(addr_t{server}, m_loop.get(), m_params));
}

nplex::client_t::impl_t::~impl_t()
{
    assert (m_state == state_e::CLOSED);
}

void nplex::client_t::impl_t::run() noexcept
{
    assert (m_state == state_e::DISCONNECTED);

    try
    {
        log_debug("Event loop started");
        connect();
        uv_run(m_loop.get(), UV_RUN_DEFAULT);
    }
    catch (const std::exception &e) {
        log_error("{}", e.what());
        m_error = std::current_exception();
    }

    try
    {
        uv_walk(m_loop.get(), ::cb_close_handle, nullptr);
        while (uv_run(m_loop.get(), UV_RUN_NOWAIT));
        uv_loop_close(m_loop.get());

        set_state(state_e::CLOSED);
        log_debug("Event loop terminated");
        m_listener.on_closed(m_parent);
    }
    catch (const std::exception &e) {
        log_error("{}", e.what());
        m_error = std::current_exception();
    }
}

void nplex::client_t::impl_t::wait_for_startup()
{
    std::unique_lock<std::mutex> lock(m_mutex);
    m_cv.wait(lock, [this]{
        auto s = m_state.load();
        return (s != client_t::state_e::DISCONNECTED && s != client_t::state_e::CONNECTING);
    });

    if (m_state == state_e::CLOSED && m_error)
        std::rethrow_exception(m_error);
}

void nplex::client_t::impl_t::send(flatbuffers::DetachedBuffer &&buf)
{
    if (!m_con || m_state == state_e::CLOSED)
        return;

    auto type = flatbuffers::GetRoot<nplex::msgs::Message>(buf.data())->content_type();
    log_debug("Sent {} to {}", msgs::EnumNameMsgContent(type), m_con->addr().str());

    m_con->send(std::move(buf));
}

void nplex::client_t::impl_t::abort(const std::string &msg)
{
    m_error = std::make_exception_ptr(nplex_exception(msg));
    m_con = nullptr;
    for (auto &con : m_connections)
        con->disconnect(ERR_SIGNAL);
    set_state(state_e::CLOSED);
    log_error("{}", msg);
    uv_stop(m_loop.get());
}

void nplex::client_t::impl_t::set_state(state_e state)
{
    m_state = state;
    m_cv.notify_all();
}

void nplex::client_t::impl_t::report_server_activity()
{
    if (!m_con || m_state == state_e::CLOSED || m_state == state_e::DISCONNECTED)
        return;

    auto handle = reinterpret_cast<uv_handle_t*>(m_timer_con_lost.get());
    if (handle && !uv_is_closing(handle) && uv_is_active(handle)) {
        uv_timer_again(m_timer_con_lost.get());
        log_debug("Resetting connection-lost timer");
    }
}

void nplex::client_t::impl_t::connect()
{
    if (m_state != state_e::DISCONNECTED)
        return;

    set_state(state_e::CONNECTING);
    m_con = nullptr;

    for (auto &con : m_connections)
        con->connect();
}

void nplex::client_t::impl_t::on_connection_established(connection_t *con)
{
    log_debug("{} - connection established", con->addr().str());

    if (m_state != state_e::CONNECTING) {
        con->disconnect(ERR_CLOSED_BY_LOCAL);
        return;
    }

    if (m_con != nullptr) {
        con->disconnect(ERR_ALREADY_CONNECTED);
        return;
    }

    log_debug("Sent {} to {}", msgs::EnumNameMsgContent(msgs::MsgContent::LOGIN_REQUEST), con->addr().str());

    // TODO: enable keepalive to avoid staled connections

    con->send(
        create_login_msg(
            ++m_correlation, 
            m_params.user, 
            m_params.password
        )
    );
}

void nplex::client_t::impl_t::on_connection_closed(connection_t *con)
{
    log_warn("{} - {}", con->addr().str(), ::error2str(con->error()));

    // Case: unable to connect
    if (con != m_con)
    {
        for (auto &server : m_connections) {
            if (!server->is_closed())
                // Already connected or there is an ongoing connection attempt
                return;
        }

        // All servers failed to connect
        abort("Unable to connect to the Nplex cluster");

        if (m_num_logins == 0)
            return;
    }

    std::string addr = (m_con ? m_con->addr().str() : "");
    uv_handle_t *handle = nullptr;

    handle = reinterpret_cast<uv_handle_t *>(m_timer_con_lost.get());
    if (handle && !uv_is_closing(handle) && uv_is_active(handle))
        uv_timer_stop(m_timer_con_lost.get());

    m_con = nullptr;
    set_state(state_e::DISCONNECTED);

    auto wait_time = m_listener.on_connection_lost(m_parent, addr);

    if (wait_time < 0) {
        abort("Unable to connect");
        return;
    }

    // schedule reconnection
    handle = reinterpret_cast<uv_handle_t *>(m_timer_reconnect.get());
    if (handle && !uv_is_closing(handle))
        uv_timer_start(m_timer_reconnect.get(), ::cb_timer_reconnect, (uint64_t) wait_time, 0);
}

void nplex::client_t::impl_t::on_connection_lost()
{
    uv_handle_t *handle = reinterpret_cast<uv_handle_t *>(m_timer_con_lost.get());
    if (handle && !uv_is_closing(handle) && uv_is_active(handle))
        uv_timer_stop(m_timer_con_lost.get());

    if (m_con)
        m_con->disconnect(ERR_CON_LOST);
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

    if (m_state == state_e::CLOSED)
        return;

    uv_stop(m_loop.get());
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
        con->disconnect(ERR_MSG_ERROR);
        return;
    }

    log_debug("Received {} from {}", msgs::EnumNameMsgContent(msg->content_type()), con->addr().str());

    if (msg->content_type() == msgs::MsgContent::LOGIN_RESPONSE) {
        process_login_resp(con, msg->content_as_LOGIN_RESPONSE());
        return;
    }

    if (con != m_con) {
        con->disconnect(ERR_ALREADY_CONNECTED);
        return;
    }

    report_server_activity();

    switch (msg->content_type())
    {
        case msgs::MsgContent::SUBMIT_RESPONSE:
            process_submit_resp(msg->content_as_SUBMIT_RESPONSE());
            break;

        case msgs::MsgContent::SNAPSHOT_RESPONSE:
            process_snapshot_resp(msg->content_as_SNAPSHOT_RESPONSE());
            break;

        case msgs::MsgContent::UPDATES_RESPONSE:
            process_updates_resp(msg->content_as_UPDATES_RESPONSE());
            break;

        [[likely]]
        case msgs::MsgContent::UPDATES_PUSH:
            process_updates_push(msg->content_as_UPDATES_PUSH());
            break;

        [[likely]]
        case msgs::MsgContent::KEEPALIVE_PUSH:
            process_keepalive_push(msg->content_as_KEEPALIVE_PUSH());
            break;

        case msgs::MsgContent::PING_RESPONSE:
            process_ping_resp(msg->content_as_PING_RESPONSE());
            break;

        default:
            con->disconnect(ERR_MSG_ERROR);
    }
}

void nplex::client_t::impl_t::process_login_resp(connection_t *con, const nplex::msgs::LoginResponse *resp)
{
    if (m_con) {
        con->disconnect(ERR_ALREADY_CONNECTED);
        return;
    }

    assert(m_state == state_e::CONNECTING);

    if (resp->code() != msgs::LoginCode::AUTHORIZED) {
        log_error("Login failed on server {}", con->addr().str());
        con->disconnect(ERR_AUTH);
        return;
    }

    m_con = con;
    m_num_logins++;
    set_state(state_e::SYNCHRONIZING);
    m_can_force = resp->can_force();
    m_permissions.clear();

    if (resp->permissions())
    {
        for (flatbuffers::uoffset_t i = 0; i < resp->permissions()->size(); i++) {
            auto acl = resp->permissions()->Get(i);
            m_permissions.push_back({acl->mode(), acl->pattern()->str()});
        }
    }

    if (m_permissions.empty()) {
        con->disconnect(ERR_AUTH);
        return;
    }

    log_info("can-force = {}, m_permissions = [{}]", m_can_force, fmt::join(m_permissions, ", "));

    if (resp->keepalive()) {
        auto timeout = static_cast<uint64_t>(resp->keepalive() * static_cast<double>(m_params.timeout_factor));
        uv_timer_start(m_timer_con_lost.get(), ::cb_timer_connection_lost, timeout, timeout);
        log_debug("keepalive = {}ms, connection-lost = {}ms", resp->keepalive(), timeout);
    }

    for (auto &server : m_connections)
        if (server.get() != m_con)
            server->disconnect(ERR_ALREADY_CONNECTED);

    auto srev = m_listener.on_connected(m_parent, m_con->addr().str(), resp->rev0(), resp->crev());

    send(
        create_snapshot_msg(
            ++m_correlation, 
            srev
        )
    );
}

void nplex::client_t::impl_t::process_snapshot_resp(const nplex::msgs::SnapshotResponse *resp)
{
    if (m_state != state_e::SYNCHRONIZING) {
        m_con->disconnect(ERR_MSG_UNEXPECTED);
        return;
    }

    assert(resp->cid() == m_correlation);

    if (!resp->accepted()) {
        m_con->disconnect(ERR_LOAD);
        return;
    }

    if (resp->snapshot())
        cache->load(resp->snapshot());

    m_listener.on_snapshot(m_parent);

    send(
        create_updates_msg(
            ++m_correlation, 
            cache->m_rev
        )
    );
}

void nplex::client_t::impl_t::process_updates_resp(const nplex::msgs::UpdatesResponse *resp)
{
    if (m_state != state_e::SYNCHRONIZING) {
        m_con->disconnect(ERR_MSG_UNEXPECTED);
        return;
    }

    assert(resp->cid() == m_correlation);

    if (!resp->accepted()) {
        m_con->disconnect(ERR_LOAD);
        return;
    }
}

void nplex::client_t::impl_t::process_submit_resp(const nplex::msgs::SubmitResponse *resp)
{
    // save server.crev
    // retrieve request using cid
    // retrieve tx using cid

    if (resp->code() != msgs::SubmitCode::ACCEPTED)
    {
        // TODO: process error
        //error = "Submit was rejected";
        //parent.on_reject(tx);
        return;
    }

    // update tx status
}

void nplex::client_t::impl_t::process_updates_push(const nplex::msgs::UpdatesPush *resp)
{
    auto updates = resp->updates();

    if (updates)
    {
        for (auto upd : *updates)
            process_update(upd);
    }

    if (cache->m_rev == resp->crev())
        set_state(state_e::SYNCHRONIZED);

    assert(m_state == state_e::SYNCHRONIZING || cache->m_rev == resp->crev());
}

void nplex::client_t::impl_t::process_update(const nplex::msgs::Update *upd)
{
    std::lock_guard<decltype(cache->m_mutex)> lock(cache->m_mutex);

    auto changes = cache->update(upd);

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

    m_listener.on_update(m_parent, meta->second, changes);
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
