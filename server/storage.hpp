#pragma once
#include <pqxx/pqxx>
#include <string>
#include <mutex>
#include <optional>
#include <iostream>

using namespace std;

// Minimal connection-pool-like wrapper (single connection safe for phase 1).
class Storage {
public:
    Storage(const string &conn_str) : conn_str(conn_str) {
        // no-op: lazy connect
    }

    bool put(const string &key, const string &value) {
        try {
            pqxx::connection C(conn_str);
            pqxx::work W(C);
            W.exec_params("INSERT INTO kvstore (key, value) VALUES ($1, $2) ON CONFLICT (key) DO UPDATE SET value = EXCLUDED.value", key, value);
            W.commit();
            return true;
        } catch (const std::exception &e) {
            cerr << "storage put error: " << e.what() << endl;
            return false;
        }
    }

    bool get(const string &key, string &value) {
        try {
            pqxx::connection C(conn_str);
            pqxx::work W(C);
            pqxx::result R = W.exec_params("SELECT value FROM kvstore WHERE key = $1", key);
            if (R.empty()) return false;
            value = R[0][0].c_str();
            return true;
        } catch (const std::exception &e) {
            cerr << "storage get error: " << e.what() << endl;
            return false;
        }
    }

    bool remove(const string &key) {
        try {
            pqxx::connection C(conn_str);
            pqxx::work W(C);
            W.exec_params("DELETE FROM kvstore WHERE key = $1", key);
            W.commit();
            return true;
        } catch (const std::exception &e) {
            cerr << "storage delete error: " << e.what() << endl;
            return false;
        }
    }

private:
    string conn_str;
};
