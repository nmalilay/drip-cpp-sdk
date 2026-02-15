/**
 * Drip C++ SDK - Local Demo
 *
 * Creates a customer, tracks usage, and records a run against localhost.
 *
 * Build:
 *   cd sdk-cpp && make examples
 *   DRIP_API_KEY="sk_test_..." DRIP_BASE_URL="http://localhost:3001/v1" ./build/demo
 */

#include <drip/drip.hpp>
#include <iostream>
#include <string>

int main() {
    try {
        drip::Client client;

        std::cout << "\n[1] Pinging API..." << std::endl;
        auto health = client.ping();
        std::cout << "    Status: " << (health.ok ? "healthy" : "unhealthy") << std::endl;
        std::cout << "    Latency: " << health.latency_ms << "ms" << std::endl;

        if (!health.ok) {
            std::cerr << "API not reachable. Is the backend running?" << std::endl;
            return 1;
        }

        std::cout << "\n[2] Creating customer..." << std::endl;
        drip::CreateCustomerParams cparams;
        cparams.external_customer_id = "cpp_demo_user";
        auto customer = client.createCustomer(cparams);
        std::cout << "    Customer ID: " << customer.id << std::endl;
        std::cout << "    External ID: cpp_demo_user" << std::endl;

        std::cout << "\n[3] Tracking usage (50 API calls)..." << std::endl;
        drip::TrackUsageParams usage;
        usage.customer_id = customer.id;
        usage.meter = "api_calls";
        usage.quantity = 50;
        usage.units = "calls";
        usage.description = "Batch of 50 API calls from C++ client";
        usage.metadata["source"] = "cpp_sdk";
        usage.metadata["version"] = DRIP_SDK_VERSION;

        auto usage_result = client.trackUsage(usage);
        std::cout << "    Event ID: " << usage_result.usage_event_id << std::endl;

        std::cout << "\n[4] Tracking usage (10000 tokens)..." << std::endl;
        drip::TrackUsageParams token_usage;
        token_usage.customer_id = customer.id;
        token_usage.meter = "tokens";
        token_usage.quantity = 10000;
        token_usage.units = "tokens";
        token_usage.metadata["model"] = "gpt-4";

        auto token_result = client.trackUsage(token_usage);
        std::cout << "    Event ID: " << token_result.usage_event_id << std::endl;

        drip::RecordRunParams run;
        run.customer_id = customer.id;
        run.workflow = "cpp-inference";
        run.status = drip::RUN_COMPLETED;
        run.metadata["language"] = "cpp";

        drip::RecordRunEvent inference_event;
        inference_event.event_type = "inference.complete";
        inference_event.quantity = 1;
        inference_event.units = "requests";
        inference_event.description = "Inference request processed";
        run.events.push_back(inference_event);

        drip::RecordRunEvent token_event;
        token_event.event_type = "inference.tokens";
        token_event.quantity = 2048;
        token_event.units = "tokens";
        run.events.push_back(token_event);

        auto run_result = client.recordRun(run);
        std::cout << "    Run ID: " << run_result.run.id << std::endl;
        std::cout << "    Summary: " << run_result.summary << std::endl;
        std::cout << "    Events: " << run_result.events.created << std::endl;

        // 6. Get customer balance
        std::cout << "\n[6] Checking customer balance..." << std::endl;
        auto balance = client.getBalance(customer.id);
        std::cout << "    Balance: $" << balance.balance_usdc << std::endl;

        std::cout << "\n=== Demo complete! ===" << std::endl;
        return 0;

    } catch (const drip::AuthenticationError& e) {
        std::cerr << "Auth error: " << e.what() << std::endl;
        std::cerr << "Set DRIP_API_KEY=sk_test_..." << std::endl;
        return 1;
    } catch (const drip::DripError& e) {
        std::cerr << "Drip error [" << e.status_code() << "]: " << e.what() << std::endl;
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}
