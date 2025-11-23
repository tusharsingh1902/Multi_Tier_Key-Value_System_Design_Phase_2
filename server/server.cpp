#include "threadpool.hpp"
#include "lru_cache.hpp"
#include "storage.hpp"

#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>

#include <iostream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>
#include <thread>

using namespace std;

// ---------------------------
//   GLOBAL COMPONENTS
// ---------------------------

static LRUCache cache(1000);       // LRU cache of size 1000 keys
static Storage* storage = nullptr; // DB interface
static ThreadPool* pool = nullptr; // ThreadPool pointer

// ---------------------------
//   URL DECODING
// ---------------------------

string url_decode(const string &s) {
    string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size()) {
            string hex = s.substr(i + 1, 2);
            char c = (char) strtol(hex.c_str(), nullptr, 16);
            out.push_back(c);
            i += 2;
        } else if (s[i] == '+') {
            out.push_back(' ');
        } else {
            out.push_back(s[i]);
        }
    }
    return out;
}

// ---------------------------
//   SEND HTTP RESPONSE
// ---------------------------

void send_response(int client, const string &body,
                   int status = 200,
                   const string &ctype = "text/plain")
{
    stringstream ss;
    ss << "HTTP/1.1 " << status << " OK\r\n"
       << "Content-Type: " << ctype << "\r\n"
       << "Content-Length: " << body.size() << "\r\n"
       << "Connection: close\r\n\r\n"
       << body;

    string s = ss.str();
    send(client, s.c_str(), s.size(), 0);
}

// ---------------------------
//   PARSE QUERY STRING
// ---------------------------

unordered_map<string,string> parse_query_string(const string &path) {
    unordered_map<string,string> params;
    auto qpos = path.find('?');
    if (qpos == string::npos) return params;

    string q = path.substr(qpos + 1);
    stringstream ss(q);
    string item;

    while (getline(ss, item, '&')) {
        auto eq = item.find('=');
        if (eq != string::npos) {
            string k = url_decode(item.substr(0, eq));
            string v = url_decode(item.substr(eq + 1));
            params[k] = v;
        }
    }
    return params;
}

// ---------------------------
//   HANDLE ONE CLIENT
// ---------------------------

void handle_connection(int client_sock) {
    char buf[8192];
    ssize_t n = recv(client_sock, buf, sizeof(buf) - 1, 0);
    if (n <= 0) { close(client_sock); return; }

    string req(buf, buf + n);

    // Parse method + path
    string method, path;
    {
        stringstream ss(req);
        ss >> method >> path;
    }

    // Determine endpoint
    string endpoint = "/";
    if (path.rfind("/create", 0) == 0) endpoint = "/create";
    else if (path.rfind("/read", 0) == 0) endpoint = "/read";
    else if (path.rfind("/delete", 0) == 0) endpoint = "/delete";
    else if (path.rfind("/compute", 0) == 0) endpoint = "/compute";

    // ---------------------------
    //      CREATE
    // ---------------------------
    if (endpoint == "/create") {
        auto params = parse_query_string(path);
        string key   = params.count("key")   ? params["key"]   : "";
        string value = params.count("value") ? params["value"] : "";

        if (key.empty() || value.empty()) {
            send_response(client_sock, "Missing key or value\n", 400);
        } else {
            cache.put(key, value);                 // update cache
            bool ok = storage->put(key, value);    // update DB
            if (ok) send_response(client_sock, "Created key=" + key + "\n");
            else    send_response(client_sock, "DB error\n", 500);
        }
    }

    // ---------------------------
    //      READ
    // ---------------------------
    else if (endpoint == "/read") {
        auto params = parse_query_string(path);
        string key = params.count("key") ? params["key"] : "";

        if (key.empty()) {
            send_response(client_sock, "Missing key\n", 400);
        } else {
            string v;
            if (cache.get(key, v)) {
                send_response(client_sock, "Cache hit: " + v + "\n");
            } else {
                if (storage->get(key, v)) {
                    cache.put(key, v);
                    send_response(client_sock, "Cache miss -> DB: " + v + "\n");
                } else {
                    send_response(client_sock, "Key not found\n", 404);
                }
            }
        }
    }

    // ---------------------------
    //      DELETE
    // ---------------------------
    else if (endpoint == "/delete") {
        auto params = parse_query_string(path);
        string key = params.count("key") ? params["key"] : "";

        if (key.empty()) {
            send_response(client_sock, "Missing key\n", 400);
        } else {
            cache.remove(key);
            bool ok = storage->remove(key);
            if (ok) send_response(client_sock, "Deleted key=" + key + "\n");
            else    send_response(client_sock, "Key not found\n", 404);
        }
    }

    // ---------------------------
    //      COMPUTE (CPU-bound)
    // ---------------------------
    else if (endpoint == "/compute") {
        int n = 40;  // heavy Fibonacci for CPU load
        long long a = 0, b = 1;
        for (int i = 2; i <= n; i++) { long long t = a + b; a = b; b = t; }
        send_response(client_sock, "fib(40)=" + to_string(b) + "\n");
    }

    // ---------------------------
    //      ROOT
    // ---------------------------
    else {
        string body =
            "Multi-tier KV Server\n"
            "APIs:\n"
            "/create?key=&value=\n"
            "/read?key=\n"
            "/delete?key=\n"
            "/compute\n";
        send_response(client_sock, body);
    }

    close(client_sock);
}

// ---------------------------
//   MAIN SERVER
// ---------------------------

int main(int argc, char** argv) {
    if (argc < 2) {
        cerr << "Usage: ./server \"host=127.0.0.1 port=5432 dbname=decs_project user=$(whoami)\"\n";
        return 1;
    }

    string conn = argv[1];
    Storage st(conn);
    storage = &st;

    // ThreadPool = number of CPU cores
    size_t worker_count = thread::hardware_concurrency();
    if (worker_count == 0) worker_count = 4;

    ThreadPool tp(worker_count);
    pool = &tp;

    int port = 8080;
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) { perror("socket"); return 1; }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK); // 127.0.0.1
    addr.sin_port = htons(port);

    if (::bind(server_fd, (sockaddr*)&addr, sizeof(addr)) < 0) { perror("bind"); return 1; }
    if (::listen(server_fd, 128) < 0) { perror("listen"); return 1; }

    cout << "=========================================\n";
    cout << "  ✅ Multi-tier KV Server running at\n";
    cout << "     http://127.0.0.1:" << port << "\n";
    cout << "  Worker threads = " << worker_count << "\n";
    cout << "=========================================\n";

    // Accept loop
    while (true) {
        int client = accept(server_fd, nullptr, nullptr);
        if (client < 0) continue;

        // Submit task to threadpool
        pool->enqueue([client]() {
            handle_connection(client);
        });
    }

    return 0;
}
