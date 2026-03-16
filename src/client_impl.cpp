#include <cassert>
#include <limits>
#include <fmt/format.h>
#include "misc.hpp"
#include "user.hpp"
#include "store.hpp"
#include "messaging.hpp"
#include "client_impl.hpp"

#define MAX_MILLIS_IN_REACTOR  250

// ==========================================================
// Internal (static) functions
// ==========================================================

static const char * to_str(nplex::client_impl::state_e state)
{
    switch (state)
    {
        case nplex::client_impl::state_e::OFFLINE:           return "OFFLINE";
        case nplex::client_impl::state_e::CONNECTING:        return "CONNECTING";
        case nplex::client_impl::state_e::AUTHENTICATED:     return "AUTHENTICATED";
        case nplex::client_impl::state_e::LOADING_SNAPSHOT:  return "LOADING_SNAPSHOT";
        case nplex::client_impl::state_e::INITIALIZED:       return "INITIALIZED";
        case nplex::client_impl::state_e::SYNCING:           return "SYNCING";
        case nplex::client_impl::state_e::SYNCED:            return "SYNCED";
        case nplex::client_impl::state_e::CLOSED:            return "CLOSED";
        default:                                             return "UNKNOWN";
    }
}

template<typename T, typename Q>
static std::unique_ptr<T> convert_ptr(std::unique_ptr<Q> &req)
{
    auto obj = dynamic_cast<T*>(req.get());
    assert(obj);
    std::unique_ptr<T> ret{};
    ret.reset(obj);
    (void) req.release();
    return ret;
}

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

    if (m_params.max_active_txs == 0)
        m_params.max_active_txs = UINT32_MAX;

    m_user = nullptr;
    m_store = std::make_shared<store_t>();
    m_manager = std::make_shared<manager>();

    // initialize the event loop
    if ((rc = uv_loop_init(&m_loop)) != 0)
        throw nplex_exception(uv_strerror(rc));
    m_loop.data = this;

    // initialize the connection-lost timer
    if ((rc = uv_timer_init(&m_loop, &m_timer_con_lost)) != 0)
        throw nplex_exception(uv_strerror(rc));
    m_timer_con_lost.data = this;

    // initialize the reconnect timer
    if ((rc = uv_timer_init(&m_loop, &m_timer_reconnect)) != 0)
        throw nplex_exception(uv_strerror(rc));
    m_timer_reconnect.data = this;

    // install the async handler
    if ((rc = uv_async_init(&m_loop, &m_async_command, ::cb_process_async_commands)) != 0)
        throw nplex_exception(uv_strerror(rc));
    m_async_command.data = this;

    // install the SIGINT (Ctrl-C) handler
    if ((rc = uv_signal_init(&m_loop, &m_signal_sigint)) != 0)
        throw nplex_exception(uv_strerror(rc));
    m_signal_sigint.data = this;
    if ((rc = uv_signal_start(&m_signal_sigint, ::cb_signal_sigint, SIGINT)) != 0)
        throw nplex_exception(uv_strerror(rc));

    // create connections
    for (const auto &server : m_params.servers)
        m_connections.push_back(connection::create(addr_t{server}, &m_loop, m_params.connection));
}

nplex::client_impl::~client_impl()
{
    assert(is_closed());
    assert(!is_running());
}

nplex::client & nplex::client_impl::set_logger(const logger_ptr &log)
{
    if (is_running())
        throw nplex_exception("cannot set logger while running");

    loggable::set_logger(log);
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
    if (is_running() || m_state != state_e::OFFLINE) {
        assert(false);
        return;
    }

    // Initialize the loop thread id to identify the event loop thread.
    m_running.store(true);
    m_loop_thread_id = std::this_thread::get_id();
    m_error = nullptr;
    m_con = nullptr;

    // Register a callback to be called when the stop token is requested.
    std::stop_callback cb(st, [this]() noexcept {
        try {
            push_command(std::make_unique<close_cmd_t>());
        } catch (...) {
            // last resort (not thread-safe, undefined behavior)
            uv_stop(&m_loop);
        }
    });

    // Run the event loop
    try
    {
        log_debug("Event loop started");
        try_to_connect();
        uv_run(&m_loop, UV_RUN_DEFAULT);
    }
    catch (const std::exception &e) {
        log_error("{}", e.what());
        m_error = std::current_exception();
    }

    // Close the event loop
    try
    {
        set_state(state_e::CLOSED);
        cancel_requests();

        for (auto &con : m_connections)
            con->disconnect(ERR_CLOSED_BY_LOCAL);

        uv_walk(&m_loop, ::cb_close_handle, nullptr);
        while (uv_run(&m_loop, UV_RUN_NOWAIT));
        uv_loop_close(&m_loop);
    }
    catch (const std::exception &e) {
        m_error = std::current_exception();
        log_error("{}", e.what());
    }

    // Reset containers
    m_transactions.clear();
    m_connections.clear();
    m_commands.clear();
    m_requests.clear();
    m_accepted.clear();
    m_con = nullptr;

    // Terminate the client
    log_debug("Event loop terminated");
    m_loop_thread_id = std::thread::id{};
    m_running.store(false);
}

void nplex::client_impl::cancel_requests()
{
    assert(m_loop_thread_id == std::this_thread::get_id());

    // cancel ongoing requests
    while (!m_requests.empty()) {
        auto req = m_requests.pop();
        req->cancel();
    }

    // cancel ongoing transactions
    while (!m_accepted.empty()) {
        auto req = m_accepted.pop();
        req->cancel();
    }
}

void nplex::client_impl::set_state(state_e state)
{
    assert(m_loop_thread_id == std::this_thread::get_id());

    if (m_state == state)
        return;

    log_debug("State changed from {} to {}", ::to_str(m_state.load()), ::to_str(state));

    m_state = state;

    if (m_state == state_e::CLOSED)
    {
        std::lock_guard<std::mutex> lock_transactions(m_mutex_transactions);
        std::lock_guard<std::mutex> lock_commands(m_mutex_commands);

        m_transactions.clear();
        m_commands.clear();
        m_requests.clear();
        m_accepted.clear();
    }

    m_cv.notify_all();
}

bool nplex::client_impl::wait_for_populated(millis timeout)
{
    if (is_closed())
        throw nplex_exception("client is closed");

    if (timeout.count() > UINT32_MAX)
        timeout = millis{UINT32_MAX};

    std::unique_lock<std::mutex> lock(m_mutex_commands);

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

    std::unique_lock<std::mutex> lock(m_mutex_commands);

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

    if (is_timer_active(&m_timer_con_lost)) {
        log_trace("Connection-lost timer stopped");
        uv_timer_stop(&m_timer_con_lost);
    }

    if (is_timer_active(&m_timer_reconnect)) {
        log_trace("Reconnect timer stopped");
        uv_timer_stop(&m_timer_reconnect);
    }

    set_state(state_e::CLOSED);
    log_error("{}", msg);

    {
        std::lock_guard<std::mutex> lock_commands(m_mutex_commands);
        m_commands.clear();
    }

    uv_stop(&m_loop);
}

void nplex::client_impl::report_server_activity()
{
    assert(m_loop_thread_id == std::this_thread::get_id());

    if (!m_con || is_closed())
        return;

    if (is_timer_active(&m_timer_con_lost)) {
        log_trace("Resetting connection-lost timer");
        uv_timer_again(&m_timer_con_lost);
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

    if (is_timer_active(&m_timer_reconnect)) {
        log_trace("Stopping reconnect timer");
        uv_timer_stop(&m_timer_reconnect);
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

    if (is_timer_active(&m_timer_reconnect)) {
        log_trace("Reconnect timer stopped");
        uv_timer_stop(&m_timer_reconnect);
    }

    uv_timer_start(&m_timer_reconnect, ::cb_timer_reconnect, delay_ms, 0);
    log_trace("Starting reconnect timer with {} ms delay", delay_ms);
}

void nplex::client_impl::on_connection_closed(connection *con)
{
    assert(con != nullptr);
    assert(m_loop_thread_id == std::this_thread::get_id());

    log_warn("{} - {}", con->addr().str(), strerror(con->error()));

    if (is_closed())
        return;

    // Case: connection to cluster failed
    if (!m_con && are_all_connections_closed(m_connections))
    {
        log_debug("Unable to connect to any server");

        std::int32_t wait_time = -1;
        
        try {
            wait_time = m_manager->on_connection_failed(*this);
        } catch (const std::exception &e) {
            log_error("Exception in on_connection_failed callback: {}", e.what());
            wait_time = -1;
        } catch (...) {
            log_error("Unknown exception in on_connection_failed callback");
            wait_time = -1;
        }

        // case: do not reconnect
        if (wait_time < 0) {
            abort("Client closed by manager");
            return;
        }

        // case: re-schedule reconnection
        schedule_reconnect(static_cast<std::uint32_t>(wait_time));
        return;
    }

    // Case: already connected to another server (m_con != nullptr)
    // Case: no connection to the server (m_con == nullptr), but there are other attempts in progress
    if (con != m_con)
        return;

    // Case: current server connection failed (con == m_con)
    set_state(state_e::OFFLINE);
    m_con = nullptr;
    m_data_cid = 0;

    // Cancel ongoing requests and transactions
    cancel_requests();

    // close timers
    if (is_timer_active(&m_timer_con_lost)) {
        log_trace("Connection-lost timer stopped");
        uv_timer_stop(&m_timer_con_lost);
    }

    bool retry = false;
    
    try {
        retry = m_manager->on_connection_lost(*this, con->addr().str());
    } catch (const std::exception &e) {
        log_error("Exception in on_connection_lost callback: {}", e.what());
        retry = false;
    } catch (...) {
        log_error("Unknown exception in on_connection_lost callback");
        retry = false;
    }

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

    if (is_timer_active(&m_timer_con_lost)) {
        log_trace("Connection-lost timer stopped");
        uv_timer_stop(&m_timer_con_lost);
    }
}

void nplex::client_impl::push_command(command_ptr &&cmd)
{
    if (is_closed())
        return;

    std::lock_guard<std::mutex> lock_commands(m_mutex_commands);

    // worst scenario: max_active_txs + close + ping
    if (m_commands.size() >= m_params.max_active_txs + 2)
        throw nplex_exception("command queue overflow");

    m_commands.push(std::move(cmd));

    if (m_commands.size() == 1)
        uv_async_send(&m_async_command);
}

void nplex::client_impl::process_commands()
{
    assert(m_loop_thread_id == std::this_thread::get_id());

    if (is_closed())
        return;

    command_ptr cmd;

    while (true)
    {
        {
            std::lock_guard<std::mutex> lock_commands(m_mutex_commands);

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

    auto *req = dynamic_cast<submit_req_t*>(cmd.get());

    if (!m_con) {
        req->tx->set_submit_result(std::make_exception_ptr(nplex_exception("client is not connected")));
        return;
    }

    req->cid = m_correlation++;

    send(
        create_submit_msg(
            req->cid,
            m_store->m_rev,
            req->force,
            req->tx
        )
    );

    m_requests.push(convert_ptr<request_t>(cmd));
}

void nplex::client_impl::process_ping_cmd(command_ptr &&cmd)
{
    assert(m_loop_thread_id == std::this_thread::get_id());
    
    auto *req = dynamic_cast<ping_req_t*>(cmd.get());

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

    m_requests.push(convert_ptr<request_t>(cmd));
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

    auto usr = std::make_shared<user_t>();
    usr->name = m_params.user;
    usr->can_force = resp->can_force();
    usr->permissions.clear();

    if (resp->permissions())
    {
        for (flatbuffers::uoffset_t i = 0; i < resp->permissions()->size(); i++) {
            auto acl = resp->permissions()->Get(i);
            usr->permissions.push_back({acl->mode(), acl->pattern()->str()});
        }
    }

    if (usr->permissions.empty()) {
        con->disconnect(ERR_AUTH);
        return;
    }

    m_con = con;
    m_user.store(usr);
    set_state(state_e::AUTHENTICATED);

    log_info("Login successful on server {}", m_con->addr().str());
    log_debug("Server info: rev = {}, min-rev = {}, keepalive = {}ms", resp->crev(), resp->rev0(), resp->keepalive());
    log_debug("User info: can-force = {}, permissions = [{}]", m_user.load()->can_force, fmt::join(m_user.load()->permissions, ", "));

    if (resp->keepalive())
    {
        auto timeout = static_cast<uint64_t>(resp->keepalive() * static_cast<double>(m_params.connection.timeout_factor));
        uv_timer_start(&m_timer_con_lost, ::cb_timer_connection_lost, timeout, timeout);
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

    if (m_reactor)
    {
        try {
            m_reactor->on_snapshot(*this);
        } catch (const std::exception &e) {
            log_error("Error in reactor on_snapshot: {}", e.what());
            m_con->disconnect(ERR_CLOSED_BY_LOCAL);
            abort("Error in on_snapshot");
            return;
        } catch (...) {
            log_error("Error in reactor on_snapshot");
            m_con->disconnect(ERR_CLOSED_BY_LOCAL);
            abort("Error in on_snapshot");
            return;
        }
    }

    m_initialized = true;
    set_state(state_e::INITIALIZED);
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
        std::lock_guard<decltype(m_store->m_mutex)> lock(m_store->m_mutex);
        m_store->m_rev = resp->crev();
        m_initialized = true;
        set_state(state_e::INITIALIZED);
    }

    // Case: snapshot has same rev than current rev
    if (m_store->m_rev == resp->crev())
        set_state(state_e::SYNCED);
}

void nplex::client_impl::process_submit_resp(const nplex::msgs::SubmitResponse *resp)
{
    assert(m_loop_thread_id == std::this_thread::get_id());

    while (!m_requests.empty() && m_requests.front()->cid < resp->cid()) {
        log_error("Received response with cid {} but expecting cid {}, discarding it", resp->cid(), m_requests.front()->cid);
        m_requests.pop();
    }

    if (m_requests.empty() || m_requests.front()->cid != resp->cid())
        return;

    auto ptr = m_requests.pop();
    auto req = dynamic_cast<submit_req_t*>(ptr.get());

    auto latency = std::chrono::duration_cast<usec>(clock::now() - req->t0);

    log_debug("Received submit response from {} with code {} ({} usec)", m_con->addr().str(), msgs::EnumNameSubmitCode(resp->code()), latency.count());

    req->tx->set_submit_result(resp->code());

    if (resp->code() != msgs::SubmitCode::ACCEPTED)
        return;

    req->rev = resp->erev();

    m_accepted.push(convert_ptr<submit_req_t>(ptr));
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

    // update the local database
    auto [changes, meta] = m_store->update(upd);
    assert(meta && meta->rev == upd->rev());
    log_debug("Store updated to rev {} with {} changes", meta->rev, changes.size());

    // check accepted commits with pending update
    while (!m_accepted.empty() && m_accepted.front()->rev < meta->rev) {
        log_error("Discarding accepted tx with rev {} at rev {}", m_accepted.front()->rev, meta->rev);
        auto req = m_accepted.pop();
        req->tx->set_submit_result(std::make_exception_ptr(nplex_exception("unexpected update received")));
        assert(false);
    }

    if (!m_accepted.empty() && m_accepted.front()->rev == meta->rev) {
        auto req = m_accepted.pop();
        req->tx->confirm_commit(meta->rev);
        log_debug("Tx committed in {} usec", std::chrono::duration_cast<usec>(clock::now() - req->t0).count());
    }

    std::size_t num_unused_txs = 0;

    // update current transactions and remove the closed ones
    {
        std::lock_guard<std::mutex> lock_transactions(m_mutex_transactions);

        for (auto it = m_transactions.begin(); it != m_transactions.end(); )
        {
            auto &tx = *it;

            if (tx.use_count() == 1 || tx->is_closed()) {
                num_unused_txs++;
                ++it;
                continue;
            }

            switch (tx->state())
            {
                case transaction::state_e::OPEN:
                    tx->update(changes);
                    ++it;
                    break;

                case transaction::state_e::SUBMITTED:
                case transaction::state_e::ACCEPTED:
                    ++it;
                    break;

                case transaction::state_e::REJECTED:
                case transaction::state_e::COMMITTED:
                case transaction::state_e::DISCARDED:
                case transaction::state_e::ABORTED:
                    it = m_transactions.erase(it);
                    break;
            }
        }
    }

    if (num_unused_txs > 0)
        purge_unused_txs();

    // update business objects and trigger actions through the reactor
    if (m_reactor)
    {
        auto t0 = clock::now();

        try {
            m_reactor->on_update(*this, meta, changes);
        } catch (const std::exception &e) {
            log_error("Error in reactor on_update: {}", e.what());
        } catch (...) {
            log_error("Unknown error in reactor on_update");
        }

        auto duration = std::chrono::duration_cast<millis>(clock::now() - t0);
        if (duration > millis{MAX_MILLIS_IN_REACTOR})
            log_warn("on_update(r{}) too slow ({} usec)", upd->rev(), duration.count());
    }
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

    purge_unused_txs();

    std::lock_guard<std::mutex> lock_transactions(m_mutex_transactions);

    size_t num_txs = m_transactions.size();
    if (num_txs >= m_params.max_active_txs)
        throw nplex_exception("Too many concurrent transactions (max={})", m_params.max_active_txs);

    auto tx = std::make_shared<transaction_impl>(shared_from_this(), isolation, read_only);
    m_transactions.insert(tx);

    return tx;
}

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

void nplex::client_impl::purge_unused_txs()
{
    std::lock_guard<std::mutex> lock_transactions(m_mutex_transactions);

    for (auto it = m_transactions.begin(); it != m_transactions.end(); )
    {
        if ((*it).use_count() == 1 || (*it)->is_closed())
            it = m_transactions.erase(it);
        else
            ++it;
    }
}
