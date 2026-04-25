#include <fmt/core.h>
#include "utf8.h"
#include "base64.hpp"
#include "utils.hpp"
#include "json.hpp"

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

    const char *p = text.data();
    const char *end = p + text.size();
    const char *start = p;

    while (p < end)
    {
        unsigned char c = static_cast<unsigned char>(*p);

        if (c >= 0x20 && c != '"' && c != '\\') {
            ++p;
            continue;
        }

        if (p > start)
            out.append(start, p);

        switch (c)
        {
            case '"':  out.append("\\\""); break;
            case '\\': out.append("\\\\"); break;
            case '\b': out.append("\\b");  break;
            case '\f': out.append("\\f");  break;
            case '\n': out.append("\\n");  break;
            case '\r': out.append("\\r");  break;
            case '\t': out.append("\\t");  break;
            default: {
                char buf[7];
                snprintf(buf, sizeof(buf), "\\u%04x", static_cast<int>(c));
                out.append(buf);
                break;
            }
        }

        start = ++p;
    }

    if (p > start)
        out.append(start, p);

    out.push_back('"');
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

static void json_append_value(const nplex::value_t &value, std::string &out)
{
    const char *content = value.data().data();
    size_t length = value.data().size();
    bool is_utf8 = !utf8nvalid(reinterpret_cast<const utf8_int8_t *>(content), length);

    out += "{";

    json_append_text("data", out);
    out += ":";

    if (is_utf8)
    {
        json_append_escaped_text({content, length}, out);
        out += ",";
        json_append_text("encoding", out);
        out += ":";
        json_append_text("text", out);
    }
    else
    {
        json_append_text(base64::to_base64({content, length}), out);
        out += ",";
        json_append_text("encoding", out);
        out += ":";
        json_append_text("base64", out);
    }

    out += ",";
    json_append_text("rev", out);
    out += ":";
    json_append_number(value.rev(), out);
    out += "}";
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
        json_append_value(*change.new_value, out);
    }

    if (change.action != nplex::change_t::action_e::CREATE)
    {
        out += ",";
        json_append_text("old_value", out);
        out += ":";
        json_append_value(*change.old_value, out);
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
    json_append_value(value, out);
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
