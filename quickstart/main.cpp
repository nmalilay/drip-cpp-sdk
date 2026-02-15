#include <drip/drip.hpp>
#include <iostream>
#include <fstream>
#include <cstdlib>

// Load .env file into environment variables
void load_dotenv(const std::string& path) {
    std::ifstream file(path);
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);
        if (val.size() >= 2 && (val[0] == '"' || val[0] == '\''))
            val = val.substr(1, val.size() - 2);
        setenv(key.c_str(), val.c_str(), 0);
    }
}

int main() {
    load_dotenv("../.env");
    load_dotenv(".env");

    try {
        drip::Client client;
        std::cout << "Connected to Drip API" << std::endl;

        // Ping
        drip::PingResult health = client.ping();
        std::cout << "Ping: " << (health.ok ? "OK" : "FAIL")
                  << " (" << health.latency_ms << "ms)" << std::endl;

        // Create a customer
        drip::CreateCustomerParams cparams;
        cparams.external_customer_id = "quickstart_user";
        drip::CustomerResult customer = client.createCustomer(cparams);
        std::cout << "Customer: " << customer.id << std::endl;

        // Track usage
        drip::TrackUsageParams usage;
        usage.customer_id = customer.id;
        usage.meter = "api_calls";
        usage.quantity = 1;
        drip::TrackUsageResult result = client.trackUsage(usage);
        std::cout << "Usage tracked: " << result.usage_event_id << std::endl;

        std::cout << "Done! SDK is working." << std::endl;

    } catch (const drip::DripError& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}
