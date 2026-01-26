#include <cassert>
#include <fmt/ranges.h>
#include <fmt/format.h>
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

#define LOG(severity, ...) \
    do { \
        if (static_cast<int>(listener.log_level()) <= static_cast<int>(severity)) \
            listener.log(parent, severity, fmt::format(__VA_ARGS__)); \
    } while(0)
#define LOG_DEBUG(...)  LOG(listener_t::log_level_e::DEBUG, __VA_ARGS__)
#define LOG_INFO(...)   LOG(listener_t::log_level_e::INFO , __VA_ARGS__)
#define LOG_WARN(...)   LOG(listener_t::log_level_e::WARN , __VA_ARGS__)
#define LOG_ERROR(...)  LOG(listener_t::log_level_e::ERROR, __VA_ARGS__)

// ==========================================================
// Internal (static) functions
// ==========================================================

static std::string error2str(int error)
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
struct fmt::formatter<nplex::acl_t> {
  constexpr auto parse (format_parse_context& ctx) { return ctx.begin(); }
  template <typename Context>
  constexpr auto format (nplex::acl_t const& obj, Context& ctx) const {
      return format_to(ctx.out(), "{}:{}", ::mode_to_string(obj.mode), obj.pattern);
  }
};

static void cb_process_async(uv_async_t *handle)
{
    auto *impl = (nplex::client_t::impl_t *) handle->loop->data;
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

static void cb_timer_keepalive_timeout(uv_timer_t *timer)
{
    auto *impl = (nplex::client_t::impl_t *) timer->loop->data;
    impl->on_keepalive_timeout();
}

static void cb_timer_connect_timeout(uv_timer_t *timer)
{
    auto *impl = (nplex::client_t::impl_t *) timer->loop->data;
    uv_timer_stop(timer);
    uv_close((uv_handle_t *) timer, (uv_close_cb) free);
    impl->connect();
}

// ==========================================================
// client_t::impl_t methods
// ==========================================================

nplex::client_t::impl_t::impl_t(const params_t &params_, listener_t &listener_, client_t &parent_) : 
    parent(parent_),
    m_state{state_e::DISCONNECTED},
    listener(listener_),
    params(params_)
{
    if (params.servers.empty())
        throw invalid_config("Invalid params: no servers");

    cache = std::make_shared<cache_t>();

    loop = std::make_unique<uv_loop_t>();

    if (uv_loop_init(loop.get()) != 0)
        throw nplex_exception("Error initializing the event loop (uv_loop_init)");

    loop->data = this;

    async = std::make_unique<uv_async_t>();

    if (uv_async_init(loop.get(), async.get(), ::cb_process_async) != 0)
        throw nplex_exception("Error initializing the event loop (uv_async_init)");

    for (const auto &server : params.servers)
        connections.push_back(connection_t::create(server, loop.get(), params));
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
        LOG_DEBUG("Event loop started");
        connect();
        uv_run(loop.get(), UV_RUN_DEFAULT);
    }
    catch (const std::exception &e) {
        LOG_ERROR("{}", e.what());
        error = e.what();
    }

    try
    {
        uv_walk(loop.get(), ::cb_close_handle, NULL);
        while (uv_run(loop.get(), UV_RUN_NOWAIT));
        uv_loop_close(loop.get());
        LOG_DEBUG("Event loop terminated");

        set_state(state_e::CLOSED);
        listener.on_closed(parent);
    }
    catch (const std::exception &e) {
        LOG_ERROR("{}", e.what());
        error = e.what();
    }
}

void nplex::client_t::impl_t::send(flatbuffers::DetachedBuffer &&buf)
{
    if (!m_con || m_state == state_e::CLOSED)
        return;

    auto type = flatbuffers::GetRoot<nplex::msgs::Message>(buf.data())->content_type();
    LOG_DEBUG("Sent {} to {}", msgs::EnumNameMsgContent(type), m_con->addr().str());

    m_con->send(std::move(buf));
}

void nplex::client_t::impl_t::abort(const std::string &msg)
{
    error = msg;
    set_state(state_e::CLOSED);
    LOG_ERROR("{}", msg);
    uv_stop(loop.get());
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

    auto handle = reinterpret_cast<uv_handle_t*>(timer_keepalive);

    if (handle && uv_is_active(handle) && !uv_is_closing(handle))
        uv_timer_again(timer_keepalive);
}

void nplex::client_t::impl_t::connect()
{
    if (m_state != state_e::DISCONNECTED)
        return;

    set_state(state_e::CONNECTING);
    m_con = nullptr;

    for (auto &con : connections)
        con->connect();
}

void nplex::client_t::impl_t::on_connection_established(connection_t *con)
{
    LOG_DEBUG("{} - connection established", con->addr().str());

    if (m_state != state_e::CONNECTING) {
        con->disconnect(ERR_CLOSED_BY_LOCAL);
        return;
    }

    if (m_con != nullptr) {
        con->disconnect(ERR_ALREADY_CONNECTED);
        return;
    }

    LOG_DEBUG("Sent {} to {}", msgs::EnumNameMsgContent(msgs::MsgContent::LOGIN_REQUEST), con->addr().str());

    // TODO: enable keepalive to avoid staled connections

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
    LOG_WARN("{} - {}", con->addr().str(), ::error2str(con->error()));

    // Case: unable to connect
    if (con != m_con)
    {
        for (auto &server : connections) {
            if (!server->is_closed())
                // Already connected or there is an ongoing connection attempt
                return;  
        }

        // All servers failed to connect

        abort("Unable to connect to the Nplex cluster");

        if (num_logins == 0)
            return;
    }

    std::string addr = (m_con ? m_con->addr().str() : "");

    if (timer_keepalive) {
        uv_close((uv_handle_t *) timer_keepalive, (uv_close_cb) free);
        timer_keepalive = nullptr;
    }

    m_con = nullptr;
    set_state(state_e::DISCONNECTED);

    auto wait_time = listener.on_connection_lost(parent, addr);

    if (wait_time < 0) {
        abort("Connection lost");
        return;
    }

    // schedule reconnection
    uv_timer_t *reconnect_timer = (uv_timer_t *) malloc(sizeof(uv_timer_t));
    uv_timer_init(loop.get(), reconnect_timer);
    reconnect_timer->data = this;
    uv_timer_start(reconnect_timer, ::cb_timer_connect_timeout, (uint64_t) wait_time, 0);
}

void nplex::client_t::impl_t::on_keepalive_timeout()
{
    if (timer_keepalive) {
        uv_close((uv_handle_t *) timer_keepalive, (uv_close_cb) free);
        timer_keepalive = nullptr;
    }

    if (m_con) {
        LOG_WARN("{} - connection lost", m_con->addr().str());
        m_con->disconnect(ERR_KEEPALIVE);
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

    if (m_state == state_e::CLOSED)
        return;

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
        con->disconnect(ERR_MSG_ERROR);
        return;
    }

    LOG_DEBUG("Received {} from {}", msgs::EnumNameMsgContent(msg->content_type()), con->addr().str());

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
        case msgs::MsgContent::LOAD_RESPONSE:
            process_load_resp(msg->content_as_LOAD_RESPONSE());
            break;

        case msgs::MsgContent::SUBMIT_RESPONSE:
            process_submit_resp(msg->content_as_SUBMIT_RESPONSE());
            break;

        case msgs::MsgContent::CHANGES_PUSH:
            process_changes_push(msg->content_as_CHANGES_PUSH());
            break;

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
        LOG_ERROR("Login failed on server {}", con->addr().str());
        con->disconnect(ERR_AUTH);
        return;
    }

    m_con = con;
    num_logins++;
    set_state(state_e::SYNCHRONIZING);
    can_force = resp->can_force();
    permissions.clear();

    if (resp->permissions())
    {
        for (flatbuffers::uoffset_t i = 0; i < resp->permissions()->size(); i++) {
            auto acl = resp->permissions()->Get(i);
            permissions.push_back({acl->mode(), acl->pattern()->str()});
        }
    }

    if (permissions.empty()) {
        con->disconnect(ERR_AUTH);
        return;
    }

    LOG_INFO("can-force = {}, permissions = [{}]", can_force, fmt::join(permissions, ", "));

    if (resp->keepalive())
    {
        auto timeout = static_cast<uint64_t>(resp->keepalive() * static_cast<double>(params.timeout_factor));

        timer_keepalive = (uv_timer_t *) malloc(sizeof(uv_timer_t));
        uv_timer_init(loop.get(), timer_keepalive);
    
        uv_timer_start(timer_keepalive, ::cb_timer_keepalive_timeout, timeout, timeout);

        LOG_INFO("keepalive = {}ms, connection-lost = {}ms", resp->keepalive(), timeout);
    }


    for (auto &server : connections)
        if (server.get() != m_con)
            server->disconnect(ERR_ALREADY_CONNECTED);

    auto cmd = listener.on_connected(parent, m_con->addr().str(), resp->rev0(), resp->crev());

    msgs::LoadMode mode = msgs::LoadMode::SNAPSHOT_AT_LAST_REV;

    switch (cmd.first)
    {
        case listener_t::load_mode_e::SNAPSHOT_AT_FIXED_REV:
            mode = msgs::LoadMode::SNAPSHOT_AT_FIXED_REV;
            break;

        case listener_t::load_mode_e::SNAPSHOT_AT_LAST_REV:
            mode = msgs::LoadMode::SNAPSHOT_AT_LAST_REV;
            break;

        case listener_t::load_mode_e::ONLY_UPDATES_FROM_REV:
            mode = msgs::LoadMode::ONLY_UPDATES_FROM_REV;
            break;

        default:
            break;
    }

    send(
        create_load_msg(
            ++correlation, 
            mode, 
            cmd.second
        )
    );
}

void nplex::client_t::impl_t::process_load_resp(const nplex::msgs::LoadResponse *resp)
{
    if (m_state != state_e::SYNCHRONIZING) {
        m_con->disconnect(ERR_MSG_UNEXPECTED);
        return;
    }

    assert(resp->cid() == correlation);

    if (!resp->accepted()) {
        m_con->disconnect(ERR_LOAD);
        return;
    }

    if (resp->snapshot())
        cache->load(resp->snapshot());

    listener.on_snapshot(parent);

    if (cache->m_rev == resp->crev())
        set_state(state_e::SYNCHRONIZED);
}

void nplex::client_t::impl_t::process_submit_resp(const nplex::msgs::SubmitResponse *resp)
{
    // save server.crev
    // retrieve request using cid
    // retrieve tx using cid

    if (resp->code() != msgs::SubmitCode::ACCEPTED)
    {
        // TODO: process error
        error = "Submit was rejected";
        //parent.on_reject(tx);
        return;
    }

    // update tx status
}

void nplex::client_t::impl_t::process_changes_push(const nplex::msgs::ChangesPush *resp)
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

    listener.on_update(parent, meta->second, changes);
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
