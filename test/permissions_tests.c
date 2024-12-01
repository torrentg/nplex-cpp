#include "acutest.h"
#include "permissions.h"
#include "match.h"

// gcc -std=c11 -g -Wall -Wextra -D_DEFAULT_SOURCE -DRUNNING_ON_VALGRIND -I../src -I../deps -o permissions_tests permissions_tests.c ../deps/match.c ../src/permissions.c
// valgrind --tool=memcheck --leak-check=yes ./permissions_tests

#define CRUD(c, r, u, d) (crud_t){.create = c, .read = r, .update = u, .delete = d}

static permission_t list[] = {
    { CRUD(false, true , true , false), "energy/**" },                  // user jdoe can read/update all energy entries
    { CRUD(true , true , true , true ), "alarms/**" },                  // user jdoe can do anything under alarms
    { CRUD(false, true , true , false), "tracks/t001/value" },          // user jdoe can read/update track1.value
    { CRUD(false, true , false, false), "tracks/**" },                  // user jdoe can read all track entries
    { CRUD(false, false, false, false), "system/user/jdoe/password" },  // user jdoe can not read his password
    { CRUD(false, true , false, false), "system/user/jdoe/**" },        // user jdoe can read his data
    { CRUD(false, true , false, false), "system/user/*/status" },       // user jdoe can access other users status (only read)
    { CRUD(false, true,  false, false), "system/server/**" },           // user jdoe can read servers entries
    { CRUD(false, false, false, false), "system/**" },                  // system directory is not accessible
};

static void test_match(void)
{
    // pattern '*': anything except a / (lazy match)
    // ---------------------------------------------

    // * matches a, b but not x/a, x/y/b
    TEST_CHECK(glob_match("a", "*"));
    TEST_CHECK(glob_match("b", "*"));
    TEST_CHECK(!glob_match("x/a", "*"));
    TEST_CHECK(!glob_match("x/y/b", "*"));

    // a/*/b matches a/x/b, a/y/b but not a/b, a/x/y/b
    TEST_CHECK(glob_match("a/x/b", "a/*/b"));
    TEST_CHECK(glob_match("a/y/b", "a/*/b"));
    TEST_CHECK(!glob_match("a/b", "a/*/b"));
    TEST_CHECK(!glob_match("a/x/y/b", "a/*/b"));

    TEST_CHECK(glob_match("", "*"));
    TEST_CHECK(glob_match("jdoe", "*"));
    TEST_CHECK(glob_match("jdoe", "*doe"));
    TEST_CHECK(glob_match("jdoe", "jdo*"));
    TEST_CHECK(glob_match("jdoe", "*jdoe"));
    TEST_CHECK(glob_match("jdoe", "jdoe*"));
    TEST_CHECK(glob_match("jdoe", "*o*"));
    TEST_CHECK(glob_match("/", "/*"));
    TEST_CHECK(glob_match("/", "*/"));
    TEST_CHECK(glob_match("/", "*/*"));
    TEST_CHECK(glob_match("/system", "/*"));
    TEST_CHECK(glob_match("/system/", "/*/"));
    TEST_CHECK(glob_match("/system/users", "/system/*"));
    TEST_CHECK(glob_match("/system/users", "/*/users"));
    TEST_CHECK(glob_match("/system/users", "/*/users"));
    TEST_CHECK(glob_match("/system/users", "/*/use*"));
    TEST_CHECK(glob_match("/system/users", "/*/*ers"));
    TEST_CHECK(glob_match("/system/users", "/*/*ser*"));
    TEST_CHECK(glob_match("/system/users", "/*/*"));
    TEST_CHECK(glob_match("/system/users/", "/*/*/"));
    TEST_CHECK(glob_match("/system/users/", "/*/*/*"));
    TEST_CHECK(glob_match("/system/users/", "/*/users/"));
    TEST_CHECK(glob_match("/system/users/jdoe/password", "/system/*/*o*/*"));

    TEST_CHECK(!glob_match("abcd", "*o*"));
    TEST_CHECK(!glob_match("system/", "*"));
    TEST_CHECK(!glob_match("system/users", "*"));
    TEST_CHECK(!glob_match("/", "*"));
    TEST_CHECK(!glob_match("/system", "*"));
    TEST_CHECK(!glob_match("/system/", "/*"));
    TEST_CHECK(!glob_match("/system/users", "/*"));
    TEST_CHECK(!glob_match("/system/groups", "/system/users"));
    TEST_CHECK(!glob_match("/system/groups", "/system/users"));
    TEST_CHECK(!glob_match("/system/users/jdoe/password", "/system/*/*w*/*"));
    TEST_CHECK(!glob_match("a", "/*"));
    TEST_CHECK(!glob_match("a", "*/"));

    // pattern '**': anything, included / (greedy match)
    // -------------------------------------------------

    TEST_CHECK(glob_match("", "**"));
    TEST_CHECK(glob_match("a", "**"));
    TEST_CHECK(glob_match("abc", "**"));
    TEST_CHECK(glob_match("abc", "a**"));
    TEST_CHECK(glob_match("jdoe", "*o**"));
    TEST_CHECK(glob_match("jdoe", "**"));
    TEST_CHECK(glob_match("/system/users/jdoe", "/system/**"));
    TEST_CHECK(glob_match("/system/users/jdoe", "/system/**/jdoe"));
    TEST_CHECK(glob_match("./file.png", "*/*.png"));
    TEST_CHECK(glob_match("abc/avalon/door/xyz", "abc/a**/xyz"));
    TEST_CHECK(!glob_match("xyz", "a**"));

    // **/a matches a, x/a, x/y/a but not b, x/b
    TEST_CHECK(glob_match("x/a", "**/a"));
    TEST_CHECK(glob_match("x/y/a", "**/a"));
    TEST_CHECK(!glob_match("b", "**/a"));
    TEST_CHECK(!glob_match("x/b", "**/a"));

    // a/**/b matches a/b, a/x/b, a/x/y/b but not x/a/b, a/b/x
    TEST_CHECK(glob_match("a/b", "a/**/b"));
    TEST_CHECK(glob_match("a/x/b", "a/**/b"));
    TEST_CHECK(glob_match("a/x/y/b", "a/**/b"));
    TEST_CHECK(glob_match("a/x/y/z/b", "a/**/b"));
    TEST_CHECK(glob_match("a/x/y/z/", "a/**/"));
    TEST_CHECK(!glob_match("x/a/b", "a/**/b"));
    TEST_CHECK(!glob_match("a/b/x", "a/**/b"));

    // a/** matches a/x, a/y, a/x/y but not a, b/x
    TEST_CHECK(glob_match("a/x", "a/**"));
    TEST_CHECK(glob_match("a/y", "a/**"));
    TEST_CHECK(glob_match("a/x/y", "a/**"));
    TEST_CHECK(!glob_match("a", "a/**"));
    TEST_CHECK(!glob_match("b/x", "a/**"));

    // greedy match (** followed by something distinct than / fails)
    TEST_CHECK(!glob_match("abc", "**c"));
    TEST_CHECK(!glob_match("abc", "**b**"));

    // bizarre case (this case should be rejected)
    TEST_CHECK(glob_match("a", "**/a"));

    // pattern '?: any one character except a /
    // -----------------------------------

    TEST_CHECK(glob_match("*", "?"));
    TEST_CHECK(glob_match("?", "?"));
    TEST_CHECK(glob_match("a", "?"));
    TEST_CHECK(glob_match("[", "?"));
    TEST_CHECK(glob_match("]", "?"));
    TEST_CHECK(glob_match("ab", "a?"));
    TEST_CHECK(glob_match("a?c", "a?c"));
    TEST_CHECK(glob_match("ab", "??"));
    TEST_CHECK(glob_match("abcd", "a??d"));
    TEST_CHECK(glob_match("abcd", "ab??"));
    TEST_CHECK(glob_match("abcd", "a?c?"));
    TEST_CHECK(glob_match("/a", "/?"));

    TEST_CHECK(!glob_match("", "?"));
    TEST_CHECK(!glob_match("/", "?"));
    TEST_CHECK(!glob_match("/a", "?a"));

    // a?b matches axb, ayb but not a, b, ab, a/b
    TEST_CHECK(glob_match("axb", "a?b"));
    TEST_CHECK(glob_match("ayb", "a?b"));
    TEST_CHECK(!glob_match("a", "a?b"));
    TEST_CHECK(!glob_match("b", "a?b"));
    TEST_CHECK(!glob_match("ab", "a?b"));
    TEST_CHECK(!glob_match("a/b", "a?b"));

    // case [a-z]: one character in the selected range of characters
    // -----------------------------------

    // a[xy]b matches axb, ayb but not a, b, azb
    TEST_CHECK(glob_match("axb", "a[xy]b"));
    TEST_CHECK(glob_match("ayb", "a[xy]b"));
    TEST_CHECK(!glob_match("a", "a[xy]b"));
    TEST_CHECK(!glob_match("b", "a[xy]b"));
    TEST_CHECK(!glob_match("azb", "a[xy]b"));

    // a[a-z]b matches aab, abb, acb, azb but not a, b, a3b, aAb, aZb
    TEST_CHECK(glob_match("aab", "a[a-z]b"));
    TEST_CHECK(glob_match("abb", "a[a-z]b"));
    TEST_CHECK(glob_match("acb", "a[a-z]b"));
    TEST_CHECK(glob_match("azb", "a[a-z]b"));
    TEST_CHECK(!glob_match("a", "a[a-z]b"));
    TEST_CHECK(!glob_match("b", "a[a-z]b"));
    TEST_CHECK(!glob_match("a3b", "a[a-z]b"));
    TEST_CHECK(!glob_match("aAb", "a[a-z]b"));
    TEST_CHECK(!glob_match("aZb", "a[a-z]b"));

    // a[^xy]b matches aab, abb, acb, azb but not a, b, axb, ayb
    TEST_CHECK(glob_match("aab", "a[^xy]b"));
    TEST_CHECK(glob_match("abb", "a[^xy]b"));
    TEST_CHECK(glob_match("acb", "a[^xy]b"));
    TEST_CHECK(glob_match("azb", "a[^xy]b"));
    TEST_CHECK(!glob_match("a", "a[^xy]b"));
    TEST_CHECK(!glob_match("b", "a[^xy]b"));
    TEST_CHECK(!glob_match("axb", "a[^xy]b"));
    TEST_CHECK(!glob_match("ayb", "a[^xy]b"));

    // a[^a-z]b matches a3b, aAb, aZb but not a, b, aab, abb, acb, azb
    TEST_CHECK(glob_match("a3b", "a[^a-z]b"));
    TEST_CHECK(glob_match("aAb", "a[^a-z]b"));
    TEST_CHECK(glob_match("aZb", "a[^a-z]b"));
    TEST_CHECK(!glob_match("a", "a[^a-z]b"));
    TEST_CHECK(!glob_match("b", "a[^a-z]b"));
    TEST_CHECK(!glob_match("aab", "a[^a-z]b"));
    TEST_CHECK(!glob_match("abb", "a[^a-z]b"));
    TEST_CHECK(!glob_match("acb", "a[^a-z]b"));
    TEST_CHECK(!glob_match("azb", "a[^a-z]b"));

    // case \ (backslash): any character specified after the backslash
    // -----------------------------------

    // a\?b matches a?b but not a, b, ab, axb, a/b
    TEST_CHECK(glob_match("a?b", "a\\?b"));
    TEST_CHECK(!glob_match("a", "a\\?b"));
    TEST_CHECK(!glob_match("b", "a\\?b"));
    TEST_CHECK(!glob_match("ab", "a\\?b"));
    TEST_CHECK(!glob_match("axb", "a\\?b"));
    TEST_CHECK(!glob_match("a/b", "a\\?b"));

    // other cases
    TEST_CHECK(glob_match("a\\b", "a\\\\b"));
    TEST_CHECK(!glob_match("axb", "a\\\\b"));
    TEST_CHECK(glob_match("axb", "a\\xb"));
    TEST_CHECK(!glob_match("awb", "a\\xb"));
}

static void test_is_valid_pattern(void)
{
    char pattern[1024] = {0};

    // valid patterns
    TEST_CHECK(is_valid_pattern("a"));
    TEST_CHECK(is_valid_pattern("1"));
    TEST_CHECK(is_valid_pattern("*"));
    TEST_CHECK(is_valid_pattern("~"));
    TEST_CHECK(is_valid_pattern("[1]"));
    TEST_CHECK(is_valid_pattern("key"));
    TEST_CHECK(is_valid_pattern("key[1]"));

    // NULL pattern
    TEST_CHECK(!is_valid_pattern(NULL));

    // empty pattern
    TEST_CHECK(!is_valid_pattern(""));

    // pattern too long
    for (size_t i = 0; i < sizeof(pattern) - 1; i++)
        pattern[i] = 'x';

    TEST_CHECK(!is_valid_pattern(pattern));

    // pattern with non-printable chars
    pattern[0] = 'a';
    pattern[1] = 13;
    pattern[2] = 'b';
    pattern[3] = 0;
    TEST_CHECK(!is_valid_pattern(pattern));
}

static void test_sizeof(void)
{
    TEST_CHECK(sizeof(crud_t) == 1);
    TEST_CHECK(sizeof(permission_t) == 2 * 8);
}

static void test_permissions(void)
{
    permissions_t perms = {0};

    TEST_CHECK(perms.length == 0);
    TEST_CHECK(perms.capacity == 0);
    TEST_CHECK(perms.data == 0);

    TEST_CHECK(!permissions_append(NULL, "perm", (crud_t){0}));
    TEST_CHECK(!permissions_append(&perms, NULL, (crud_t){0}));
    TEST_CHECK(!permissions_append(&perms, "", (crud_t){0}));

    for (size_t i = 0; i < sizeof(list)/sizeof(list[0]); i++) 
    {
        TEST_CHECK(permissions_append(&perms, list[i].pattern, list[i].crud));
        TEST_CHECK(perms.length == i + 1);
        TEST_CHECK(perms.capacity >= i + 1);
        TEST_CHECK(perms.data);
        TEST_CHECK(*((uint8_t *) &perms.data[i].crud) == *((uint8_t *) &list[i].crud));
        TEST_CHECK(perms.data[i].pattern);
        TEST_CHECK(strcmp(perms.data[i].pattern, list[i].pattern) == 0);
    }

    for (size_t i = 0; i < sizeof(list)/sizeof(list[0]); i++)
        TEST_CHECK(permissions_contains(&perms, perms.data[i].pattern));

    TEST_CHECK(!permissions_contains(&perms, ""));
    TEST_CHECK(!permissions_contains(&perms, "unknow/permission"));
    TEST_CHECK(!permissions_contains(&perms, "tracks/t002/value"));

    TEST_CHECK(permissions_check(&perms, "").num == 0);
    TEST_CHECK(permissions_check(&perms, "xxx").num == 0);
    TEST_CHECK(permissions_check(&perms, "energy").num == 0);
    TEST_CHECK(permissions_check(&perms, "energy/ss043/watts").num == 6);
    TEST_CHECK(permissions_check(&perms, "alarms/00042/level").num == 15);
    TEST_CHECK(permissions_check(&perms, "tracks/t001/value").num == 6);
    TEST_CHECK(permissions_check(&perms, "tracks/t001/units").num == 2);

    permissions_free(&perms);

    TEST_CHECK(perms.length == 0);
    TEST_CHECK(perms.capacity == 0);
    TEST_CHECK(perms.data == 0);
}

TEST_LIST = {
    { "match()",                      test_match },
    { "sizeof()",                     test_sizeof },
    { "is_valid_pattern()",           test_is_valid_pattern },
    { "permissions()",                test_permissions },
    { NULL, NULL }
};
