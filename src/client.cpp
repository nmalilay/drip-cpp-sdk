#include "drip/client.hpp"

#include <picojson/picojson.h>
#include <curl/curl.h>

#include <sstream>
#include <cstdlib>
#include <ctime>
#include <cstring>

/* C++03: use sys/time.h for gettimeofday instead of std::chrono */
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/time.h>
#endif

namespace drip {

/* Convenience typedefs for picojson */
typedef picojson::value  JsonVal;
typedef picojson::object JsonObj;
typedef picojson::array  JsonArr;

// =============================================================================
// Helpers
// =============================================================================

static const char* DEFAULT_BASE_URL = "https://drip-app-hlunj.ondigitalocean.app/v1";

/**
 * Get current time in milliseconds (C++03 compatible).
 */
static long long now_ms() {
#ifdef _WIN32
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    long long t = (long long)ft.dwHighDateTime << 32 | ft.dwLowDateTime;
    return t / 10000LL - 11644473600000LL;
#else
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (long long)tv.tv_sec * 1000LL + (long long)tv.tv_usec / 1000LL;
#endif
}

/**
 * Generate a deterministic idempotency key from components.
 */
static std::string make_idempotency_key(
    const std::string& prefix,
    const std::string& a,
    const std::string& b,
    double c
) {
    std::ostringstream oss;
    oss << prefix << ":" << a << ":" << b << ":" << c;

    std::string input = oss.str();
    unsigned long hash = 5381;
    for (size_t i = 0; i < input.size(); ++i) {
        hash = ((hash << 5) + hash) + static_cast<unsigned char>(input[i]);
    }

    std::ostringstream hex;
    hex << prefix << "_" << std::hex << hash;
    return hex.str();
}

static std::string make_idempotency_key_int(
    const std::string& prefix,
    const std::string& a,
    const std::string& b,
    int c
) {
    return make_idempotency_key(prefix, a, b, static_cast<double>(c));
}

/**
 * Get environment variable or fallback.
 */
static std::string env_or(const char* name, const std::string& fallback) {
    const char* val = std::getenv(name);
    if (val && val[0] != '\0') {
        return std::string(val);
    }
    return fallback;
}

// =============================================================================
// picojson helpers — build & read JSON safely
// =============================================================================

/** Create a JSON string value. */
static JsonVal jstr(const std::string& s) {
    return JsonVal(s);
}

/** Create a JSON number value. */
static JsonVal jnum(double n) {
    return JsonVal(n);
}

/** Create a JSON bool value. */
static JsonVal jbool(bool b) {
    return JsonVal(b);
}

/** Convert Metadata map to a JSON object value. */
static JsonVal metadata_to_json(const Metadata& m) {
    JsonObj obj;
    for (Metadata::const_iterator it = m.begin(); it != m.end(); ++it) {
        obj[it->first] = jstr(it->second);
    }
    return JsonVal(obj);
}

/** Parse Metadata from a JSON object value. */
static Metadata metadata_from_json(const JsonVal& v) {
    Metadata m;
    if (v.is<JsonObj>()) {
        const JsonObj& obj = v.get<JsonObj>();
        for (JsonObj::const_iterator it = obj.begin(); it != obj.end(); ++it) {
            if (it->second.is<std::string>()) {
                m[it->first] = it->second.get<std::string>();
            } else {
                m[it->first] = it->second.serialize();
            }
        }
    }
    return m;
}

/** Safely get a string from a JSON object. */
static std::string json_string(const JsonObj& obj, const char* key) {
    JsonObj::const_iterator it = obj.find(key);
    if (it != obj.end() && it->second.is<std::string>()) {
        return it->second.get<std::string>();
    }
    return "";
}

static int json_int(const JsonObj& obj, const char* key, int def = 0) {
    JsonObj::const_iterator it = obj.find(key);
    if (it != obj.end() && it->second.is<double>()) {
        return static_cast<int>(it->second.get<double>());
    }
    return def;
}

static double json_double(const JsonObj& obj, const char* key, double def = 0.0) {
    JsonObj::const_iterator it = obj.find(key);
    if (it != obj.end() && it->second.is<double>()) {
        return it->second.get<double>();
    }
    return def;
}

static bool json_bool(const JsonObj& obj, const char* key, bool def = false) {
    JsonObj::const_iterator it = obj.find(key);
    if (it != obj.end() && it->second.is<bool>()) {
        return it->second.get<bool>();
    }
    return def;
}

/** Check if a key exists in a JSON object. */
static bool json_has(const JsonObj& obj, const char* key) {
    return obj.find(key) != obj.end();
}

/** Get a nested array. Returns empty array if missing. */
static JsonArr json_arr(const JsonObj& obj, const char* key) {
    JsonObj::const_iterator it = obj.find(key);
    if (it != obj.end() && it->second.is<JsonArr>()) {
        return it->second.get<JsonArr>();
    }
    return JsonArr();
}

// =============================================================================
// CURL write callback
// =============================================================================

static size_t curl_write_cb(char* ptr, size_t size, size_t nmemb, void* userdata) {
    std::string* buf = static_cast<std::string*>(userdata);
    size_t total = size * nmemb;
    buf->append(ptr, total);
    return total;
}

// =============================================================================
// Client::Impl (PIMPL)
// =============================================================================

struct Client::Impl {
    std::string api_key;
    std::string base_url;
    int timeout_ms;
    KeyType key_type;

    Impl(const Config& config) {
        /* Resolve API key */
        api_key = config.api_key;
        if (api_key.empty()) {
            api_key = env_or("DRIP_API_KEY", "");
        }
        if (api_key.empty()) {
            throw DripError(
                "Drip API key is required. Either pass config.api_key or set DRIP_API_KEY.",
                0, "NO_API_KEY"
            );
        }

        /* Resolve base URL */
        base_url = config.base_url;
        if (base_url.empty()) {
            base_url = env_or("DRIP_BASE_URL", DEFAULT_BASE_URL);
        }
        while (!base_url.empty() && base_url[base_url.size() - 1] == '/') {
            base_url.erase(base_url.size() - 1);
        }

        timeout_ms = config.timeout_ms > 0 ? config.timeout_ms : 30000;

        /* Detect key type */
        if (api_key.size() >= 3 && api_key.substr(0, 3) == "sk_") {
            key_type = KEY_SECRET;
        } else if (api_key.size() >= 3 && api_key.substr(0, 3) == "pk_") {
            key_type = KEY_PUBLIC;
        } else {
            key_type = KEY_UNKNOWN;
        }
    }

    /**
     * Make an HTTP request. Returns parsed JSON object.
     */
    JsonObj request(const std::string& method, const std::string& path, const JsonObj& body) {
        CURL* curl = curl_easy_init();
        if (!curl) {
            throw NetworkError("Failed to initialize CURL");
        }

        std::string url = base_url + path;
        std::string response_buf;
        long http_code = 0;

        struct curl_slist* headers = NULL;
        headers = curl_slist_append(headers, "Content-Type: application/json");
        std::string auth_header = "Authorization: Bearer " + api_key;
        headers = curl_slist_append(headers, auth_header.c_str());

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_buf);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, static_cast<long>(timeout_ms));
        curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

        std::string body_str;
        if (method == "POST" || method == "PATCH") {
            body_str = JsonVal(body).serialize();
            if (method == "PATCH") {
                curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PATCH");
            } else {
                curl_easy_setopt(curl, CURLOPT_POST, 1L);
            }
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body_str.c_str());
            curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body_str.size()));
        } else {
            curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
        }

        CURLcode res = curl_easy_perform(curl);
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);

        if (res == CURLE_OPERATION_TIMEDOUT) {
            throw TimeoutError("Request timed out");
        }
        if (res != CURLE_OK) {
            throw NetworkError(std::string("CURL error: ") + curl_easy_strerror(res));
        }

        /* 204 No Content */
        if (http_code == 204) {
            JsonObj result;
            result["success"] = jbool(true);
            return result;
        }

        /* Parse response */
        JsonVal parsed;
        std::string parse_err = picojson::parse(parsed, response_buf);
        if (!parse_err.empty()) {
            throw DripError(
                std::string("Failed to parse API response: ") + parse_err,
                static_cast<int>(http_code),
                "PARSE_ERROR"
            );
        }

        if (!parsed.is<JsonObj>()) {
            throw DripError(
                "API response is not a JSON object",
                static_cast<int>(http_code),
                "PARSE_ERROR"
            );
        }

        JsonObj data = parsed.get<JsonObj>();

        /* Check for HTTP errors */
        if (http_code < 200 || http_code >= 300) {
            std::string msg = json_string(data, "message");
            if (msg.empty()) {
                msg = json_string(data, "error");
            }
            if (msg.empty()) {
                std::ostringstream oss;
                oss << "Request failed with status " << http_code;
                msg = oss.str();
            }
            std::string code = json_string(data, "code");

            if (http_code == 401) throw AuthenticationError(msg);
            if (http_code == 404) throw NotFoundError(msg);
            if (http_code == 429) throw RateLimitError(msg);
            throw DripError(msg, static_cast<int>(http_code), code);
        }

        return data;
    }

    /* Overload for GET (no body) */
    JsonObj request(const std::string& method, const std::string& path) {
        return request(method, path, JsonObj());
    }

    JsonObj get(const std::string& path) {
        return request("GET", path);
    }

    JsonObj post(const std::string& path, const JsonObj& body) {
        return request("POST", path, body);
    }

    JsonObj patch(const std::string& path, const JsonObj& body) {
        return request("PATCH", path, body);
    }
};

// =============================================================================
// Client construction / destruction
// =============================================================================

Client::Client(const Config& config)
    : impl_(new Impl(config))
{}

Client::~Client() {
    delete impl_;
}

KeyType Client::key_type() const {
    return impl_->key_type;
}

// =============================================================================
// createCustomer()
// =============================================================================

static CustomerResult parse_customer(const JsonObj& data) {
    CustomerResult r;
    r.id = json_string(data, "id");
    r.external_customer_id = json_string(data, "externalCustomerId");
    r.onchain_address = json_string(data, "onchainAddress");
    r.status = json_string(data, "status");
    r.is_internal = json_bool(data, "isInternal", false);
    if (json_has(data, "metadata")) {
        JsonObj::const_iterator it = data.find("metadata");
        if (it != data.end()) {
            r.metadata = metadata_from_json(it->second);
        }
    }
    r.created_at = json_string(data, "createdAt");
    r.updated_at = json_string(data, "updatedAt");
    return r;
}

CustomerResult Client::createCustomer(const CreateCustomerParams& params) {
    JsonObj body;
    if (!params.external_customer_id.empty()) body["externalCustomerId"] = jstr(params.external_customer_id);
    if (!params.onchain_address.empty()) body["onchainAddress"] = jstr(params.onchain_address);
    if (!params.metadata.empty()) body["metadata"] = metadata_to_json(params.metadata);

    return parse_customer(impl_->post("/customers", body));
}

// =============================================================================
// getCustomer()
// =============================================================================

CustomerResult Client::getCustomer(const std::string& customer_id) {
    return parse_customer(impl_->get("/customers/" + customer_id));
}

// =============================================================================
// listCustomers()
// =============================================================================

ListCustomersResult Client::listCustomers(const ListCustomersOptions& options) {
    std::ostringstream path;
    path << "/customers?limit=" << options.limit;
    if (!options.status.empty()) path << "&status=" << options.status;

    JsonObj data = impl_->get(path.str());

    ListCustomersResult result;
    result.total = json_int(data, "count", 0);

    JsonArr arr = json_arr(data, "data");
    for (size_t i = 0; i < arr.size(); ++i) {
        if (arr[i].is<JsonObj>()) {
            result.customers.push_back(parse_customer(arr[i].get<JsonObj>()));
        }
    }
    return result;
}

// =============================================================================
// getBalance()
// =============================================================================

BalanceResult Client::getBalance(const std::string& customer_id) {
    JsonObj data = impl_->get("/customers/" + customer_id + "/balance");

    BalanceResult r;
    r.customer_id = json_string(data, "customerId");
    r.balance_usdc = json_string(data, "balanceUsdc");
    return r;
}

// =============================================================================
// ping()
// =============================================================================

PingResult Client::ping() {
    std::string health_url = impl_->base_url;

    std::string suffix = "/v1";
    if (health_url.size() >= suffix.size() &&
        health_url.compare(health_url.size() - suffix.size(), suffix.size(), suffix) == 0) {
        health_url.erase(health_url.size() - suffix.size());
    }

    long long start = now_ms();

    std::string saved_base = impl_->base_url;
    impl_->base_url = health_url;

    PingResult result;
    try {
        JsonObj data = impl_->get("/health");
        long long end = now_ms();

        result.latency_ms = static_cast<int>(end - start);
        result.status = json_string(data, "status");
        if (result.status.empty()) result.status = "healthy";
        result.ok = (result.status == "healthy");
        result.timestamp = static_cast<int64_t>(
            json_double(data, "timestamp", static_cast<double>(std::time(NULL) * 1000))
        );
    } catch (...) {
        impl_->base_url = saved_base;
        throw;
    }

    impl_->base_url = saved_base;
    return result;
}

// =============================================================================
// trackUsage()
// =============================================================================

TrackUsageResult Client::trackUsage(const TrackUsageParams& params) {
    std::string idem_key = params.idempotency_key;
    if (idem_key.empty()) {
        idem_key = make_idempotency_key("track", params.customer_id, params.meter, params.quantity);
    }

    JsonObj body;
    body["customerId"] = jstr(params.customer_id);
    body["usageType"] = jstr(params.meter);
    body["quantity"] = jnum(params.quantity);
    body["idempotencyKey"] = jstr(idem_key);

    if (!params.units.empty()) body["units"] = jstr(params.units);
    if (!params.description.empty()) body["description"] = jstr(params.description);
    if (!params.metadata.empty()) body["metadata"] = metadata_to_json(params.metadata);

    JsonObj data = impl_->post("/usage/internal", body);

    TrackUsageResult r;
    r.success = json_bool(data, "success", true);
    r.usage_event_id = json_string(data, "usageEventId");
    r.customer_id = json_string(data, "customerId");
    r.usage_type = json_string(data, "usageType");
    r.quantity = json_double(data, "quantity", params.quantity);
    r.is_internal = json_bool(data, "isInternal", false);
    r.message = json_string(data, "message");
    return r;
}

// =============================================================================
// Run methods
// =============================================================================

RunResult Client::startRun(const StartRunParams& params) {
    JsonObj body;
    body["customerId"] = jstr(params.customer_id);
    body["workflowId"] = jstr(params.workflow_id);

    if (!params.external_run_id.empty()) body["externalRunId"] = jstr(params.external_run_id);
    if (!params.correlation_id.empty()) body["correlationId"] = jstr(params.correlation_id);
    if (!params.parent_run_id.empty()) body["parentRunId"] = jstr(params.parent_run_id);
    if (!params.metadata.empty()) body["metadata"] = metadata_to_json(params.metadata);

    JsonObj data = impl_->post("/runs", body);

    RunResult r;
    r.id = json_string(data, "id");
    r.customer_id = json_string(data, "customerId");
    r.workflow_id = json_string(data, "workflowId");
    r.workflow_name = json_string(data, "workflowName");
    r.status = run_status_from_string(json_string(data, "status"));
    r.correlation_id = json_string(data, "correlationId");
    r.created_at = json_string(data, "createdAt");
    return r;
}

EndRunResult Client::endRun(const std::string& run_id, const EndRunParams& params) {
    JsonObj body;
    body["status"] = jstr(run_status_to_string(params.status));

    if (!params.error_message.empty()) body["errorMessage"] = jstr(params.error_message);
    if (!params.error_code.empty()) body["errorCode"] = jstr(params.error_code);
    if (!params.metadata.empty()) body["metadata"] = metadata_to_json(params.metadata);

    JsonObj data = impl_->patch("/runs/" + run_id, body);

    EndRunResult r;
    r.id = json_string(data, "id");
    r.status = run_status_from_string(json_string(data, "status"));
    r.ended_at = json_string(data, "endedAt");
    r.duration_ms = json_int(data, "durationMs", 0);
    r.event_count = json_int(data, "eventCount", 0);
    r.total_cost_units = json_string(data, "totalCostUnits");
    return r;
}

EventResult Client::emitEvent(const EmitEventParams& params) {
    std::string idem_key = params.idempotency_key;
    if (idem_key.empty()) {
        idem_key = make_idempotency_key("evt", params.run_id, params.event_type, params.quantity);
    }

    JsonObj body;
    body["runId"] = jstr(params.run_id);
    body["eventType"] = jstr(params.event_type);
    body["idempotencyKey"] = jstr(idem_key);

    if (params.quantity != 0) body["quantity"] = jnum(params.quantity);
    if (!params.units.empty()) body["units"] = jstr(params.units);
    if (!params.description.empty()) body["description"] = jstr(params.description);
    if (params.cost_units != 0) body["costUnits"] = jnum(params.cost_units);
    if (!params.metadata.empty()) body["metadata"] = metadata_to_json(params.metadata);

    JsonObj data = impl_->post("/run-events", body);

    EventResult r;
    r.id = json_string(data, "id");
    r.run_id = json_string(data, "runId");
    r.event_type = json_string(data, "eventType");
    r.quantity = json_double(data, "quantity", 0);
    r.cost_units = json_double(data, "costUnits", 0);
    r.is_duplicate = json_bool(data, "isDuplicate", false);
    r.timestamp = json_string(data, "timestamp");
    return r;
}

// =============================================================================
// recordRun() - all-in-one
// =============================================================================

RecordRunResult Client::recordRun(const RecordRunParams& params) {
    long long start_time = now_ms();

    /* Step 1: Resolve workflow (find or create) */
    std::string workflow_id = params.workflow;
    std::string workflow_name = params.workflow;

    if (params.workflow.substr(0, 3) != "wf_") {
        try {
            JsonObj workflows = impl_->get("/workflows");
            bool found = false;

            JsonArr arr = json_arr(workflows, "data");
            for (size_t i = 0; i < arr.size(); ++i) {
                if (!arr[i].is<JsonObj>()) continue;
                JsonObj w = arr[i].get<JsonObj>();
                std::string slug = json_string(w, "slug");
                std::string id = json_string(w, "id");
                if (slug == params.workflow || id == params.workflow) {
                    workflow_id = id;
                    workflow_name = json_string(w, "name");
                    found = true;
                    break;
                }
            }

            if (!found) {
                std::string display_name = params.workflow;
                for (size_t i = 0; i < display_name.size(); ++i) {
                    if (display_name[i] == '_' || display_name[i] == '-') {
                        display_name[i] = ' ';
                    }
                }
                bool cap_next = true;
                for (size_t i = 0; i < display_name.size(); ++i) {
                    if (cap_next && display_name[i] >= 'a' && display_name[i] <= 'z') {
                        display_name[i] = display_name[i] - 'a' + 'A';
                    }
                    cap_next = (display_name[i] == ' ');
                }

                JsonObj create_body;
                create_body["name"] = jstr(display_name);
                create_body["slug"] = jstr(params.workflow);
                create_body["productSurface"] = jstr("CUSTOM");

                JsonObj created = impl_->post("/workflows", create_body);
                workflow_id = json_string(created, "id");
                workflow_name = json_string(created, "name");
            }
        } catch (...) {
            workflow_id = params.workflow;
        }
    }

    /* Step 2: Create the run */
    StartRunParams run_params;
    run_params.customer_id = params.customer_id;
    run_params.workflow_id = workflow_id;
    run_params.external_run_id = params.external_run_id;
    run_params.correlation_id = params.correlation_id;
    run_params.metadata = params.metadata;

    RunResult run = startRun(run_params);

    /* Step 3: Emit events in batch */
    int events_created = 0;
    int events_duplicates = 0;

    if (!params.events.empty()) {
        JsonArr batch_events;
        for (size_t i = 0; i < params.events.size(); ++i) {
            const RecordRunEvent& evt = params.events[i];
            JsonObj event_json;
            event_json["runId"] = jstr(run.id);
            event_json["eventType"] = jstr(evt.event_type);

            if (evt.quantity != 0) event_json["quantity"] = jnum(evt.quantity);
            if (!evt.units.empty()) event_json["units"] = jstr(evt.units);
            if (!evt.description.empty()) event_json["description"] = jstr(evt.description);
            if (evt.cost_units != 0) event_json["costUnits"] = jnum(evt.cost_units);
            if (!evt.metadata.empty()) event_json["metadata"] = metadata_to_json(evt.metadata);

            if (!params.external_run_id.empty()) {
                std::ostringstream key;
                key << params.external_run_id << ":" << evt.event_type << ":" << i;
                event_json["idempotencyKey"] = jstr(key.str());
            } else {
                event_json["idempotencyKey"] = jstr(make_idempotency_key_int(
                    "run", run.id, evt.event_type, static_cast<int>(i)
                ));
            }

            batch_events.push_back(JsonVal(event_json));
        }

        JsonObj batch_body;
        batch_body["events"] = JsonVal(batch_events);

        JsonObj batch_result = impl_->post("/run-events/batch", batch_body);
        events_created = json_int(batch_result, "created", 0);
        events_duplicates = json_int(batch_result, "duplicates", 0);
    }

    /* Step 4: End the run */
    EndRunParams end_params;
    end_params.status = params.status;
    end_params.error_message = params.error_message;
    end_params.error_code = params.error_code;

    EndRunResult end_result = endRun(run.id, end_params);

    long long end_time = now_ms();
    int total_ms = static_cast<int>(end_time - start_time);

    int display_ms = (end_result.duration_ms > 0) ? end_result.duration_ms : total_ms;
    std::string status_icon;
    if (params.status == RUN_COMPLETED) status_icon = "[OK]";
    else if (params.status == RUN_FAILED) status_icon = "[FAIL]";
    else status_icon = "[--]";

    std::ostringstream summary;
    summary << status_icon << " " << workflow_name << ": "
            << events_created << " events recorded (" << display_ms << "ms)";

    RecordRunResult result;
    result.run.id = run.id;
    result.run.workflow_id = workflow_id;
    result.run.workflow_name = workflow_name;
    result.run.status = params.status;
    result.run.duration_ms = end_result.duration_ms;
    result.events.created = events_created;
    result.events.duplicates = events_duplicates;
    result.total_cost_units = end_result.total_cost_units;
    result.summary = summary.str();

    return result;
}

} /* namespace drip */
