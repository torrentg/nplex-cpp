#include <cstdlib>
#include <cstring>
#include <getopt.h>
#include <iostream>
#include <stop_token>
#include <string>
#include <thread>
#include "nplex-cpp/client.hpp"
#include "logger.hpp"

using namespace nplex;

// ---------------------------------------------------------------------------
// Types
// ---------------------------------------------------------------------------

struct config_t
{
    std::string user;
    std::string password;
    std::string servers;
};

// ---------------------------------------------------------------------------
// Functions
// ---------------------------------------------------------------------------

static void print_help(const char *prog)
{
    std::cout
        << "Description:\n"
        << "  Functional tests for nplex isolation levels.\n"
        << "\n"
        << "Usage: " << prog << " [options]\n"
        << "\n"
        << "Options:\n"
        << "  -h, --help               Show this help and exit\n"
        << "  -u, --user USER          User identifier (mandatory)\n"
        << "  -p, --password PWD       User password (mandatory)\n"
        << "  -s, --servers LIST       Comma-separated server list (mandatory)\n";
}

static bool parse_args(int argc, char *argv[], config_t &cfg)
{
    static struct option long_opts[] = {
        {"help",     no_argument,       nullptr, 'h'},
        {"user",     required_argument, nullptr, 'u'},
        {"password", required_argument, nullptr, 'p'},
        {"servers",  required_argument, nullptr, 's'},
        {nullptr, 0, nullptr, 0}
    };

    int opt = 0;
    while ((opt = getopt_long(argc, argv, "hu:p:s:", long_opts, nullptr)) != -1)
    {
        switch (opt)
        {
            case 'h': print_help(argv[0]); return false;
            case 'u': cfg.user     = optarg; break;
            case 'p': cfg.password = optarg; break;
            case 's': cfg.servers  = optarg; break;
            default:  print_help(argv[0]); return false;
        }
    }

    if (cfg.user.empty() || cfg.password.empty() || cfg.servers.empty()) {
        std::cerr << "Error: --user, --password, and --servers are mandatory.\n\n";
        print_help(argv[0]);
        return false;
    }

    return true;
}

static void clean_db(const client_ptr &cli)
{
    auto tx = cli->create_tx(transaction::isolation_e::READ_COMMITTED, false);

    tx->remove("**");

    auto rc = tx->submit(true).get();

    if (rc != transaction::submit_e::COMMITTED && rc != transaction::submit_e::NO_MODIFICATIONS)
        throw std::runtime_error("clean_db: submit failed");
}

/// Insert key=value using a forced read-committed transaction and commit.
static void external_upsert(const client_ptr &cli, const char *key, const std::string_view &value)
{
    auto tx = cli->create_tx(transaction::isolation_e::READ_COMMITTED, false);

    tx->upsert(key, value);

    auto rc = tx->submit(true).get();

    if (rc != transaction::submit_e::COMMITTED)
        throw std::runtime_error(std::string("external_upsert: submit failed for key=") + key);
}

static void external_remove(const client_ptr &cli, const char *key)
{
    auto tx = cli->create_tx(transaction::isolation_e::READ_COMMITTED, false);

    tx->remove(key);

    auto rc = tx->submit(true).get();

    if (rc != transaction::submit_e::COMMITTED && rc != transaction::submit_e::NO_MODIFICATIONS)
        throw std::runtime_error(std::string("external_remove: submit failed for key=") + key);
}

// ---------------------------------------------------------------------------
// Test infrastructure
// ---------------------------------------------------------------------------

static int g_tests_passed = 0;
static int g_tests_failed = 0;
static int g_tests_aux = 0;

#define CHECK(expr)                                                   \
    do {                                                              \
        if (expr) {                                                   \
            g_tests_passed++;                                         \
        } else {                                                      \
            g_tests_failed++;                                         \
            std::cerr << "  FAIL[" << __LINE__ << "]" << std::endl;   \
        }                                                             \
    } while (false)

#define TEST_STARTS(name)                                             \
    std::cout << "  " << (name) << "..." << std::flush;               \
    g_tests_aux = g_tests_failed

#define TEST_FINISH()                                                 \
    if (g_tests_aux == g_tests_failed)                                \
        std::cout << " OK" << std::endl

// ---------------------------------------------------------------------------
// READ_COMMITTED tests
// ---------------------------------------------------------------------------

static void test_rc_read_sees_commits(const client_ptr &cli)
{
    TEST_STARTS("rc_read_sees_commits");
    clean_db(cli);

    auto tx1 = cli->create_tx(transaction::isolation_e::READ_COMMITTED, true);

    // key does not exist yet
    auto val = tx1->read("test/x");
    CHECK(val == nullptr);

    // external commit: insert test/x=1
    external_upsert(cli, "test/x", "1");

    val = tx1->read("test/x");
    CHECK(val != nullptr && val->data() == "1");

    // external commit: update test/x=2
    external_upsert(cli, "test/x", "2");

    val = tx1->read("test/x");
    CHECK(val != nullptr && val->data() == "2");

    tx1->discard();
    TEST_FINISH();
}

static void test_rc_own_writes_visible(const client_ptr &cli)
{
    TEST_STARTS("rc_own_writes_visible");
    clean_db(cli);

    auto tx1 = cli->create_tx(transaction::isolation_e::READ_COMMITTED, false);

    tx1->upsert("test/x", "99");

    // external commit inserting a different value
    external_upsert(cli, "test/x", "3");

    auto val = tx1->read("test/x");
    CHECK(val != nullptr && val->data() == "99");

    tx1->discard();
    TEST_FINISH();
}

static void test_rc_read_after_external_delete(const client_ptr &cli)
{
    TEST_STARTS("rc_read_after_external_delete");
    clean_db(cli);

    external_upsert(cli, "test/x", "10");

    auto tx1 = cli->create_tx(transaction::isolation_e::READ_COMMITTED, true);
    auto val = tx1->read("test/x");
    CHECK(val != nullptr && val->data() == "10");

    external_remove(cli, "test/x");

    val = tx1->read("test/x");
    CHECK(val == nullptr);

    tx1->discard();
    TEST_FINISH();
}

static void test_rc_read_or_default(const client_ptr &cli)
{
    TEST_STARTS("rc_read_or_default");
    clean_db(cli);

    auto tx1 = cli->create_tx(transaction::isolation_e::READ_COMMITTED, true);

    auto val = tx1->read_or("test/missing", "default_val");
    CHECK(val != nullptr && val->data() == "default_val");

    tx1->discard();
    TEST_FINISH();
}

static void test_rc_full_scenario(const client_ptr &cli)
{
    TEST_STARTS("rc_full_scenario");
    clean_db(cli);

    auto tx1 = cli->create_tx(transaction::isolation_e::READ_COMMITTED, false);

    // verify test/x is null
    auto val = tx1->read("test/x");
    CHECK(val == nullptr);

    // tx0 inserts test/x=1
    external_upsert(cli, "test/x", "1");
    val = tx1->read("test/x");
    CHECK(val != nullptr && val->data() == "1");

    // tx0 updates test/x=2
    external_upsert(cli, "test/x", "2");
    val = tx1->read("test/x");
    CHECK(val != nullptr && val->data() == "2");

    // tx1 upserts test/x=99
    tx1->upsert("test/x", "99");

    // tx0 updates test/x=3
    external_upsert(cli, "test/x", "3");

    // tx1 reads test/x -> should see own write (99)
    val = tx1->read("test/x");
    CHECK(val != nullptr && val->data() == "99");

    // tx1 delete test/x
    tx1->remove("test/x");
    val = tx1->read("test/x");
    CHECK(val == nullptr);

     // tx0 updates test/x=4
    external_upsert(cli, "test/x", "4");
    val = tx1->read("test/x");
    CHECK(val == nullptr);

    tx1->discard();
    TEST_FINISH();
}

// ---------------------------------------------------------------------------
// REPEATABLE_READ tests
// ---------------------------------------------------------------------------

static void test_rr_repeatable_reads(const client_ptr &cli)
{
    TEST_STARTS("rr_repeatable_reads");
    clean_db(cli);

    external_upsert(cli, "test/x", "1");

    auto tx1 = cli->create_tx(transaction::isolation_e::REPEATABLE_READ, true);

    auto val = tx1->read("test/x");
    CHECK(val != nullptr && val->data() == "1");

    // external commit: update test/x=2
    external_upsert(cli, "test/x", "2");

    val = tx1->read("test/x");
    CHECK(val != nullptr && val->data() == "1");

    tx1->discard();
    TEST_FINISH();
}

static void test_rr_new_key_reads_latest(const client_ptr &cli)
{
    TEST_STARTS("rr_new_key_reads_latest");
    clean_db(cli);

    auto tx1 = cli->create_tx(transaction::isolation_e::REPEATABLE_READ, true);

    // external commit before first read of test/y
    external_upsert(cli, "test/y", "10");

    auto val = tx1->read("test/y");
    CHECK(val != nullptr && val->data() == "10");

    // external update of test/y
    external_upsert(cli, "test/y", "20");

    val = tx1->read("test/y");
    CHECK(val != nullptr && val->data() == "10");

    tx1->discard();
    TEST_FINISH();
}

static void test_rr_read_after_external_delete(const client_ptr &cli)
{
    TEST_STARTS("rr_read_after_external_delete");
    clean_db(cli);

    external_upsert(cli, "test/x", "5");

    auto tx1 = cli->create_tx(transaction::isolation_e::REPEATABLE_READ, true);

    auto val = tx1->read("test/x");
    CHECK(val != nullptr && val->data() == "5");

    external_remove(cli, "test/x");

    val = tx1->read("test/x");
    CHECK(val != nullptr && val->data() == "5");

    tx1->discard();
    TEST_FINISH();
}

static void test_rr_own_writes(const client_ptr &cli)
{
    TEST_STARTS("rr_own_writes");
    clean_db(cli);

    external_upsert(cli, "test/x", "1");

    auto tx1 = cli->create_tx(transaction::isolation_e::REPEATABLE_READ, false);

    auto val = tx1->read("test/x");
    CHECK(val != nullptr && val->data() == "1");

    tx1->upsert("test/x", "50");

    val = tx1->read("test/x");
    CHECK(val != nullptr && val->data() == "50");

    external_upsert(cli, "test/x", "2");

    val = tx1->read("test/x");
    CHECK(val != nullptr && val->data() == "50");

    tx1->discard();
    TEST_FINISH();
}

static void test_rr_full_scenario(const client_ptr &cli)
{
    TEST_STARTS("rr_full_scenario");
    clean_db(cli);

    auto tx1 = cli->create_tx(transaction::isolation_e::REPEATABLE_READ, false);

    // test/x doesn't exist
    auto val = tx1->read("test/x");
    CHECK(val == nullptr);

    // tx0 inserts test/x=1
    external_upsert(cli, "test/x", "1");

    // test/x was already read (as null) -> repeatable read keeps null
    val = tx1->read("test/x");
    CHECK(val == nullptr);

    tx1->discard();

    // New tx1 to read test/x=1
    tx1 = cli->create_tx(transaction::isolation_e::REPEATABLE_READ, false);

    val = tx1->read("test/x");
    CHECK(val != nullptr && val->data() == "1");

    // tx0 updates test/x=2
    external_upsert(cli, "test/x", "2");

    val = tx1->read("test/x");
    CHECK(val != nullptr && val->data() == "1");

    // tx1 upserts test/x=99
    tx1->upsert("test/x", "99");

    // tx0 updates test/x=3
    external_upsert(cli, "test/x", "3");

    val = tx1->read("test/x");
    CHECK(val != nullptr && val->data() == "99");

    tx1->discard();
    TEST_FINISH();
}

// ---------------------------------------------------------------------------
// SERIALIZABLE tests
// ---------------------------------------------------------------------------

static void test_ser_snapshot_read(const client_ptr &cli)
{
    TEST_STARTS("ser_snapshot_read");
    clean_db(cli);

    external_upsert(cli, "test/x", "1");
    external_upsert(cli, "test/y", "2");

    auto tx1 = cli->create_tx(transaction::isolation_e::SERIALIZABLE, true);

    // external commit: update both keys
    external_upsert(cli, "test/x", "10");
    external_upsert(cli, "test/y", "20");
    external_upsert(cli, "test/z", "30");

    CHECK(tx1->is_dirty() == false);

    auto val_x = tx1->read("test/x");
    auto val_y = tx1->read("test/y");
    auto val_z = tx1->read("test/z");
    CHECK(val_x != nullptr && val_x->data() == "1");
    CHECK(val_y != nullptr && val_y->data() == "2");
    CHECK(val_z == nullptr);

    CHECK(tx1->is_dirty() == true);

    auto rc = tx1->submit(false).get();
    CHECK(rc == transaction::submit_e::NO_MODIFICATIONS);

    tx1->discard();
    TEST_FINISH();
}

static void test_ser_phantom_prevention(const client_ptr &cli)
{
    TEST_STARTS("ser_phantom_prevention");
    clean_db(cli);

    auto tx1 = cli->create_tx(transaction::isolation_e::SERIALIZABLE, true);

    // external insert after tx creation
    external_upsert(cli, "test/phantom", "ghost");

    auto val = tx1->read("test/phantom");
    CHECK(val == nullptr);

    tx1->discard();
    TEST_FINISH();
}

static void test_ser_delete_not_visible(const client_ptr &cli)
{
    TEST_STARTS("ser_delete_not_visible");
    clean_db(cli);

    external_upsert(cli, "test/x", "alive");

    auto tx1 = cli->create_tx(transaction::isolation_e::SERIALIZABLE, true);

    external_remove(cli, "test/x");

    auto val = tx1->read("test/x");
    CHECK(val != nullptr && val->data() == "alive");

    tx1->discard();
    TEST_FINISH();
}

static void test_ser_own_writes(const client_ptr &cli)
{
    TEST_STARTS("ser_own_writes");
    clean_db(cli);

    external_upsert(cli, "test/x", "1");

    auto tx1 = cli->create_tx(transaction::isolation_e::SERIALIZABLE, false);

    auto val = tx1->read("test/x");
    CHECK(val != nullptr && val->data() == "1");

    tx1->upsert("test/x", "42");

    external_upsert(cli, "test/x", "999");

    val = tx1->read("test/x");
    CHECK(val != nullptr && val->data() == "42");

    tx1->discard();
    TEST_FINISH();
}

static void test_ser_full_scenario(const client_ptr &cli)
{
    TEST_STARTS("ser_full_scenario");
    clean_db(cli);

    auto tx1 = cli->create_tx(transaction::isolation_e::SERIALIZABLE, false);

    // test/x doesn't exist at tx creation
    auto val = tx1->read("test/x");
    CHECK(val == nullptr);

    // tx0 inserts test/x=1 (after tx1 creation -> not visible)
    external_upsert(cli, "test/x", "1");

    val = tx1->read("test/x");
    CHECK(val == nullptr);

    // tx1 upserts test/x=99
    tx1->upsert("test/x", "99");

    // tx0 updates test/x=3
    external_upsert(cli, "test/x", "3");

    val = tx1->read("test/x");
    CHECK(val != nullptr && val->data() == "99");

    tx1->discard();
    TEST_FINISH();
}

static void test_ser_rev_consistent(const client_ptr &cli)
{
    TEST_STARTS("ser_rev_consistent");
    clean_db(cli);

    auto tx0 = cli->create_tx(transaction::isolation_e::READ_COMMITTED, true);
    auto tx1 = cli->create_tx(transaction::isolation_e::SERIALIZABLE, true);

    CHECK(tx0->rev() == tx1->rev());

    external_upsert(cli, "test/x", "1");

    // After an external commit, READ_COMMITTED rev advances but SERIALIZABLE does not
    auto rev0_after = tx0->rev();
    auto rev1_after = tx1->rev();
    CHECK(rev0_after > rev1_after);

    tx0->discard();
    tx1->discard();
    TEST_FINISH();
}

// ---------------------------------------------------------------------------
// DIRTY flag tests
// ---------------------------------------------------------------------------

static void test_rc_not_dirty_on_read_key(const client_ptr &cli)
{
    TEST_STARTS("rc_not_dirty_on_read_key");
    clean_db(cli);

    external_upsert(cli, "test/x", "1");

    auto tx1 = cli->create_tx(transaction::isolation_e::READ_COMMITTED, false);
    tx1->read("test/x");
    CHECK(!tx1->is_dirty());

    external_upsert(cli, "test/x", "2");

    CHECK(!tx1->is_dirty());

    auto rc = tx1->submit(false).get();
    CHECK(rc == transaction::submit_e::NO_MODIFICATIONS);
    TEST_FINISH();
}

static void test_rc_not_dirty_unrelated_key(const client_ptr &cli)
{
    TEST_STARTS("rc_not_dirty_unrelated_key");
    clean_db(cli);

    external_upsert(cli, "test/x", "1");

    auto tx1 = cli->create_tx(transaction::isolation_e::READ_COMMITTED, false);
    tx1->read("test/x");

    external_upsert(cli, "test/other", "99");

    CHECK(!tx1->is_dirty());

    tx1->discard();
    TEST_FINISH();
}

static void test_rr_dirty_on_read_key(const client_ptr &cli)
{
    TEST_STARTS("rr_dirty_on_read_key");
    clean_db(cli);

    external_upsert(cli, "test/x", "1");

    auto tx1 = cli->create_tx(transaction::isolation_e::REPEATABLE_READ, false);
    tx1->read("test/x");
    CHECK(!tx1->is_dirty());

    external_upsert(cli, "test/x", "2");

    CHECK(tx1->is_dirty());

    auto rc = tx1->submit(false).get();
    CHECK(rc == transaction::submit_e::NO_MODIFICATIONS);
    TEST_FINISH();
}

static void test_rr_not_dirty_unrelated_key(const client_ptr &cli)
{
    TEST_STARTS("rr_not_dirty_unrelated_key");
    clean_db(cli);

    external_upsert(cli, "test/x", "1");

    auto tx1 = cli->create_tx(transaction::isolation_e::REPEATABLE_READ, false);
    tx1->read("test/x");

    external_upsert(cli, "test/other", "99");

    CHECK(!tx1->is_dirty());

    tx1->discard();
    TEST_FINISH();
}

static void test_ser_dirty_on_snapshot_key(const client_ptr &cli)
{
    TEST_STARTS("ser_dirty_on_snapshot_key");
    clean_db(cli);

    external_upsert(cli, "test/x", "1");

    auto tx1 = cli->create_tx(transaction::isolation_e::SERIALIZABLE, false);
    tx1->read("test/x");
    CHECK(!tx1->is_dirty());

    external_upsert(cli, "test/x", "2");

    CHECK(tx1->is_dirty());

    auto rc = tx1->submit(false).get();
    CHECK(rc == transaction::submit_e::NO_MODIFICATIONS);
    TEST_FINISH();
}

static void test_ser_dirty_phantom_key(const client_ptr &cli)
{
    TEST_STARTS("ser_dirty_phantom_key");
    clean_db(cli);

    auto tx1 = cli->create_tx(transaction::isolation_e::SERIALIZABLE, false);

    // External creates a new key -> adds phantom entry to tx snapshot
    external_upsert(cli, "test/new", "ghost");

    // Phantom was tracked but tx is not dirty yet (first occurrence)
    auto val = tx1->read("test/new");
    CHECK(val == nullptr);
    CHECK(!tx1->is_dirty());

    // Second external commit on same key -> now it IS dirty
    external_upsert(cli, "test/new", "ghost2");

    CHECK(tx1->is_dirty());

    tx1->discard();
    TEST_FINISH();
}

static void test_dirty_forced_submit(const client_ptr &cli)
{
    TEST_STARTS("dirty_forced_submit");
    clean_db(cli);

    external_upsert(cli, "test/x", "1");

    auto tx1 = cli->create_tx(transaction::isolation_e::READ_COMMITTED, false);
    tx1->read("test/x");
    tx1->upsert("test/x", "forced");

    external_upsert(cli, "test/x", "2");

    CHECK(tx1->is_dirty());

    auto rc = tx1->submit(true).get();  // force=true
    CHECK(rc == transaction::submit_e::COMMITTED);
    TEST_FINISH();
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int main(int argc, char *argv[])
{
    config_t cfg;

    if (!parse_args(argc, argv, cfg))
        return EXIT_FAILURE;

    try {
        // Configure connection
        nplex::params_t params = {
            .servers  = cfg.servers,
            .user     = cfg.user,
            .password = cfg.password
        };

        auto cli = nplex::client::create(params);

        // Attach logger
        //auto log = std::make_shared<::logger>(nplex::logger::log_level_e::WARN);
        //cli->set_logger(log);

        // Start event loop
        std::jthread worker([cli](std::stop_token st) {
            cli->run(std::move(st));
        });

        cli->wait_for_synced();

        // Run tests
        std::cout << "READ_COMMITTED tests" << std::endl;
        test_rc_read_sees_commits(cli);
        test_rc_own_writes_visible(cli);
        test_rc_read_after_external_delete(cli);
        test_rc_read_or_default(cli);
        test_rc_full_scenario(cli);

        std::cout << "REPEATABLE_READ tests" << std::endl;
        test_rr_repeatable_reads(cli);
        test_rr_new_key_reads_latest(cli);
        test_rr_read_after_external_delete(cli);
        test_rr_own_writes(cli);
        test_rr_full_scenario(cli);

        std::cout << "SERIALIZABLE tests" << std::endl;
        test_ser_snapshot_read(cli);
        test_ser_phantom_prevention(cli);
        test_ser_delete_not_visible(cli);
        test_ser_own_writes(cli);
        test_ser_full_scenario(cli);
        test_ser_rev_consistent(cli);

        std::cout << "DIRTY flag tests" << std::endl;
        test_rc_not_dirty_on_read_key(cli);
        test_rc_not_dirty_unrelated_key(cli);
        test_rr_dirty_on_read_key(cli);
        test_rr_not_dirty_unrelated_key(cli);
        test_ser_dirty_on_snapshot_key(cli);
        test_ser_dirty_phantom_key(cli);
        test_dirty_forced_submit(cli);

        // Clean up
        clean_db(cli);

        // Summary
        std::cout << "\n"
                  << "Results: " << g_tests_passed << " passed, "
                  << g_tests_failed << " failed, "
                  << (g_tests_passed + g_tests_failed) << " total"
                  << std::endl;

        worker.request_stop();

        return (g_tests_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
    }
    catch (const std::exception &e) {
        std::cerr << "Error: " << e.what() << '\n';
        return EXIT_FAILURE;
    }
}
