#pragma once
#include <unordered_map>
#include <list>
#include <string>
#include <mutex>

using namespace std;

// Simple thread-safe LRU cache
class LRUCache {
public:
    LRUCache(size_t capacity = 1000) : capacity(capacity) {}

    void put(const string &k, const string &v) {
        lock_guard<mutex> lock(m);
        auto it = map.find(k);
        if (it != map.end()) {
            // update
            it->second.first = v;
            touch(it->second.second);
            return;
        }
        if (map.size() >= capacity) {
            auto last = order.back();
            order.pop_back();
            map.erase(last);
        }
        order.push_front(k);
        map[k] = {v, order.begin()};
    }

    bool get(const string &k, string &out) {
        lock_guard<mutex> lock(m);
        auto it = map.find(k);
        if (it == map.end()) return false;
        out = it->second.first;
        touch(it->second.second);
        return true;
    }

    void remove(const string &k) {
        lock_guard<mutex> lock(m);
        auto it = map.find(k);
        if (it == map.end()) return;
        order.erase(it->second.second);
        map.erase(it);
    }

    void clear() {
        lock_guard<mutex> lock(m);
        map.clear();
        order.clear();
    }

private:
    void touch(list<string>::iterator itpos) {
        string key = *itpos;
        order.erase(itpos);
        order.push_front(key);
        map[key].second = order.begin();
    }

    size_t capacity;
    unordered_map<string, pair<string, list<string>::iterator>> map;
    list<string> order;
    mutex m;
};
