#include <cassert>
#include <limits>
#include <fmt/ranges.h>
#include <fmt/format.h>
#include "utils.hpp"
#include "client_impl.hpp"

// ==========================================================
// Internal (static) functions
// ==========================================================

static const char * to_str(nplex::client_impl::state_e state)
{
    using namespace nplex;

    switch (state)
    {
        case client_impl::state_e::OFFLINE:             return "OFFLINE";
        case client_impl::state_e::CONNECTING:          return "CONNECTING";
        case client_impl::state_e::AUTHENTICATED:       return "AUTHENTICATED";
        case client_impl::state_e::LOADING_SNAPSHOT:    return "LOADING_SNAPSHOT";
        case client_impl::state_e::INITIALIZED:         return "INITIALIZED";
        case client_impl::state_e::SYNCING:             return "SYNCING";
        case client_impl::state_e::SYNCED:              return "SYNCED";
        case client_impl::state_e::CLOSED:              return "CLOSED";
        default:                                        return "UNKNOWN";
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

static nplex::request_ptr cmd_to_req(nplex::command_ptr &cmd)
{
    auto obj = dynamic_cast<nplex::request_t*>(cmd.get());
    assert(obj);
    nplex::request_ptr req{};
    req.reset(obj);
    cmd.release();
    return req;
}

template <>
struct fmt::formatter<nplex::acl_t>
{
    static constexpr auto parse(fmt::format_parse_context &ctx) { 
        return ctx.begin();
    }

    template <typename Context>
    auto format (nplex::acl_t const &obj, Context &ctx) const -> decltype(ctx.out()) {
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

static void cb_process_async_commands(uv_async_t *handle)
{
    auto impl = static_cast<nplex::client_impl *>(handle->loop->data);
    impl->process_commands();
}

static void cb_timer_connection_lost(uv_timer_t *timer)
{
    auto impl = static_cast<nplex::client_impl *>(timer->loop->data);
    impl->on_connection_lost();
}

static void cb_timer_reconnect(uv_timer_t *timer)
{
    auto impl = static_cast<nplex::client_impl *>(timer->loop->data);
    uv_timer_stop(timer);
    impl->try_to_connect();
}

static void cb_signal_sigint(uv_signal_t *handle, int signum)
{
    auto impl = static_cast<nplex::client_impl *>(handle->loop->data);
    impl->abort(fmt::format("Signal {} received, stopping event loop", signum));
    uv_signal_stop(handle);
}

// ==========================================================
// client_impl methods
// ==========================================================

nplex::client_impl::client_impl(const client_params_t &params) :
    m_params(params),
    m_rev0(std::numeric_limits<rev_t>::max()),
    m_state{state_e::OFFLINE}
{
    int rc = 0;

    if (m_params.servers.empty())
        throw nplex_exception("no servers");

    m_store = std::make_shared<store_t>();
    m_manager = std::make_shared<manager>();

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
    m_async_command = std::make_unique<uv_async_t>();
    if ((rc = uv_async_init(m_loop.get(), m_async_command.get(), ::cb_process_async_commands)) != 0)
        throw nplex_exception(uv_strerror(rc));
    m_async_command->data = this;

    // install the SIGINT (Ctrl-C) handler
    m_signal_sigint = std::make_unique<uv_signal_t>();
    if ((rc = uv_signal_init(m_loop.get(), m_signal_sigint.get())) != 0)
        throw nplex_exception(uv_strerror(rc));
    m_signal_sigint->data = this;
    if ((rc = uv_signal_start(m_signal_sigint.get(), ::cb_signal_sigint, SIGINT)) != 0)
        throw nplex_exception(uv_strerror(rc));

    // create connections
    for (const auto &server : m_params.servers)
        m_connections.push_back(connection::create(addr_t{server}, m_loop.get(), m_params.connection));
}

nplex::client_impl::~client_impl()
{
    assert(m_loop_thread_id == std::thread::id{});
    assert(is_closed());
}

nplex::client & nplex::client_impl::set_logger(const std::shared_ptr<logger> &log)
{
    if (is_running())
        throw nplex_exception("cannot set logger while running");

    m_logger = log;
    return *this;
}

nplex::client & nplex::client_impl::set_reactor(const std::shared_ptr<reactor> &rct)
{
    if (is_running())
        throw nplex_exception("cannot set reactor while running");

    m_reactor = rct;
    return *this;
}

nplex::client & nplex::client_impl::set_manager(const std::shared_ptr<manager> &mngr)
{
    if (is_running())
        throw nplex_exception("cannot set manager while running");

    m_manager = (mngr ? mngr : std::make_shared<manager>());

    return *this;
}

nplex::client & nplex::client_impl::set_initial_rev(rev_t rev)
{
    if (is_running())
        throw nplex_exception("cannot set initial revision while running");

    m_rev0 = rev;
    return *this;
}

void nplex::client_impl::run(std::stop_token st) noexcept
{
    assert(m_state == state_e::OFFLINE);
    assert(m_loop_thread_id == std::thread::id{});

    // Initialize the loop thread id to identify the event loop thread.
    m_loop_thread_id = std::this_thread::get_id();
    m_error = nullptr;
    m_con = nullptr;

    // Register a callback to be called when the stop token is requested.
    std::stop_callback cb(st, [this]() noexcept {
        try {
            push_command(std::make_unique<close_cmd_t>());
        } catch (...) {
            // last resort (not thread-safe, undefined behavior)
            uv_stop(m_loop.get());
        }
    });

    // Run the event loop. This call blocks until uv_stop() is called.
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
        // Close properly the event loop and all its handles.
        // This is required to avoid memory leaks.

        // TODO: call connection.disconnect()

        uv_walk(m_loop.get(), ::cb_close_handle, nullptr);
        while (uv_run(m_loop.get(), UV_RUN_NOWAIT));
        uv_loop_close(m_loop.get());
    }
    catch (const std::exception &e) {
        m_error = std::current_exception();
        log_error("{}", e.what());
    }

    log_debug("Event loop terminated");
    set_state(state_e::CLOSED);
    m_loop_thread_id = std::thread::id{};
}

void nplex::client_impl::set_state(state_e state)
{
    assert(m_loop_thread_id == std::this_thread::get_id());

    if (m_state == state)
        return;

    log_debug("State changed from {} to {}", ::to_str(m_state.load()), ::to_str(state));

    m_state = state;
    m_cv.notify_all();
}

bool nplex::client_impl::wait_for_usable(millis timeout)
{
    if (is_closed())
        throw nplex_exception("client is closed");

    if (timeout.count() > UINT32_MAX)
        timeout = millis{UINT32_MAX};

    std::unique_lock<std::mutex> lock(m_mutex);

    if (!m_cv.wait_for(lock, timeout, [this] {
        auto s = m_state.load();
        if (s == state_e::CLOSED)
            throw nplex_exception("client is closed");
        return m_initialized.load();
    })) {
        return false;
    }

    return true;
}

bool nplex::client_impl::wait_for_synced(millis timeout)
{
    if (is_closed())
        throw nplex_exception("client is closed");

    if (timeout.count() > UINT32_MAX)
        timeout = millis{UINT32_MAX};

    std::unique_lock<std::mutex> lock(m_mutex);

    if (!m_cv.wait_for(lock, timeout, [this] {
        auto s = m_state.load();
        if (s == state_e::CLOSED)
            throw nplex_exception("client is closed");
        return (m_state.load() == state_e::SYNCED);
    })) {
        return false;
    }

    return true;
}

void nplex::client_impl::send(flatbuffers::DetachedBuffer &&buf)
{
    assert(m_loop_thread_id == std::this_thread::get_id());

    if (!m_con || is_closed())
        return;

    auto type = flatbuffers::GetRoot<nplex::msgs::Message>(buf.data())->content_type();
    log_debug("Sent {} to {}", msgs::EnumNameMsgContent(type), m_con->addr().str());

    m_con->send(std::move(buf));
}

void nplex::client_impl::abort(const std::string &msg)
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
        log_trace("Connection-lost timer stopped");
        uv_timer_stop(m_timer_con_lost.get());
    }

    if (is_timer_active(m_timer_reconnect.get())) {
        log_trace("Reconnect timer stopped");
        uv_timer_stop(m_timer_reconnect.get());
    }

    set_state(state_e::CLOSED);
    log_error("{}", msg);

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_commands.clear();
    }

    uv_stop(m_loop.get());
}

void nplex::client_impl::report_server_activity()
{
    assert(m_loop_thread_id == std::this_thread::get_id());

    if (!m_con || is_closed())
        return;

    if (is_timer_active(m_timer_con_lost.get())) {
        log_trace("Resetting connection-lost timer");
        uv_timer_again(m_timer_con_lost.get());
    }
}

void nplex::client_impl::try_to_connect()
{
    assert(m_loop_thread_id == std::this_thread::get_id());

    if (is_closed() || m_state != state_e::OFFLINE)
        return;

    log_debug("Trying to connect to servers...");

    assert(m_con == nullptr);
    m_con = nullptr;

    if (is_timer_active(m_timer_reconnect.get())) {
        log_trace("Stopping reconnect timer");
        uv_timer_stop(m_timer_reconnect.get());
    }

    set_state(state_e::CONNECTING);

    for (auto &con : m_connections) {
        log_debug("Connecting to {}", con->addr().str());
        assert(con->is_closed());
        con->connect();
    }
}

void nplex::client_impl::on_connection_established(connection *con)
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

static bool are_all_connections_closed(const std::vector<nplex::connection_ptr> &connections)
{
    for (const auto &con : connections) {
        if (!con->is_closed())
            return false;
    }

    return true;
}

void nplex::client_impl::schedule_reconnect(std::uint32_t delay_ms)
{
    assert(m_loop_thread_id == std::this_thread::get_id());

    if (is_closed())
        return;

    if (is_timer_active(m_timer_reconnect.get())) {
        log_trace("Reconnect timer stopped");
        uv_timer_stop(m_timer_reconnect.get());
    }

    uv_timer_start(m_timer_reconnect.get(), ::cb_timer_reconnect, delay_ms, 0);
    log_trace("Starting reconnect timer with {} ms delay", delay_ms);
}

void nplex::client_impl::on_connection_closed(connection *con)
{
    assert(m_loop_thread_id == std::this_thread::get_id());

    log_warn("{} - {}", con->addr().str(), ::error2str(con->error()));

    if (is_closed())
        return;

    // Case: connection to cluster failed
    if (!m_con && are_all_connections_closed(m_connections))
    {
        log_debug("Unable to connect to any server");

        auto wait_time = m_manager->on_connection_failed(*this);

        // case: do not reconnect
        if (wait_time < 0) {
            abort("Client closed by manager");
            return;
        }

        // case: re-schedule reconnection
        schedule_reconnect(static_cast<std::uint32_t>(wait_time));
        return;
    }

    // Case: connection to server established previously (m_con != nullptr)
    // Case: no connection to the server (m_con == nullptr), but there are other attempts in progress
    if (con != m_con)
        return;

    // Case: current server connection failed (con == m_con)
    set_state(state_e::OFFLINE);
    m_con = nullptr;
    m_data_cid = 0;

    if (is_timer_active(m_timer_con_lost.get())) {
        log_trace("Connection-lost timer stopped");
        uv_timer_stop(m_timer_con_lost.get());
    }

    bool retry = m_manager->on_connection_lost(*this, con->addr().str());

    if (retry) {
        try_to_connect();
        return;
    }
    
    abort("Client closed by manager");
}

void nplex::client_impl::on_connection_lost()
{
    assert(m_loop_thread_id == std::this_thread::get_id());

    if (m_con) {
        log_trace("Connection-lost timer expired");
        m_con->disconnect(ERR_CON_LOST);
    }

    if (is_timer_active(m_timer_con_lost.get())) {
        log_trace("Connection-lost timer stopped");
        uv_timer_stop(m_timer_con_lost.get());
    }
}

void nplex::client_impl::push_command(command_ptr &&cmd)
{
    if (is_closed())
        return;

    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_commands.size() >= 1024)
        throw nplex_exception("command queue overflow");

    m_commands.push(std::move(cmd));

    if (m_commands.size() == 1)
        uv_async_send(m_async_command.get());
}

void nplex::client_impl::process_commands()
{
    assert(m_loop_thread_id == std::this_thread::get_id());

    if (is_closed()) {
        m_commands.clear();
        return;
    }

    command_ptr cmd;

    while (true)
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex);

            if (m_commands.empty())
                break;

            cmd = m_commands.pop();
            assert(cmd != nullptr);
        }

        if (dynamic_cast<close_cmd_t*>(cmd.get()))
            process_close_cmd(std::move(cmd));
        else if (dynamic_cast<submit_req_t*>(cmd.get()))
            process_submit_cmd(std::move(cmd));
        else if (dynamic_cast<ping_req_t*>(cmd.get()))
            process_ping_cmd(std::move(cmd));
        else {
            log_error("Unknown command type");
            assert(false);
        }
    }
}

void nplex::client_impl::process_close_cmd([[maybe_unused]] command_ptr &&cmd)
{
    assert(m_loop_thread_id == std::this_thread::get_id());
    abort("client closed by user");
}

void nplex::client_impl::process_submit_cmd(command_ptr &&cmd)
{
    assert(m_loop_thread_id == std::this_thread::get_id());

    UNUSED(cmd);
    // TODO: implement
}

void nplex::client_impl::process_ping_cmd(command_ptr &&cmd)
{
    assert(m_loop_thread_id == std::this_thread::get_id());
    
    ping_req_t *req = dynamic_cast<ping_req_t*>(cmd.get());

    if (!m_con) {
        req->promise.set_exception(std::make_exception_ptr(nplex_exception("client is not connected")));
        return;
    }

    req->cid = m_correlation++;

    send(
        create_ping_msg(
            req->cid,
            req->payload
        )
    );

    m_requests.push(cmd_to_req(cmd));

    // TODO: process m_requests on connection-loss
}

void nplex::client_impl::on_msg_delivered(connection *con, [[maybe_unused]] const msgs::Message *msg)
{
    assert(m_loop_thread_id == std::this_thread::get_id());

    if (m_con != con)
        return;

    report_server_activity();
}

void nplex::client_impl::on_msg_received(connection *con, const msgs::Message *msg)
{
    using namespace msgs;
    assert(m_loop_thread_id == std::this_thread::get_id());

    if (!msg || !msg->content()) {
        con->disconnect(ERR_MSG_ERROR);
        return;
    }

    log((msg->content_type() == MsgContent::KEEPALIVE_PUSH ? logger::log_level_e::TRACE : logger::log_level_e::DEBUG),
        "Received {} from {}", EnumNameMsgContent(msg->content_type()), con->addr().str());

    if (msg->content_type() == MsgContent::LOGIN_RESPONSE) {
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
        case MsgContent::SUBMIT_RESPONSE:
            process_submit_resp(msg->content_as_SUBMIT_RESPONSE());
            break;

        case MsgContent::SNAPSHOT_RESPONSE:
            process_snapshot_resp(msg->content_as_SNAPSHOT_RESPONSE());
            break;

        case MsgContent::UPDATES_RESPONSE:
            process_updates_resp(msg->content_as_UPDATES_RESPONSE());
            break;

        [[likely]]
        case MsgContent::UPDATES_PUSH:
            process_updates_push(msg->content_as_UPDATES_PUSH());
            break;

        [[likely]]
        case MsgContent::KEEPALIVE_PUSH:
            process_keepalive_push(msg->content_as_KEEPALIVE_PUSH());
            break;

        case MsgContent::PING_RESPONSE:
            process_ping_resp(msg->content_as_PING_RESPONSE());
            break;

        default:
            con->disconnect(ERR_MSG_ERROR);
    }
}

void nplex::client_impl::process_login_resp(connection *con, const nplex::msgs::LoginResponse *resp)
{
    assert(m_loop_thread_id == std::this_thread::get_id());

    if (m_con) {
        con->disconnect(ERR_ALREADY_CONNECTED);
        return;
    }

    assert(m_state == state_e::CONNECTING);

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

    m_con = con;
    set_state(state_e::AUTHENTICATED);

    log_info("Login successful on server {}", m_con->addr().str());
    log_debug("Server info: rev = {}, min-rev = {}, keepalive = {}ms", resp->crev(), resp->rev0(), resp->keepalive());
    log_debug("User info: can-force = {}, permissions = [{}]", m_can_force, fmt::join(m_permissions, ", "));

    if (resp->keepalive())
    {
        auto timeout = static_cast<uint64_t>(resp->keepalive() * static_cast<double>(m_params.connection.timeout_factor));
        uv_timer_start(m_timer_con_lost.get(), ::cb_timer_connection_lost, timeout, timeout);
        log_trace("Started connection-lost timer with {} ms timeout", timeout);
    }

    // Closing other connection attempts
    for (auto &server : m_connections)
        if (server.get() != m_con)
            server->disconnect(ERR_ALREADY_CONNECTED);

    // case: initialized
    if (m_initialized.load())
    {
        set_state(state_e::SYNCING);
        m_data_cid = m_correlation++;

        send(
            create_updates_msg(
                m_data_cid,
                m_store->m_rev
            )
        );

        return;
    }

    // case: no snapshot required
    if (m_rev0 == 0 )
    {
        set_state(state_e::SYNCING);
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

    set_state(state_e::LOADING_SNAPSHOT);
    m_data_cid = m_correlation++;

    send(
        create_snapshot_msg(
            m_data_cid, 
            srev
        )
    );
}

void nplex::client_impl::process_snapshot_resp(const nplex::msgs::SnapshotResponse *resp)
{
    assert(m_loop_thread_id == std::this_thread::get_id());

    if (m_state != state_e::LOADING_SNAPSHOT || resp->cid() != m_data_cid) {
        m_con->disconnect(ERR_MSG_UNEXPECTED);
        return;
    }

    if (!resp->accepted()) {
        m_con->disconnect(ERR_LOAD);
        return;
    }

    if (resp->snapshot())
        m_store->load(resp->snapshot());

    if (m_reactor) m_reactor->on_snapshot(*this);

    m_initialized = true;
    set_state(state_e::SYNCING);
    m_data_cid = m_correlation++;

    send(
        create_updates_msg(
            m_data_cid, 
            m_store->m_rev
        )
    );
}

void nplex::client_impl::process_updates_resp(const nplex::msgs::UpdatesResponse *resp)
{
    assert(m_loop_thread_id == std::this_thread::get_id());

    if (m_state != state_e::SYNCING || resp->cid() != m_data_cid) {
        m_con->disconnect(ERR_MSG_UNEXPECTED);
        return;
    }

    if (!resp->accepted()) {
        m_con->disconnect(ERR_LOAD);
        return;
    }

    // Case: no snapshot installed, only updates
    if (m_rev0 == 0) {
        m_store->m_rev = resp->crev();
        m_initialized = true;
    }

    // Case: snapshot has same rev than current rev
    if (m_store->m_rev == resp->crev())
        set_state(state_e::SYNCED);
}

void nplex::client_impl::process_submit_resp(const nplex::msgs::SubmitResponse *resp)
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

void nplex::client_impl::process_updates_push(const nplex::msgs::UpdatesPush *resp)
{
    assert(m_loop_thread_id == std::this_thread::get_id());

    if (resp->cid() != m_data_cid) {
        m_con->disconnect(ERR_MSG_UNEXPECTED);
        return;
    }

    auto updates = resp->updates();

    if (updates)
    {
        for (auto upd : *updates)
        {
            process_update(upd);

            if (m_state == state_e::SYNCING && upd->rev() == resp->crev())
                set_state(state_e::SYNCED);
        }
    }
}

void nplex::client_impl::process_update(const nplex::msgs::Update *upd)
{
    assert(m_loop_thread_id == std::this_thread::get_id());

    std::lock_guard<decltype(m_store->m_mutex)> lock(m_store->m_mutex);

    // update the local database
    auto changes = m_store->update(upd);

    // update current transactions and remove the closed ones
    for (auto it = m_transactions.begin(); it != m_transactions.end(); )
    {
        auto tx = *it;

        switch (tx->state())
        {
            case transaction::state_e::OPEN:
                tx->update(changes);
                ++it;
                break;

            case transaction::state_e::REJECTED:
            case transaction::state_e::COMMITTED:
            case transaction::state_e::DISCARDED:
            case transaction::state_e::ABORTED:
                it = m_transactions.erase(it);
                break;

            case transaction::state_e::SUBMITTING:
            case transaction::state_e::SUBMITTED:
                ++it;
                break;

            case transaction::state_e::ACCEPTED:
                // TODO: try to match it with the update
                ++it;
                break;
        }
    }

    auto meta = m_store->m_metas.rbegin();
    assert(meta != m_store->m_metas.rend());

    // update business objects and trigger actions through the reactor
    if (m_reactor) m_reactor->on_update(*this, meta->second, changes);
}

void nplex::client_impl::process_keepalive_push(const nplex::msgs::KeepAlivePush *resp)
{
    assert(m_loop_thread_id == std::this_thread::get_id());

    UNUSED(resp);

    // do nothing
    // catched by report_server_activity
}

void nplex::client_impl::process_ping_resp(const nplex::msgs::PingResponse *resp)
{
    assert(m_loop_thread_id == std::this_thread::get_id());

    if (m_requests.empty())
        return;

    if (m_requests.front()->cid != resp->cid())
        return;

    auto ptr = m_requests.pop();
    auto req = dynamic_cast<ping_req_t*>(ptr.get());

    auto latency = std::chrono::duration_cast<usec>(clock::now() - req->t0);

    req->promise.set_value(latency);
}

nplex::tx_ptr nplex::client_impl::create_tx(transaction::isolation_e isolation, bool read_only)
{
    if (is_closed())
        throw nplex_exception("Client is closed");

    std::lock_guard<decltype(m_mutex)> lock_impl(m_mutex);

    size_t num_txs = m_transactions.size();
    if (num_txs >= m_params.max_active_txs)
        throw nplex_exception("Too many concurrent transactions (max={})", m_params.max_active_txs);

    std::lock_guard<decltype(m_store->m_mutex)> lock_store(m_store->m_mutex);

    auto tx = std::make_shared<transaction_impl>(m_store, isolation, read_only);
    m_transactions.insert(tx);

    log_debug("Transaction created, isolation={}, read_only={}", to_str(isolation), read_only);

    return tx;
}

#if 0
bool nplex::client::submit_tx(const tx_ptr &tx, bool force)
{
    if (!tx)
        throw std::invalid_argument("Transaction is empty");

    if (tx->state() != transaction::state_e::OPEN)
        throw nplex_exception("Transaction is not open");

    std::lock_guard<decltype(m_mutex)> lock(m_mutex);

    if (!m_impl->is_connected())
        throw nplex_exception("Client is not connected");

    //TODO: caution, m_impl->transactions is not thread-safe

    auto it = m_impl->transactions.find(tx);
    if (it == m_impl->transactions.end())
        throw nplex_exception("Transaction not found");

    // TODO: check if there are actions to submit (not-only-reads)

    m_impl->push_command(submit_cmd_t{dynamic_pointer_cast<transaction_impl>(tx), force});

    // TODO: solve visibility error
    //tx->state(transaction::state_e::SUBMITTING);

    return true;
}
#endif

std::future<nplex::client::usec> nplex::client_impl::ping(const std::string &payload) 
{
    if (is_closed())
        throw nplex_exception("Client is closed");

    auto req = std::make_unique<ping_req_t>(payload);
    auto future = req->promise.get_future();

    if (m_loop_thread_id == std::this_thread::get_id())
        process_ping_cmd(std::move(req));
    else
        push_command(std::move(req));

    return future;
}
