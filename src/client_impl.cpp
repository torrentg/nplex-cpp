#include <cassert>
#include <fmt/ranges.h>
#include <fmt/format.h>
#include "client_impl.hpp"

// ==========================================================
// Internal (static) functions
// ==========================================================

static const char * to_str(nplex::client_state_e state)
{
    switch (state)
    {
        case nplex::client_state_e::STARTUP:        return "STARTUP";
        case nplex::client_state_e::CONNECTED:      return "CONNECTED";
        case nplex::client_state_e::RECONNECTING:   return "RECONNECTING";
        case nplex::client_state_e::CLOSED:         return "CLOSED";
        default:                                    return "UNKNOWN";
    }
}

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
    constexpr auto parse (fmt::format_parse_context& ctx) { 
        return ctx.begin();
    }

    template <typename Context>
    auto format (nplex::acl_t const& obj, Context& ctx) const -> decltype(ctx.out()) {
        return fmt::format_to(ctx.out(), "{}:{}", ::mode_to_string(obj.mode), obj.pattern);
    }
};

static bool is_timer_active(uv_timer_t *timer)
{
    auto handle = reinterpret_cast<uv_handle_t*>(timer);
    return (handle && !uv_is_closing(handle) && uv_is_active(handle));
}

static void cb_close_handle(uv_handle_t *handle, void *arg)
{
    UNUSED(arg);

    if (uv_is_closing(handle))
        return;

    switch (handle->type)
    {
        case UV_TCP:
        case UV_ASYNC:
        case UV_TIMER:
        case UV_SIGNAL:
            uv_close(handle, nullptr);
            break;
        default:
            //fmt::print("Warning: unhandled {}\n", uv_handle_type_name(handle->type));
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
    impl->log_debug("Reconnect timer expired");
    uv_timer_stop(timer);
    impl->try_to_connect();
}

static void cb_signal_sigint(uv_signal_t *handle, int signum)
{
    auto impl = static_cast<nplex::client_t::impl_t *>(handle->loop->data);
    impl->abort(fmt::format("Signal {} received, stopping event loop", signum));
    uv_signal_stop(handle);
}

// ==========================================================
// client_t::impl_t methods
// ==========================================================

nplex::client_t::impl_t::impl_t(const params_t &params, rev_t rev0, listener_t &listener, client_t &parent) : 
    m_parent(parent),
    m_listener(listener),
    m_params(params),
    m_rev0(rev0),
    m_state{client_state_e::STARTUP}
{
    int rc = 0;

    if (m_params.servers.empty())
        throw invalid_config("no servers");

    store = std::make_shared<store_t>();

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
    m_async = std::make_unique<uv_async_t>();
    if ((rc = uv_async_init(m_loop.get(), m_async.get(), ::cb_process_async)) != 0)
        throw nplex_exception(uv_strerror(rc));
    m_async->data = this;

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
    assert(is_closed());
}

void nplex::client_t::impl_t::run() noexcept
{
    assert(m_state == client_state_e::STARTUP);
    m_loop_thread_id = std::this_thread::get_id();
    m_error = nullptr;
    m_con = nullptr;

    try
    {
        log_debug("Event loop started");
        try_to_connect();
        uv_run(m_loop.get(), UV_RUN_DEFAULT);
    }
    catch (const std::exception &e) {
        log_error("{}", e.what());
        m_error = std::current_exception();
    }

    try
    {
        // TODO: call connection.disconnect()

        uv_walk(m_loop.get(), ::cb_close_handle, nullptr);
        while (uv_run(m_loop.get(), UV_RUN_NOWAIT));
        uv_loop_close(m_loop.get());

        set_state(client_state_e::CLOSED);
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
        return (s != client_state_e::STARTUP);
    });

    if (m_error)
        std::rethrow_exception(m_error);
}

void nplex::client_t::impl_t::send(flatbuffers::DetachedBuffer &&buf)
{
    assert(m_loop_thread_id == std::this_thread::get_id());

    if (!m_con || is_closed())
        return;

    auto type = flatbuffers::GetRoot<nplex::msgs::Message>(buf.data())->content_type();
    log_debug("Sent {} to {}", msgs::EnumNameMsgContent(type), m_con->addr().str());

    m_con->send(std::move(buf));
}

void nplex::client_t::impl_t::abort(const std::string &msg)
{
    assert(m_loop_thread_id == std::this_thread::get_id());

    if (is_closed())
        return;

    m_error = std::make_exception_ptr(nplex_exception(msg));
    m_con = nullptr;
    m_data_cid = 0;

    for (auto &con : m_connections)
        con->disconnect(ERR_CLOSED_BY_LOCAL);

    if (is_timer_active(m_timer_con_lost.get())) {
        log_debug("Connection-lost timer stopped");
        uv_timer_stop(m_timer_con_lost.get());
    }

    if (is_timer_active(m_timer_reconnect.get())) {
        log_debug("Reconnect timer stopped");
        uv_timer_stop(m_timer_reconnect.get());
    }

    set_state(client_state_e::CLOSED);
    log_error("{}", msg);

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_commands.clear();
    }

    uv_stop(m_loop.get());
}

void nplex::client_t::impl_t::set_state(client_state_e state)
{
    assert(m_loop_thread_id == std::this_thread::get_id());

    if (m_state == state)
        return;

    log_debug("State changed from {} to {}", to_str(m_state.load()), to_str(state));

    m_state = state;
    m_cv.notify_all();
}

void nplex::client_t::impl_t::report_server_activity()
{
    assert(m_loop_thread_id == std::this_thread::get_id());

    if (!m_con || is_closed())
        return;

    if (is_timer_active(m_timer_con_lost.get())) {
        log_debug("Resetting connection-lost timer");
        uv_timer_again(m_timer_con_lost.get());
    }
}

void nplex::client_t::impl_t::try_to_connect()
{
    assert(m_loop_thread_id == std::this_thread::get_id());

    if (is_closed())
        return;

    log_debug("Trying to connect to servers...");

    assert(m_con == nullptr);
    m_con = nullptr;

    if (is_timer_active(m_timer_reconnect.get())) {
        log_debug("Stopping reconnect timer");
        uv_timer_stop(m_timer_reconnect.get());
    }

    for (auto &con : m_connections) {
        log_debug("Connecting to {}", con->addr().str());
        assert(con->is_closed());
        con->connect();
    }
}

void nplex::client_t::impl_t::on_connection_established(connection_t *con)
{
    assert(m_loop_thread_id == std::this_thread::get_id());

    log_debug("{} - connection established", con->addr().str());

    if (m_con != nullptr) {
        con->disconnect(ERR_ALREADY_CONNECTED);
        return;
    }

    log_debug("Sent {} to {}", msgs::EnumNameMsgContent(msgs::MsgContent::LOGIN_REQUEST), con->addr().str());

    con->send(
        create_login_msg(
            ++m_correlation, 
            m_params.user, 
            m_params.password
        )
    );
}

static bool all_connections_closed(const std::vector<nplex::connection_ptr> &connections)
{
    for (const auto &con : connections) {
        if (!con->is_closed())
            return false;
    }

    return true;
}

void nplex::client_t::impl_t::schedule_reconnect(std::uint32_t millis)
{
    assert(m_loop_thread_id == std::this_thread::get_id());

    if (is_closed())
        return;

    if (is_timer_active(m_timer_reconnect.get())) {
        log_debug("Reconnect timer stopped");
        uv_timer_stop(m_timer_reconnect.get());
    }

    uv_timer_start(m_timer_reconnect.get(), ::cb_timer_reconnect, millis, 0);
    log_debug("Starting reconnect timer with {} ms delay", millis);
}

void nplex::client_t::impl_t::on_connection_closed(connection_t *con)
{
    assert(m_loop_thread_id == std::this_thread::get_id());

    log_warn("{} - {}", con->addr().str(), ::error2str(con->error()));

    if (is_closed())
        return;

    // Case: connection to cluster failed
    if (!m_con && all_connections_closed(m_connections))
    {
        switch (m_state)
        {
            case client_state_e::STARTUP:
                abort("Unable to connect to any server");
                return;

            case client_state_e::RECONNECTING:
            {
                auto wait_time = m_listener.on_connection_failed(m_parent);

                // case: do not reconnect
                if (wait_time < 0) {
                    abort("Client closed by listener");
                    return;
                }

                // case: re-schedule reconnection
                schedule_reconnect(static_cast<std::uint32_t>(wait_time));
                return;
            }
            default:
                assert(false);
        }
    }

    // Case: connection to server established previously (m_con != nullptr)
    // Case: no connection to the server (m_con == nullptr), but there are other attempts in progress
    if (con != m_con)
        return;

    // Case: current server connection failed (con == m_con)
    assert(is_connected());
    m_con = nullptr;
    m_data_cid = 0;

    if (is_timer_active(m_timer_con_lost.get())) {
        log_debug("Connection-lost timer stopped");
        uv_timer_stop(m_timer_con_lost.get());
    }

    bool retry = m_listener.on_connection_lost(m_parent, con->addr().str());

    if (retry) {
        set_state(client_state_e::RECONNECTING);
        try_to_connect();
    }
    else {
        abort("Client closed by listener");
    }
}

void nplex::client_t::impl_t::on_connection_lost()
{
    assert(m_loop_thread_id == std::this_thread::get_id());

    if (m_con) {
        log_debug("Connection-lost timer expired");
        m_con->disconnect(ERR_CON_LOST);
    }

    if (is_timer_active(m_timer_con_lost.get())) {
        log_debug("Connection-lost timer stopped");
        uv_timer_stop(m_timer_con_lost.get());
    }
}

void nplex::client_t::impl_t::push_command(const command_t &cmd)
{
    if (is_closed())
        return;

    std::lock_guard<std::mutex> lock(m_mutex);
    m_commands.push(cmd);
    uv_async_send(m_async.get());
}

void nplex::client_t::impl_t::process_commands()
{
    assert(m_loop_thread_id == std::this_thread::get_id());

    if (is_closed()) {
        m_commands.clear();
        return;
    }

    while (true)
    {
        command_t cmd;

        {
            std::lock_guard<std::mutex> lock(m_mutex);

            if (m_commands.empty())
                break;

            cmd = m_commands.pop();
        }

        std::visit([this](auto&& arg)
        {
            using T = std::decay_t<decltype(arg)>;

            if constexpr (std::is_same_v<T, nplex::submit_cmd_t>)
                process_submit_cmd(arg);
            else if constexpr (std::is_same_v<T, nplex::close_cmd_t>)
                process_close_cmd(arg);
            else if constexpr (std::is_same_v<T, nplex::ping_cmd_t>)
                process_ping_cmd(arg);
            else
                static_assert(false, "non-exhaustive visitor!");
        }, cmd);
    }
}

void nplex::client_t::impl_t::process_submit_cmd(const nplex::submit_cmd_t &cmd)
{
    assert(m_loop_thread_id == std::this_thread::get_id());

    UNUSED(cmd);
    // TODO: implement
}

void nplex::client_t::impl_t::process_close_cmd([[maybe_unused]] const nplex::close_cmd_t &cmd)
{
    assert(m_loop_thread_id == std::this_thread::get_id());
    abort("nplex closed by user");
}

void nplex::client_t::impl_t::process_ping_cmd(const nplex::ping_cmd_t &cmd)
{
    assert(m_loop_thread_id == std::this_thread::get_id());

    UNUSED(cmd);
    // TODO: implement
}

void nplex::client_t::impl_t::on_msg_delivered(connection_t *con, [[maybe_unused]] const msgs::Message *msg)
{
    assert(m_loop_thread_id == std::this_thread::get_id());

    if (m_con != con)
        return;

    report_server_activity();
}

void nplex::client_t::impl_t::on_msg_received(connection_t *con, const msgs::Message *msg)
{
    assert(m_loop_thread_id == std::this_thread::get_id());

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
    assert(m_loop_thread_id == std::this_thread::get_id());

    if (m_con) {
        con->disconnect(ERR_ALREADY_CONNECTED);
        return;
    }

    assert(m_state == client_state_e::STARTUP || m_state == client_state_e::RECONNECTING);

    if (resp->code() != msgs::LoginCode::AUTHORIZED) {
        log_error("Login failed on server {} ({})", con->addr().str(), msgs::EnumNameLoginCode(resp->code()));
        con->disconnect(ERR_AUTH);
        return;
    }

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

    if (resp->keepalive())
    {
        auto timeout = static_cast<uint64_t>(resp->keepalive() * static_cast<double>(m_params.timeout_factor));
        uv_timer_start(m_timer_con_lost.get(), ::cb_timer_connection_lost, timeout, timeout);
        log_debug("Started connection-lost timer with {} ms timeout", timeout);
    }

    m_con = con;

    for (auto &server : m_connections)
        if (server.get() != m_con)
            server->disconnect(ERR_ALREADY_CONNECTED);

    m_listener.on_connection_success(m_parent, m_con->addr().str());

    log_debug("Login successful on server {}, available data = [{}-{}]", m_con->addr().str(), resp->rev0(), resp->crev());

    // case: reconnecting
    if (m_state == client_state_e::RECONNECTING)
    {
        set_state(client_state_e::CONNECTED);
        m_data_cid = m_correlation++;

        send(
            create_updates_msg(
                m_data_cid,
                store->m_rev
            )
        );

        return;
    }

    assert(m_state == client_state_e::STARTUP);

    // case: no snapshot required
    if (m_rev0 == 0 )
    {
        store->m_rev = resp->crev();
        set_state(client_state_e::CONNECTED);
        m_data_cid = m_correlation++;

        send(
            create_updates_msg(
                m_data_cid, 
                0
            )
        );

        return;
    }

    // case: snapshot required
    rev_t srev = (m_rev0 <= resp->rev0() ? resp->rev0() : m_rev0 <= resp->crev() ? m_rev0 : 0);

    m_data_cid = m_correlation++;

    send(
        create_snapshot_msg(
            m_data_cid, 
            srev
        )
    );
}

void nplex::client_t::impl_t::process_snapshot_resp(const nplex::msgs::SnapshotResponse *resp)
{
    assert(m_loop_thread_id == std::this_thread::get_id());

    if (m_state != client_state_e::STARTUP || resp->cid() != m_data_cid) {
        m_con->disconnect(ERR_MSG_UNEXPECTED);
        return;
    }

    if (!resp->accepted()) {
        m_con->disconnect(ERR_LOAD);
        return;
    }

    if (resp->snapshot())
        store->load(resp->snapshot());

    set_state(client_state_e::CONNECTED);

    m_listener.on_snapshot(m_parent);

    m_data_cid = m_correlation++;

    send(
        create_updates_msg(
            m_data_cid, 
            store->m_rev
        )
    );
}

void nplex::client_t::impl_t::process_updates_resp(const nplex::msgs::UpdatesResponse *resp)
{
    assert(m_loop_thread_id == std::this_thread::get_id());

    if (!is_connected() || resp->cid() != m_data_cid) {
        m_con->disconnect(ERR_MSG_UNEXPECTED);
        return;
    }

    if (!resp->accepted()) {
        m_con->disconnect(ERR_LOAD);
        return;
    }
}

void nplex::client_t::impl_t::process_submit_resp(const nplex::msgs::SubmitResponse *resp)
{
    assert(m_loop_thread_id == std::this_thread::get_id());

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
    assert(m_loop_thread_id == std::this_thread::get_id());

    if (!is_connected() || resp->cid() != m_data_cid) {
        m_con->disconnect(ERR_MSG_UNEXPECTED);
        return;
    }

    auto updates = resp->updates();

    if (updates)
    {
        for (auto upd : *updates)
            process_update(upd);
    }
}

void nplex::client_t::impl_t::process_update(const nplex::msgs::Update *upd)
{
    assert(m_loop_thread_id == std::this_thread::get_id());

    std::lock_guard<decltype(store->m_mutex)> lock(store->m_mutex);

    auto changes = store->update(upd);

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

    auto meta = store->m_metas.rbegin();
    assert(meta != store->m_metas.rend());

    m_listener.on_update(m_parent, meta->second, changes);
}

void nplex::client_t::impl_t::process_keepalive_push(const nplex::msgs::KeepAlivePush *resp)
{
    assert(m_loop_thread_id == std::this_thread::get_id());

    UNUSED(resp);
    // TODO: implement
}

void nplex::client_t::impl_t::process_ping_resp(const nplex::msgs::PingResponse *resp)
{
    assert(m_loop_thread_id == std::this_thread::get_id());

    UNUSED(resp);
    // TODO: implement
}
