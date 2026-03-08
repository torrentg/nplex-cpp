#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include <fmt/ranges.h>
#include <fmt/format.h>
#include "misc.hpp"

#define CRUD_READ       0x01
#define CRUD_CREATE     0x02
#define CRUD_UPDATE     0x04
#define CRUD_DELETE     0x08

namespace nplex {

struct acl_t
{
    std::uint8_t mode;                      //!< Attributes (1=CREATE, 2=READ, 4=UPDATE, 8=DELETE).
    std::string pattern;                    //!< Glob pattern (ex: "foo?", "**/logs/*.txt").
};

struct user_t
{
    std::string name;                       //!< User name.
    std::string password;                   //!< User password.
    std::vector<acl_t> permissions;         //!< User permissions (fixed by server at login).
    bool can_force = false;                 //!< User can force transactions (fixed by server at login).

    /**
     * Check if the user is authorized to perform all the requested
     * operations on a key according to the user's permissions.
     *
     * ACLs are evaluated in order, and the first ACL whose pattern matches
     * the key determines the final result (later ACLs are ignored).
     *
     * @param[in] mode Bitwise combination of CRUD_CREATE, CRUD_READ,
     *                 CRUD_UPDATE, CRUD_DELETE indicating the requested
     *                 operations.
     * @param[in] key  The key on which the operation is to be performed.
     *
     * @return true  = authorized to perform all requested operations,
     *         false = not authorized for at least one of them.
     */
    bool is_authorized(uint8_t mode, const char *key) const;
};

using const_user_ptr = std::shared_ptr<const user_t>;
using user_ptr = std::shared_ptr<user_t>;

} // namespace nplex

/**
 * Formatter specialization for nplex::acl_t to be used with fmt library.
 *
 * @tparam Context The formatting context.
 */
template <>
struct fmt::formatter<nplex::acl_t>
{
    static constexpr auto parse(fmt::format_parse_context &ctx) { 
        return ctx.begin();
    }

    template <typename Context>
    auto format (nplex::acl_t const &obj, Context &ctx) const -> decltype(ctx.out()) {
        return fmt::format_to(ctx.out(), "{}:{}", nplex::crud_to_string(obj.mode), obj.pattern);
    }
};
