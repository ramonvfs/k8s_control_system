/*
 * Fuzzy control service ("Control as a Service").
 *
 * Exposes the FIM over HTTP so that remote plants can close their loop
 * through the network. One process = one Kubernetes replica (pod); horizontal
 * scaling is done by the Deployment, load balancing by the Service.
 *
 * Endpoints
 *   POST /control  {"error": e, "delta_error": de, "client_id": "...", "seq": n}
 *                  -> {"u": s, "pod": "<pod name>"}
 *   GET  /health   -> {"status": "ok", "pod": "<pod name>"}
 *
 * Every /control request is logged to a per-pod CSV file (see
 * docs/data_schema.md). Configuration is read from environment variables
 * (see print_banner() or docs/controller.md).
 */
#include "csv_logger.hpp"
#include "fuzzy_fim.hpp"

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <deque>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>

#include <pthread.h>
#include <unistd.h>

using json = nlohmann::json;
using steady = std::chrono::steady_clock;
using wall = std::chrono::system_clock;

namespace {

std::string env_str(const char* name, const std::string& def) {
    const char* v = std::getenv(name);
    return (v && *v) ? std::string(v) : def;
}

long env_long(const char* name, long def) {
    const char* v = std::getenv(name);
    return (v && *v) ? std::atol(v) : def;
}

std::string pod_name() {
    std::string name = env_str("POD_NAME", env_str("HOSTNAME", ""));
    if (name.empty()) {
        char buf[256] = {0};
        gethostname(buf, sizeof(buf) - 1);
        name = buf;
    }
    return name;
}

long long to_us(wall::time_point tp) {
    return std::chrono::duration_cast<std::chrono::microseconds>(tp.time_since_epoch()).count();
}

long long us_between(steady::time_point a, steady::time_point b) {
    return std::chrono::duration_cast<std::chrono::microseconds>(b - a).count();
}

// "YYYY-MM-DD HH:MM:SS.uuuuuu" (local time) for a wall-clock instant.
std::string format_wall(wall::time_point tp, const char* date_fmt = "%Y-%m-%d %H:%M:%S",
                        bool with_us = true) {
    long long us = to_us(tp);
    std::time_t secs = static_cast<std::time_t>(us / 1000000);
    std::tm tm{};
    localtime_r(&secs, &tm);
    char buf[64];
    size_t n = std::strftime(buf, sizeof(buf), date_fmt, &tm);
    if (with_us) std::snprintf(buf + n, sizeof(buf) - n, ".%06lld", us % 1000000);
    return buf;
}

// Client ids are written verbatim to the CSV: strip separators.
std::string csv_safe(std::string s) {
    for (char& c : s) {
        if (c == ',' || c == '"' || c == '\n' || c == '\r') c = '_';
    }
    return s.empty() ? "-" : s;
}

struct Config {
    int port;
    std::string pod;
    std::string node;
    bool log_enabled;
    std::string log_dir;
    std::string exp_name;
    long http_threads;     // 0 = cpp-httplib default
    long rate_window_ms;
};

Config load_config() {
    Config c;
    c.port = static_cast<int>(env_long("PORT", 5000));
    c.pod = pod_name();
    c.node = env_str("NODE_NAME", "-");
    c.log_enabled = env_long("CONTROLLER_LOG", 1) != 0;
    c.log_dir = env_str("LOG_DIR", ".");
    c.exp_name = env_str("EXP_NAME", "");
    c.http_threads = env_long("HTTP_THREADS", 0);
    c.rate_window_ms = env_long("RATE_WINDOW_MS", 1000);
    if (c.rate_window_ms <= 0) c.rate_window_ms = 1000;
    return c;
}

// Result_CSS_<start>_Exp_<EXP_NAME>_pod_<pod>.csv (Exp_ part omitted if EXP_NAME is empty)
std::string log_path(const Config& c, wall::time_point start) {
    std::string name = "Result_CSS_" + format_wall(start, "%Y-%m-%d_%H-%M-%S", false);
    if (!c.exp_name.empty()) name += "_Exp_" + c.exp_name;
    name += "_pod_" + c.pod + ".csv";
    return c.log_dir + "/" + name;
}

void print_banner(const Config& c, const std::string& path) {
    std::cout << "---------------------------------------------------\n"
              << "Fuzzy Inference Machine (FIM) service\n"
              << "Pod:            " << c.pod << "\n"
              << "Node:           " << c.node << "\n"
              << "Port:           " << c.port << "\n"
              << "HTTP threads:   "
              << (c.http_threads > 0 ? std::to_string(c.http_threads) : "cpp-httplib default") << "\n"
              << "Rate window:    " << c.rate_window_ms << " ms\n"
              << "CSV log:        " << (c.log_enabled ? path : "disabled") << "\n"
              << "---------------------------------------------------" << std::endl;
}

}  // namespace

int main() {
    const auto start_wall = wall::now();
    const auto start_steady = steady::now();
    const Config cfg = load_config();
    const std::string path = log_path(cfg, start_wall);

    // Handle SIGINT/SIGTERM in a dedicated thread (signals blocked everywhere
    // else) so the server can stop gracefully and the log is flushed.
    sigset_t sigs;
    sigemptyset(&sigs);
    sigaddset(&sigs, SIGINT);
    sigaddset(&sigs, SIGTERM);
    pthread_sigmask(SIG_BLOCK, &sigs, nullptr);

    print_banner(cfg, path);

    CsvLogger logger;
    if (cfg.log_enabled &&
        !logger.open(path,
                     "TimestampFormat_us,Timestamp_us,ElapsedTime_us,Pod,IIoT_name,RemoteAddr,"
                     "seq,e,ed,s,Request_rate,LockWait_us,Inference_us,Handler_us\n")) {
        std::cerr << "Cannot open log file " << path << " (logging disabled)" << std::endl;
    }

    FuzzyFIM controller;
    std::mutex mtx;                           // serializes the (non thread-safe) FIM
    std::deque<steady::time_point> arrivals;  // request times inside the rate window
    const auto rate_window = std::chrono::milliseconds(cfg.rate_window_ms);
    const double rate_window_s = cfg.rate_window_ms / 1000.0;

    httplib::Server svr;
    if (cfg.http_threads > 0) {
        const size_t n = static_cast<size_t>(cfg.http_threads);
        svr.new_task_queue = [n] { return new httplib::ThreadPool(n); };
    }

    svr.Post("/control", [&](const httplib::Request& req, httplib::Response& res) {
        const auto t_entry = steady::now();
        const auto t_wall = wall::now();

        double e, de;
        long long seq;
        std::string client;
        try {
            auto data = json::parse(req.body);
            e = data.value("error", 0.0);
            de = data.value("delta_error", 0.0);
            seq = data.value("seq", -1LL);
            client = data.value("client_id", std::string());
        } catch (...) {
            res.status = 400;
            res.set_content("{\"error\": \"Invalid JSON or Processing Error\"}", "application/json");
            return;
        }

        const auto t_parsed = steady::now();
        steady::time_point t_locked, t_done;
        double u;
        size_t in_window;
        {
            std::lock_guard<std::mutex> lock(mtx);
            t_locked = steady::now();
            u = controller.compute(e, de);
            t_done = steady::now();

            arrivals.push_back(t_locked);
            while (!arrivals.empty() && t_locked - arrivals.front() > rate_window) {
                arrivals.pop_front();
            }
            in_window = arrivals.size();
        }

        // ordered_json keeps "u" first: older plant binaries parse the reply with
        // sscanf("{\"u\": %lf}") and ignore the remaining fields.
        nlohmann::ordered_json response;
        response["u"] = u;
        response["pod"] = cfg.pod;
        res.set_content(response.dump(), "application/json");

        char line[768];
        std::snprintf(line, sizeof(line),
                      "%s,%lld,%lld,%s,%s,%s,%lld,%.8f,%.8f,%.8f,%.2f,%lld,%lld,%lld\n",
                      format_wall(t_wall).c_str(), to_us(t_wall), us_between(start_steady, t_entry),
                      cfg.pod.c_str(), csv_safe(client).c_str(), req.remote_addr.c_str(), seq,
                      e, de, u, in_window / rate_window_s,
                      us_between(t_parsed, t_locked), us_between(t_locked, t_done),
                      us_between(t_entry, t_done));
        logger.push(line);
    });

    svr.Get("/health", [&](const httplib::Request&, httplib::Response& res) {
        res.set_content("{\"status\": \"ok\", \"pod\": \"" + cfg.pod + "\"}", "application/json");
    });

    std::thread([&svr, sigs] {
        int sig = 0;
        sigwait(&sigs, &sig);
        std::cout << "Signal " << sig << " received, shutting down..." << std::endl;
        svr.stop();
    }).detach();

    std::cout << "Fuzzy Inference Machine (FIM) running on port " << cfg.port << "..." << std::endl;
    const bool ok = svr.listen("0.0.0.0", cfg.port);
    if (!ok) std::cerr << "Failed to listen on port " << cfg.port << std::endl;

    logger.close();
    return ok ? 0 : 1;
}
