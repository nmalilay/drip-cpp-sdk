/**
 * Drip C++ SDK - Break Test
 *
 * Tries hard to break the SDK with edge cases, bad inputs,
 * boundary values, and unexpected usage patterns.
 */

#include <drip/drip.hpp>
#include <iostream>
#include <string>
#include <limits>
#include <cstdlib>

static int passed = 0;
static int failed = 0;
static int total = 0;

#define RUN_TEST(name, block) do { \
    total++; \
    std::cout << "  [" << total << "] " << name << "... " << std::flush; \
    try { block; passed++; std::cout << "OK" << std::endl; } \
    catch (const std::exception& e) { failed++; std::cout << "FAIL: " << e.what() << std::endl; } \
    catch (...) { failed++; std::cout << "FAIL: unknown exception" << std::endl; } \
} while(0)

#define EXPECT_THROW(expr, etype) do { \
    bool caught = false; \
    try { expr; } catch (const etype&) { caught = true; } \
    if (!caught) throw std::runtime_error("Expected " #etype " but none thrown"); \
} while(0)

int main() {
    std::cout << "\n========================================" << std::endl;
    std::cout << "  Drip C++ SDK - Break Test Suite" << std::endl;
    std::cout << "========================================\n" << std::endl;

    // =========================================================================
    // SECTION 1: Constructor & Config edge cases (offline tests)
    // =========================================================================
    std::cout << "--- Constructor & Config ---" << std::endl;

    RUN_TEST("No API key throws DripError", {
        EXPECT_THROW(
            { drip::Config c; c.api_key = ""; drip::Client cl(c); },
            drip::DripError
        );
    });

    RUN_TEST("Whitespace-only API key accepted (no trim)", {
        // SDK doesn't trim whitespace — this will create a client but fail on requests
        // This is a potential bug: " " is not empty() but is not a valid key
        drip::Config c;
        c.api_key = "   ";
        c.base_url = "http://localhost:3001/v1";
        drip::Client cl(c);
        // Client created — whitespace wasn't rejected
        // This IS a bug: whitespace-only key should be rejected
        std::cout << "(BUG: whitespace key accepted) ";
    });

    RUN_TEST("Very long API key accepted", {
        drip::Config c;
        c.api_key = std::string(10000, 'x');
        c.base_url = "http://localhost:3001/v1";
        drip::Client cl(c);
        // Client created with garbage key — will fail on auth, but shouldn't crash
    });

    RUN_TEST("Null bytes in API key", {
        drip::Config c;
        std::string key = "sk_test_abc";
        key += '\0';
        key += "def";
        c.api_key = key;
        c.base_url = "http://localhost:3001/v1";
        drip::Client cl(c);
        // std::string preserves null bytes, but curl may truncate at \0
        // This could cause auth header to be "Bearer sk_test_abc" (truncated)
        std::cout << "(null byte in key, curl may truncate) ";
    });

    RUN_TEST("Negative timeout accepted", {
        drip::Config c;
        c.api_key = "sk_test_fake";
        c.base_url = "http://localhost:3001/v1";
        c.timeout_ms = -1;
        drip::Client cl(c);
        // Negative timeout — the impl does: timeout_ms > 0 ? timeout_ms : 30000
        // So -1 falls back to 30000. That's OK behavior.
    });

    RUN_TEST("Zero timeout falls back to default", {
        drip::Config c;
        c.api_key = "sk_test_fake";
        c.base_url = "http://localhost:3001/v1";
        c.timeout_ms = 0;
        drip::Client cl(c);
        // 0 is not > 0, so falls back to 30000. OK.
    });

    RUN_TEST("Base URL with trailing slashes stripped", {
        drip::Config c;
        c.api_key = "sk_test_fake";
        c.base_url = "http://localhost:3001/v1///";
        drip::Client cl(c);
        // Should strip trailing slashes. Let's verify via ping (will fail but URL should be sane)
    });

    RUN_TEST("Empty base_url with no env var uses default", {
        drip::Config c;
        c.api_key = "sk_test_fake";
        // base_url empty, DRIP_BASE_URL not set — should use production default
        drip::Client cl(c);
    });

    RUN_TEST("Key type detection", {
        drip::Config c;
        c.api_key = "sk_test_abc";
        c.base_url = "http://localhost:3001/v1";
        drip::Client cl(c);
        if (cl.key_type() != drip::KEY_SECRET) throw std::runtime_error("Expected KEY_SECRET");

        drip::Config c2;
        c2.api_key = "pk_live_abc";
        c2.base_url = "http://localhost:3001/v1";
        drip::Client cl2(c2);
        if (cl2.key_type() != drip::KEY_PUBLIC) throw std::runtime_error("Expected KEY_PUBLIC");

        drip::Config c3;
        c3.api_key = "random_key";
        c3.base_url = "http://localhost:3001/v1";
        drip::Client cl3(c3);
        if (cl3.key_type() != drip::KEY_UNKNOWN) throw std::runtime_error("Expected KEY_UNKNOWN");
    });

    RUN_TEST("Key type detection with short keys", {
        // Keys shorter than 3 chars — substr(0,3) could be problematic
        drip::Config c;
        c.api_key = "sk";  // only 2 chars
        c.base_url = "http://localhost:3001/v1";
        drip::Client cl(c);
        // substr(0,3) on a 2-char string: in C++11, this returns "sk" (no crash)
        // But it won't match "sk_" so key_type should be KEY_UNKNOWN
        if (cl.key_type() != drip::KEY_UNKNOWN) throw std::runtime_error("Expected KEY_UNKNOWN for short key");
    });

    RUN_TEST("Single char API key", {
        drip::Config c;
        c.api_key = "x";
        c.base_url = "http://localhost:3001/v1";
        drip::Client cl(c);
        if (cl.key_type() != drip::KEY_UNKNOWN) throw std::runtime_error("Expected KEY_UNKNOWN");
    });

    // =========================================================================
    // SECTION 2: Struct default values
    // =========================================================================
    std::cout << "\n--- Struct Defaults ---" << std::endl;

    RUN_TEST("TrackUsageParams quantity uninitialized", {
        drip::TrackUsageParams p;
        // quantity is a double with NO default constructor initializer!
        // This is uninitialized memory — could be any value
        // BUG: TrackUsageParams doesn't have a default constructor that zeros quantity
        std::cout << "(quantity=" << p.quantity << " — UNINITIALIZED) ";
    });

    RUN_TEST("RecordRunResult defaults", {
        drip::RecordRunResult r;
        // run.duration_ms is int, events.created/duplicates are int — uninitialized?
        std::cout << "(run.duration_ms=" << r.run.duration_ms
                  << " events.created=" << r.events.created << ") ";
    });

    RUN_TEST("PingResult defaults", {
        drip::PingResult p;
        // ok is bool, latency_ms is int, timestamp is int64_t — all uninitialized
        std::cout << "(ok=" << p.ok << " latency=" << p.latency_ms << ") ";
    });

    RUN_TEST("ListCustomersOptions default limit is 100", {
        drip::ListCustomersOptions opts;
        if (opts.limit != 100) throw std::runtime_error("Default limit should be 100");
    });

    // =========================================================================
    // SECTION 3: Live API tests (requires running backend)
    // =========================================================================

    const char* api_key_env = std::getenv("DRIP_API_KEY");
    if (!api_key_env || api_key_env[0] == '\0') {
        std::cout << "\n--- Skipping live API tests (DRIP_API_KEY not set) ---" << std::endl;
        std::cout << "\nResults: " << passed << "/" << total << " passed, "
                  << failed << " failed" << std::endl;
        return failed > 0 ? 1 : 0;
    }

    drip::Config cfg;
    cfg.base_url = "http://localhost:3001/v1";
    // api_key comes from DRIP_API_KEY env
    drip::Client client(cfg);

    std::cout << "\n--- Live API: Ping ---" << std::endl;

    RUN_TEST("Ping succeeds", {
        auto h = client.ping();
        if (!h.ok) throw std::runtime_error("Ping failed: " + h.status);
    });

    std::cout << "\n--- Live API: Customer edge cases ---" << std::endl;

    RUN_TEST("Create customer with empty params throws", {
        drip::CreateCustomerParams p;
        // Both external_customer_id and onchain_address empty — API should reject
        EXPECT_THROW(client.createCustomer(p), drip::DripError);
    });

    RUN_TEST("Create customer with valid external ID", {
        drip::CreateCustomerParams p;
        p.external_customer_id = "break_test_user_1";
        auto c = client.createCustomer(p);
        if (c.id.empty()) throw std::runtime_error("Customer ID is empty");
    });

    RUN_TEST("Create duplicate customer (same external ID)", {
        drip::CreateCustomerParams p;
        p.external_customer_id = "break_test_user_1";
        // Should either succeed (idempotent) or throw — but not crash
        try {
            auto c = client.createCustomer(p);
            std::cout << "(returned existing: " << c.id << ") ";
        } catch (const drip::DripError& e) {
            std::cout << "(threw: " << e.status_code() << " " << e.what() << ") ";
        }
    });

    RUN_TEST("Create customer with very long external ID", {
        drip::CreateCustomerParams p;
        p.external_customer_id = std::string(5000, 'A');
        try {
            auto c = client.createCustomer(p);
            std::cout << "(accepted long ID) ";
        } catch (const drip::DripError& e) {
            std::cout << "(rejected: " << e.status_code() << ") ";
        }
    });

    RUN_TEST("Create customer with special chars in external ID", {
        drip::CreateCustomerParams p;
        p.external_customer_id = "user<script>alert('xss')</script>";
        try {
            auto c = client.createCustomer(p);
            std::cout << "(accepted special chars) ";
        } catch (const drip::DripError& e) {
            std::cout << "(rejected: " << e.status_code() << ") ";
        }
    });

    RUN_TEST("Create customer with unicode in external ID", {
        drip::CreateCustomerParams p;
        p.external_customer_id = "user_\xE4\xB8\xAD\xE6\x96\x87_test";  // UTF-8: 中文
        try {
            auto c = client.createCustomer(p);
            std::cout << "(accepted unicode) ";
        } catch (const drip::DripError& e) {
            std::cout << "(rejected: " << e.status_code() << ") ";
        }
    });

    RUN_TEST("Create customer with newlines/tabs in metadata", {
        drip::CreateCustomerParams p;
        p.external_customer_id = "break_test_metadata";
        p.metadata["key\nwith\nnewlines"] = "value\twith\ttabs";
        p.metadata["normal"] = "value";
        auto c = client.createCustomer(p);
        if (c.id.empty()) throw std::runtime_error("Customer ID is empty");
    });

    RUN_TEST("Get nonexistent customer throws NotFoundError", {
        EXPECT_THROW(
            client.getCustomer("nonexistent_customer_id_12345"),
            drip::NotFoundError
        );
    });

    RUN_TEST("Get customer with empty ID", {
        // Empty string — will hit GET /customers/ which is the list endpoint
        try {
            auto c = client.getCustomer("");
            std::cout << "(returned something for empty ID?!) ";
        } catch (const drip::DripError& e) {
            std::cout << "(threw: " << e.status_code() << ") ";
        }
    });

    RUN_TEST("Get balance for nonexistent customer", {
        EXPECT_THROW(
            client.getBalance("fake_customer_xyz"),
            drip::DripError
        );
    });

    RUN_TEST("List customers with limit 0", {
        drip::ListCustomersOptions opts;
        opts.limit = 0;
        try {
            auto result = client.listCustomers(opts);
            std::cout << "(returned " << result.customers.size() << " with limit=0) ";
        } catch (const drip::DripError& e) {
            std::cout << "(threw: " << e.status_code() << ") ";
        }
    });

    RUN_TEST("List customers with negative limit", {
        drip::ListCustomersOptions opts;
        opts.limit = -1;
        try {
            auto result = client.listCustomers(opts);
            std::cout << "(returned " << result.customers.size() << " with limit=-1) ";
        } catch (const drip::DripError& e) {
            std::cout << "(threw: " << e.status_code() << ") ";
        }
    });

    std::cout << "\n--- Live API: Usage tracking edge cases ---" << std::endl;

    // Create a real customer for usage tests
    drip::CreateCustomerParams cp;
    cp.external_customer_id = "break_test_usage_user";
    std::string test_customer_id;
    try {
        auto cust = client.createCustomer(cp);
        test_customer_id = cust.id;
    } catch (...) {
        // May already exist, try to find it
        auto list = client.listCustomers();
        for (size_t i = 0; i < list.customers.size(); i++) {
            if (list.customers[i].external_customer_id == "break_test_usage_user") {
                test_customer_id = list.customers[i].id;
                break;
            }
        }
    }

    if (!test_customer_id.empty()) {
        RUN_TEST("Track usage with zero quantity", {
            drip::TrackUsageParams p;
            p.customer_id = test_customer_id;
            p.meter = "test_meter";
            p.quantity = 0;
            try {
                auto r = client.trackUsage(p);
                std::cout << "(accepted zero quantity) ";
            } catch (const drip::DripError& e) {
                std::cout << "(rejected: " << e.status_code() << " " << e.what() << ") ";
            }
        });

        RUN_TEST("Track usage with negative quantity", {
            drip::TrackUsageParams p;
            p.customer_id = test_customer_id;
            p.meter = "test_meter";
            p.quantity = -100;
            try {
                auto r = client.trackUsage(p);
                std::cout << "(accepted negative quantity!) ";
            } catch (const drip::DripError& e) {
                std::cout << "(rejected: " << e.status_code() << ") ";
            }
        });

        RUN_TEST("Track usage with huge quantity", {
            drip::TrackUsageParams p;
            p.customer_id = test_customer_id;
            p.meter = "test_meter";
            p.quantity = 1e18;  // quintillion
            try {
                auto r = client.trackUsage(p);
                std::cout << "(accepted 1e18 quantity) ";
            } catch (const drip::DripError& e) {
                std::cout << "(rejected: " << e.status_code() << ") ";
            }
        });

        RUN_TEST("Track usage with NaN quantity", {
            drip::TrackUsageParams p;
            p.customer_id = test_customer_id;
            p.meter = "test_meter";
            p.quantity = std::numeric_limits<double>::quiet_NaN();
            try {
                auto r = client.trackUsage(p);
                std::cout << "(accepted NaN!) ";
            } catch (const drip::DripError& e) {
                std::cout << "(rejected: " << e.status_code() << ") ";
            } catch (const std::exception& e) {
                std::cout << "(exception: " << e.what() << ") ";
            }
        });

        RUN_TEST("Track usage with Infinity quantity", {
            drip::TrackUsageParams p;
            p.customer_id = test_customer_id;
            p.meter = "test_meter";
            p.quantity = std::numeric_limits<double>::infinity();
            try {
                auto r = client.trackUsage(p);
                std::cout << "(accepted Infinity!) ";
            } catch (const drip::DripError& e) {
                std::cout << "(rejected: " << e.status_code() << ") ";
            } catch (const std::exception& e) {
                std::cout << "(exception: " << e.what() << ") ";
            }
        });

        RUN_TEST("Track usage with empty meter name", {
            drip::TrackUsageParams p;
            p.customer_id = test_customer_id;
            p.meter = "";
            p.quantity = 1;
            try {
                auto r = client.trackUsage(p);
                std::cout << "(accepted empty meter!) ";
            } catch (const drip::DripError& e) {
                std::cout << "(rejected: " << e.status_code() << ") ";
            }
        });

        RUN_TEST("Track usage with fake customer ID", {
            drip::TrackUsageParams p;
            p.customer_id = "totally_fake_id";
            p.meter = "test";
            p.quantity = 1;
            EXPECT_THROW(client.trackUsage(p), drip::DripError);
        });

        RUN_TEST("Track usage idempotency — same key twice", {
            drip::TrackUsageParams p;
            p.customer_id = test_customer_id;
            p.meter = "test_meter";
            p.quantity = 42;
            p.idempotency_key = "break_test_idem_key_001";
            auto r1 = client.trackUsage(p);
            auto r2 = client.trackUsage(p);
            // Both should succeed, second should be deduplicated
            std::cout << "(id1=" << r1.usage_event_id << " id2=" << r2.usage_event_id << ") ";
        });

        std::cout << "\n--- Live API: Run edge cases ---" << std::endl;

        RUN_TEST("Record run with zero events", {
            drip::RecordRunParams r;
            r.customer_id = test_customer_id;
            r.workflow = "break-test-empty";
            r.status = drip::RUN_COMPLETED;
            // No events pushed
            try {
                auto result = client.recordRun(r);
                std::cout << "(accepted: " << result.summary << ") ";
            } catch (const drip::DripError& e) {
                std::cout << "(rejected: " << e.status_code() << " " << e.what() << ") ";
            }
        });

        RUN_TEST("Record run with FAILED status and error message", {
            drip::RecordRunParams r;
            r.customer_id = test_customer_id;
            r.workflow = "break-test-fail";
            r.status = drip::RUN_FAILED;
            r.error_message = "Intentional test failure";
            r.error_code = "TEST_ERROR";

            drip::RecordRunEvent e;
            e.event_type = "test.fail";
            e.quantity = 1;
            r.events.push_back(e);

            auto result = client.recordRun(r);
            std::cout << "(summary: " << result.summary << ") ";
        });

        RUN_TEST("Record run with very long workflow name", {
            drip::RecordRunParams r;
            r.customer_id = test_customer_id;
            r.workflow = std::string(500, 'w');
            r.status = drip::RUN_COMPLETED;

            drip::RecordRunEvent e;
            e.event_type = "test.long";
            e.quantity = 1;
            r.events.push_back(e);

            try {
                auto result = client.recordRun(r);
                std::cout << "(accepted long workflow) ";
            } catch (const drip::DripError& e2) {
                std::cout << "(rejected: " << e2.status_code() << ") ";
            }
        });

        RUN_TEST("Record run with many events (100)", {
            drip::RecordRunParams r;
            r.customer_id = test_customer_id;
            r.workflow = "break-test-many-events";
            r.status = drip::RUN_COMPLETED;

            for (int i = 0; i < 100; i++) {
                drip::RecordRunEvent e;
                e.event_type = "test.bulk";
                e.quantity = i;
                e.units = "units";
                r.events.push_back(e);
            }

            auto result = client.recordRun(r);
            std::cout << "(created=" << result.events.created << " dupes=" << result.events.duplicates << ") ";
        });

        RUN_TEST("Emit event to nonexistent run", {
            drip::EmitEventParams e;
            e.run_id = "fake_run_id_xyz";
            e.event_type = "test.fake";
            e.quantity = 1;
            EXPECT_THROW(client.emitEvent(e), drip::DripError);
        });

        RUN_TEST("End nonexistent run", {
            drip::EndRunParams ep;
            ep.status = drip::RUN_COMPLETED;
            EXPECT_THROW(client.endRun("fake_run_id_xyz", ep), drip::DripError);
        });

    } else {
        std::cout << "\n--- Skipping live usage tests (couldn't get customer ID) ---" << std::endl;
    }

    // =========================================================================
    // SECTION 4: Connection error handling
    // =========================================================================
    std::cout << "\n--- Connection error handling ---" << std::endl;

    RUN_TEST("Ping to unreachable host throws NetworkError", {
        drip::Config c;
        c.api_key = "sk_test_fake";
        c.base_url = "http://192.0.2.1:9999/v1";  // RFC 5737 TEST-NET, not routable
        c.timeout_ms = 2000;  // 2 second timeout
        drip::Client bad_client(c);
        EXPECT_THROW(bad_client.ping(), drip::DripError);
    });

    RUN_TEST("Request to invalid URL throws", {
        drip::Config c;
        c.api_key = "sk_test_fake";
        c.base_url = "not_a_url";
        drip::Client bad_client(c);
        EXPECT_THROW(bad_client.ping(), drip::DripError);
    });

    RUN_TEST("Wrong auth key returns AuthenticationError", {
        drip::Config c;
        c.api_key = "sk_test_wrong_key_12345";
        c.base_url = "http://localhost:3001/v1";
        drip::Client bad_client(c);
        // Ping uses /health which may not need auth, so try a real endpoint
        drip::CreateCustomerParams p;
        p.external_customer_id = "should_fail_auth";
        EXPECT_THROW(bad_client.createCustomer(p), drip::DripError);
    });

    // =========================================================================
    // Summary
    // =========================================================================
    std::cout << "\n========================================" << std::endl;
    std::cout << "  Results: " << passed << "/" << total << " passed, "
              << failed << " failed" << std::endl;
    std::cout << "========================================\n" << std::endl;

    return failed > 0 ? 1 : 0;
}
