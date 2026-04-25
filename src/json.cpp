#include <fmt/core.h>
#include "utf8.h"
#include "utils.hpp"
#include "json.hpp"

static constexpr size_t k_max_binary_preview = 12;

// ==========================================================
// Internal (static) functions
// ==========================================================

template<typename T, std::enable_if_t<std::is_integral_v<T>, int> = 0>
static void json_append_number(T value, std::string &out)
{
    out += std::to_string(value);
}

static void json_append_text(std::string_view text, std::string &out)
{
    out.push_back('"');
    out += text;
    out.push_back('"');
}

static void json_append_escaped_text(std::string_view text, std::string &out)
{
    out.push_back('"');

    for (char c : text)
    {
        switch (c)
        {
            case '\"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:   out += c;      break;  // assume valid utf8
        }
    }

    out.push_back('"');
}

static void json_append_raw_bytes(const char *content, size_t length, std::string &out, size_t max_preview)
{
    if (content == nullptr) {
        out += "null";
        return;
    }

    bool is_utf8 = !(utf8nvalid(reinterpret_cast<const utf8_int8_t *>(content), length));

    if (is_utf8) {
        json_append_escaped_text({content, length}, out);
        return;
    }

    out += "\"<";

    for (size_t i = 0; i < std::min(length, max_preview); ++i)
        out += fmt::format("\\x{:02x}", static_cast<unsigned char>(content[i]));

    if (length > max_preview)
        out += "...";

    out += ">\"";
}

static const char *to_string(nplex::change_t::action_e action)
{
    switch (action)
    {
        case nplex::change_t::action_e::CREATE: return "CREATE";
        case nplex::change_t::action_e::UPDATE: return "UPDATE";
        case nplex::change_t::action_e::DELETE: return "DELETE";
    }

    return "UNKNOWN";
}

static const char *to_string(nplex::session_t::code_e code)
{
    switch (code)
    {
        case nplex::session_t::code_e::CONNECTED:        return "CONNECTED";
        case nplex::session_t::code_e::CLOSED_BY_SERVER: return "CLOSED_BY_SERVER";
        case nplex::session_t::code_e::CLOSED_BY_USER:   return "CLOSED_BY_USER";
        case nplex::session_t::code_e::COMM_ERROR:       return "COMM_ERROR";
        case nplex::session_t::code_e::CON_LOST:         return "CON_LOST";
        case nplex::session_t::code_e::EXCD_LIMITS:      return "EXCD_LIMITS";
    }

    return "UNKNOWN";
}

static void json_append_change(const nplex::change_t &change, std::string &out)
{
    out += "{";
    json_append_text("action", out);
    out += ":";
    json_append_text(to_string(change.action), out);
    out += ",";

    json_append_text("key", out);
    out += ":";
    json_append_escaped_text(change.key.view(), out);

    if (change.action != nplex::change_t::action_e::DELETE)
    {
        out += ",";
        json_append_text("new_value", out);
        out += ":";
        json_append_raw_bytes(change.new_value->data().data(), change.new_value->data().size(), out, k_max_binary_preview);
    }

    if (change.action != nplex::change_t::action_e::CREATE)
    {
        out += ",";
        json_append_text("old_value", out);
        out += ":";
        json_append_raw_bytes(change.old_value->data().data(), change.old_value->data().size(), out, k_max_binary_preview);
    }

    out += "}";
}

static void json_append_keyval(const nplex::key_t &key, const nplex::value_t &value, std::string &out)
{
    out += "{";
    json_append_text("key", out);
    out += ":";
    json_append_escaped_text(key.view(), out);
    out += ",";

    json_append_text("value", out);
    out += ":";
    json_append_raw_bytes(value.data().data(), value.data().size(), out, k_max_binary_preview);
    out += ",";

    json_append_text("rev", out);
    out += ":";
    json_append_number(value.rev(), out);
    out += "}";
}

static void json_append_session(const nplex::session_t &session, std::string &out)
{
    out += "{";
    json_append_text("user", out);
    out += ":";
    json_append_escaped_text(session.user, out);
    out += ",";

    json_append_text("address", out);
    out += ":";
    json_append_escaped_text(session.address, out);
    out += ",";

    json_append_text("since", out);
    out += ":";
    json_append_text(nplex::to_iso8601(session.since), out);
    out += "}";
}

static void json_append_event_session(const nplex::session_t &session, std::string &out)
{
    out += "{";
    json_append_text("user", out);
    out += ":";
    json_append_escaped_text(session.user, out);
    out += ",";

    json_append_text("address", out);
    out += ":";
    json_append_escaped_text(session.address, out);
    out += ",";

    json_append_text("code", out);
    out += ":";
    json_append_text(to_string(session.code), out);
    out += ",";

    json_append_text("since", out);
    out += ":";
    json_append_text(nplex::to_iso8601(session.since), out);

    if (session.until.count() != 0)
    {
        out += ",";
        json_append_text("until", out);
        out += ":";
        json_append_text(nplex::to_iso8601(session.until), out);
    }

    out += "}";
}

static void json_append_event_data(const nplex::const_meta_ptr &meta, const std::vector<nplex::change_t> &changes, std::string &out)
{
    out += "{";
    json_append_text("rev", out);
    out += ":";
    json_append_number(meta ? meta->rev : 0, out);
    out += ",";

    json_append_text("user", out);
    out += ":";
    json_append_escaped_text(meta ? meta->user.view() : std::string_view{}, out);
    out += ",";

    json_append_text("timestamp", out);
    out += ":";
    json_append_text(nplex::to_iso8601(meta ? meta->timestamp : nplex::millis_t{0}), out);
    out += ",";

    json_append_text("tx_type", out);
    out += ":";
    json_append_number(meta ? meta->tx_type : 0, out);
    out += ",";

    json_append_text("changes", out);
    out += ":";
    out += "[";

    for (size_t i = 0; i < changes.size(); ++i)
    {
        json_append_change(changes[i], out);

        if (i + 1 < changes.size())
            out += ",";
    }

    out += "]";
    out += "}";
}

// ==========================================================
// JSON functions
// ==========================================================

std::string nplex::data_to_json(const key_t &key, const value_t &value)
{
    std::string str;

    str.reserve(512);

    str += "{";
    json_append_text("data", str);
    str += ":";
    json_append_keyval(key, value, str);
    str += "}";

    return str;
}

std::string nplex::session_to_json(const session_t &session)
{
    std::string str;

    str.reserve(512);

    str += "{";
    json_append_text("session", str);
    str += ":";
    json_append_session(session, str);
    str += "}";

    return str;
}

std::string nplex::event_data_to_json(const const_meta_ptr &meta, const std::vector<change_t> &changes)
{
    std::string str;

    str.reserve(1024);

    str += "{";
    json_append_text("event_data", str);
    str += ":";
    json_append_event_data(meta, changes, str);
    str += "}";

    return str;
}

std::string nplex::event_session_to_json(const session_t &session)
{
    std::string str;

    str.reserve(512);

    str += "{";
    json_append_text("event_session", str);
    str += ":";
    json_append_event_session(session, str);
    str += "}";

    return str;
}
