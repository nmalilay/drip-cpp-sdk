/**
 * Drip C++ SDK - Minimal Test
 *
 * Usage:
 *   1. Put your API key in .env:  DRIP_API_KEY=sk_test_...
 *   2. mkdir build && cd build && cmake .. && make
 *   3. ./drip_test
 */

#include <drip/drip.hpp>
#include <iostream>
#include <fstream>
#include <cstdlib>

// Load KEY=VALUE pairs from a .env file into environment variables
void load_dotenv(const std::string& path) {
    std::ifstream file(path);
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);
        // Strip surrounding quotes if present
        if (val.size() >= 2 && (val[0] == '"' || val[0] == '\'')) {
            val = val.substr(1, val.size() - 2);
        }
        setenv(key.c_str(), val.c_str(), 0); // 0 = don't overwrite existing
    }
}

int main() {
    // Load .env from the project root (one level up from build/)
    load_dotenv("../.env");
    load_dotenv(".env");
    std::cout << "Drip C++ SDK v" << DRIP_SDK_VERSION << std::endl;
    std::cout << "==========================================\n" << std::endl;

    try {
        // 1. Initialize client (reads DRIP_API_KEY from env)
        drip::Client client;
        std::cout << "[1/4] Client initialized" << std::endl;

        // 2. Ping the API
        drip::PingResult health = client.ping();
        if (health.ok) {
            std::cout << "[2/4] Ping OK (" << health.latency_ms << "ms)" << std::endl;
        } else {
            std::cerr << "[2/4] Ping FAILED" << std::endl;
            return 1;
        }

        // 3. Create a customer
        drip::CreateCustomerParams cparams;
        cparams.external_customer_id = "cpp_sdk_test_user";
        drip::CustomerResult customer = client.createCustomer(cparams);
        std::cout << "[3/4] Customer created: " << customer.id << std::endl;

        // 4. Track a usage event
        drip::TrackUsageParams usage;
        usage.customer_id = customer.id;
        usage.meter = "sdk_test";
        usage.quantity = 1;
        usage.units = "tests";
        usage.description = "C++ SDK test event";

        drip::TrackUsageResult result = client.trackUsage(usage);
        if (result.success) {
            std::cout << "[4/4] Usage tracked (event: " << result.usage_event_id << ")" << std::endl;
        } else {
            std::cerr << "[4/4] Usage tracking FAILED" << std::endl;
            return 1;
        }

        std::cout << "\n==========================================" << std::endl;
        std::cout << "All checks passed! SDK is working." << std::endl;
        return 0;

    } catch (const drip::AuthenticationError& e) {
        std::cerr << "AUTH ERROR: " << e.what() << std::endl;
        std::cerr << "Make sure DRIP_API_KEY is set." << std::endl;
        return 1;
    } catch (const drip::DripError& e) {
        std::cerr << "API ERROR [" << e.status_code() << "]: " << e.what() << std::endl;
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "ERROR: " << e.what() << std::endl;
        return 1;
    }
}
