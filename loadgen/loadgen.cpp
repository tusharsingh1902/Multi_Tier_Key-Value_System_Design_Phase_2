// loadgen.cpp
// Usage: ./loadgen <clients> <duration_seconds> <endpoint> <base_url>
// Example: ./loadgen 20 300 /compute http://192.168.64.3:8080

#include <iostream>
#include <thread>
#include <vector>
#include <chrono>
#include <string>
#include <atomic>
#include <curl/curl.h>
#include <sstream>
#include <iomanip>
#include <fstream>
#include <filesystem>
#include <mutex>

using namespace std;
namespace fs = std::filesystem;

static size_t write_cb(void *ptr, size_t size, size_t nmemb, void *userdata) {
    (void)ptr; (void)userdata;
    return size * nmemb;
}

struct Stats {
    atomic<long long> total_requests{0};
    atomic<long long> success{0};
    atomic<long long> total_latency_ns{0};
};

string make_unique_create_url(const string &base_url, const string &endpoint, uint64_t seq, int thread_id) {
    // ensure base+endpoint ends up correct
    string url = base_url;
    if (!url.empty() && url.back() == '/' && !endpoint.empty() && endpoint.front() == '/') {
        url.pop_back();
    }
    url += endpoint;

    // Add unique key/value params: key=<thread>_<seq>_<ts> & value=...
    auto now = chrono::steady_clock::now().time_since_epoch();
    uint64_t ts = chrono::duration_cast<chrono::milliseconds>(now).count();
    stringstream ss;
    ss << (url.find('?') == string::npos ? "?" : "&");
    ss << "key=client" << thread_id << "_" << seq << "_" << ts;
    ss << "&value=val" << seq << "_" << ts;
    return url + ss.str();
}

void client_thread(const string base_url, const string endpoint, int thread_id, int duration_s, Stats &s, atomic<bool> &stop_flag) {
    CURL *curl = curl_easy_init();
    if (!curl) return;

    // Force IPv4 to avoid IPv6 problems
    curl_easy_setopt(curl, CURLOPT_IPRESOLVE, CURL_IPRESOLVE_V4);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);

    auto end_time = chrono::steady_clock::now() + chrono::seconds(duration_s);
    long long seq = 0;

    while (!stop_flag.load() && chrono::steady_clock::now() < end_time) {
        string url;
        // If endpoint is "/create" we need unique keys to force DB writes
        if (endpoint.rfind("/create", 0) == 0) {
            url = make_unique_create_url(base_url, endpoint, seq, thread_id);
        } else {
            // ensure correct concatenation
            url = base_url;
            if (!url.empty() && url.back() == '/' && !endpoint.empty() && endpoint.front() == '/') url.pop_back();
            url += endpoint;
        }

        auto t0 = chrono::steady_clock::now();
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        CURLcode res = curl_easy_perform(curl);
        auto t1 = chrono::steady_clock::now();
        long long ns = chrono::duration_cast<chrono::nanoseconds>(t1 - t0).count();

        s.total_requests.fetch_add(1, memory_order_relaxed);
        s.total_latency_ns.fetch_add(ns, memory_order_relaxed);
        if (res == CURLE_OK) s.success.fetch_add(1, memory_order_relaxed);

        ++seq;
    }

    curl_easy_cleanup(curl);
}

int main(int argc, char** argv) {
    if (argc < 5) {
        cerr << "Usage: ./loadgen <clients> <duration_seconds> <endpoint> <base_url>\n";
        cerr << "Example: ./loadgen 20 300 /compute http://192.168.64.3:8080\n";
        return 1;
    }

    int clients = stoi(argv[1]);
    int duration_s = stoi(argv[2]);
    string endpoint = argv[3];
    string base = argv[4];

    // Validate base (no trailing spaces)
    if (base.size() == 0) {
        cerr << "Base URL empty\n";
        return 1;
    }

    // Prepare results directory
    fs::create_directories("results");

    // Stats + threads
    Stats stats;
    atomic<bool> stop_flag{false};
    vector<thread> threads;
    threads.reserve(clients);

    cout << "Starting loadgen: clients=" << clients << " duration(s)=" << duration_s << " endpoint=" << endpoint << " base=" << base << "\n";
    auto wall_t0 = chrono::steady_clock::now();

    for (int i = 0; i < clients; ++i) {
        threads.emplace_back(client_thread, base, endpoint, i, duration_s, ref(stats), ref(stop_flag));
    }

    // Wait for duration to elapse
    this_thread::sleep_for(chrono::seconds(duration_s));
    stop_flag.store(true);

    for (auto &t : threads) if (t.joinable()) t.join();

    auto wall_t1 = chrono::steady_clock::now();
    double total_s = chrono::duration_cast<chrono::duration<double>>(wall_t1 - wall_t0).count();
    long long total_req = stats.total_requests.load();
    long long succ = stats.success.load();
    double throughput = total_s > 0.0 ? (double)succ / total_s : 0.0;
    double avg_resp_ms = succ > 0 ? (double)stats.total_latency_ns.load() / (double)succ / 1e6 : 0.0;

    cout << "=== Load Test Summary ===\n";
    cout << "Clients: " << clients << "\n";
    cout << "Requests: " << total_req << "\n";
    cout << "Successful: " << succ << "\n";
    cout << fixed << setprecision(3);
    cout << "Total(s): " << total_s << "\n";
    cout << "Throughput(req/s): " << throughput << "\n";
    cout << "AvgResp(ms): " << avg_resp_ms << "\n";

    // Append to CSV (atomic/mutex protected)
    static mutex file_mutex;
    lock_guard<mutex> lg(file_mutex);
    ofstream f("results/loadtest.csv", ios::app);
    if (!f) {
        cerr << "Failed to open results/loadtest.csv for writing\n";
    } else {
        if (f.tellp() == 0) {
            f << "Clients,Requests,Successful,TotalTime(s),Throughput(req/s),AvgResp(ms),Endpoint\n";
        }
        f << clients << "," << total_req << "," << succ << "," << total_s << "," << throughput << "," << avg_resp_ms << "," << endpoint << "\n";
        f.close();
        cout << "Results saved to results/loadtest.csv\n";
    }

    return 0;
}
