// ============================================================================
// MySQL Performance Benchmarks: Raw MySQL C API vs Raw ODBC vs cpplinq ORM
// Scales: 1,000, 10,000, 100,000 rows
// ============================================================================
//
// Compares three data access layers against the same MySQL / MariaDB instance:
//   1. Native MySQL C API — Direct libmariadb / libmysqlclient (baseline)
//   2. Raw ODBC          — Direct ODBC 3.x API calls (no ORM overhead)
//   3. cpplinq ORM       — Full ORM: AST -> SqlGenerator -> Driver -> RowMapper
//
// Environment variables:
//   CPPLINQ_MYSQL_ODBC   — ODBC DSN or connection string (for raw ODBC + cpplinq ODBC)
//   CPPLINQ_MYSQL_CLIENT — Native connection string or parameters (host, port, user, pwd, db)
//
// ============================================================================

#include <benchmark/benchmark.h>
#include "cpplinq/cpplinq.hpp"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <windows.h>
#endif

#ifdef HAS_MYSQL
#if __has_include(<mariadb/mysql.h>)
#include <mariadb/mysql.h>
#elif __has_include(<mysql/mysql.h>)
#include <mysql/mysql.h>
#else
#include <mysql.h>
#endif

#if !defined(MARIADB_BASE_VERSION) && !defined(MARIADB_VERSION_ID) && !defined(LIBMARIADB)
#ifndef my_bool
typedef bool my_bool;
#endif
#endif
#endif

#include <sql.h>
#include <sqlext.h>

#include <string>
#include <vector>
#include <optional>
#include <random>
#include <sstream>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <cctype>

using namespace cpplinq;

// ============================================================================
// Entity Structs & Table Schemas
// ============================================================================

struct BenchUser {
    int id = 0;
    std::string name;
    std::optional<std::string> email;
    int age = 0;
};

struct BenchOrder {
    int id = 0;
    int user_id = 0;
    double amount = 0.0;
    std::string status;
};

inline const auto bench_users = table<BenchUser>(
    "bench_users",
    column("id",    &BenchUser::id,    primary_key, auto_increment),
    column("name",  &BenchUser::name,  not_null),
    column("email", &BenchUser::email),
    column("age",   &BenchUser::age,   not_null)
);

inline const auto bench_orders = table<BenchOrder>(
    "bench_orders",
    column("id",      &BenchOrder::id,      primary_key, auto_increment),
    column("user_id", &BenchOrder::user_id, not_null),
    column("amount",  &BenchOrder::amount,  not_null),
    column("status",  &BenchOrder::status,  not_null)
);

// ============================================================================
// Data Generators (deterministic seeds for reproducibility)
// ============================================================================

static std::vector<BenchUser> generate_users(size_t count) {
    std::mt19937 rng(42);
    std::uniform_int_distribution<int> age_dist(18, 80);
    std::vector<BenchUser> users;
    users.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        BenchUser u;
        u.name = "User_" + std::to_string(i);
        if (i % 5 != 0)
            u.email = "user" + std::to_string(i) + "@bench.test";
        u.age = age_dist(rng);
        users.push_back(std::move(u));
    }
    return users;
}

static std::vector<BenchOrder> generate_orders(size_t count) {
    std::mt19937 rng(123);
    std::uniform_real_distribution<double> amount_dist(5.0, 999.99);
    const char* statuses[] = {"pending", "shipped", "delivered", "cancelled"};
    std::vector<BenchOrder> orders;
    orders.reserve(count);
    for (size_t i = 1; i <= count; ++i) {
        BenchOrder o;
        o.user_id = static_cast<int>(i);
        o.amount = amount_dist(rng);
        o.status = statuses[rng() % 4];
        orders.push_back(std::move(o));
    }
    return orders;
}

// ============================================================================
// Environment variable helpers
// ============================================================================

static std::string get_odbc_conn_str() {
    const char* env = std::getenv("CPPLINQ_MYSQL_ODBC");
    if (env && env[0] != '\0') return std::string(env);
    return "MySQLDSN";
}

struct MysqlConnParams {
    std::string host = "127.0.0.1";
    unsigned int port = 3306;
    std::string user = "cppdb";
    std::string password = "cppdb_password";
    std::string db = "cppdb";
};

static MysqlConnParams get_mysql_native_params() {
    MysqlConnParams params;
    const char* env_client = std::getenv("CPPLINQ_MYSQL_CLIENT");
    const char* env_odbc = std::getenv("CPPLINQ_MYSQL_ODBC");
    std::string raw = env_client ? env_client : (env_odbc ? env_odbc : "");

    if (raw.empty()) return params;

    // Parse key=value; pairs
    size_t start = 0;
    while (start < raw.size()) {
        size_t end = raw.find(';', start);
        if (end == std::string::npos) end = raw.size();
        std::string token = raw.substr(start, end - start);
        start = end + 1;

        size_t eq = token.find('=');
        if (eq != std::string::npos) {
            std::string k = token.substr(0, eq);
            std::string v = token.substr(eq + 1);
            while (!k.empty() && std::isspace(static_cast<unsigned char>(k.front()))) k.erase(k.begin());
            while (!k.empty() && std::isspace(static_cast<unsigned char>(k.back()))) k.pop_back();
            while (!v.empty() && std::isspace(static_cast<unsigned char>(v.front()))) v.erase(v.begin());
            while (!v.empty() && std::isspace(static_cast<unsigned char>(v.back()))) v.pop_back();
            if (v.size() >= 2 && v.front() == '{' && v.back() == '}') {
                v = v.substr(1, v.size() - 2);
            }
            std::string lk = k;
            std::transform(lk.begin(), lk.end(), lk.begin(), [](unsigned char c) { return std::tolower(c); });

            if (lk == "host" || lk == "server") params.host = v;
            else if (lk == "port") params.port = static_cast<unsigned int>(std::atoi(v.c_str()));
            else if (lk == "user" || lk == "uid" || lk == "username") params.user = v;
            else if (lk == "pwd" || lk == "password") params.password = v;
            else if (lk == "db" || lk == "database" || lk == "dbname") params.db = v;
        }
    }
    return params;
}

static void odbc_check(SQLRETURN rc, SQLSMALLINT handle_type, SQLHANDLE handle, const char* context) {
    if (SQL_SUCCEEDED(rc)) return;
    SQLCHAR state[6], msg[1024];
    SQLINTEGER native;
    SQLSMALLINT msg_len;
    SQLGetDiagRecA(handle_type, handle, 1, state, &native, msg, sizeof(msg), &msg_len);
    std::string err = std::string(context) + ": [" + std::string(reinterpret_cast<char*>(state)) + "] "
                    + std::string(reinterpret_cast<char*>(msg), msg_len);
    throw std::runtime_error(err);
}

// Register for 1000, 10000, 100000
#define BENCH_ARGS ->Arg(1000)->Arg(10000)->Arg(100000)->Iterations(1)->Unit(benchmark::kMillisecond)

// ############################################################################
//
//  SECTION 1: cpplinq ORM Benchmarks
//
// ############################################################################

class CpplinqFixture : public benchmark::Fixture {
public:
    void SetUp(benchmark::State& state) override {
        auto conn_str = get_odbc_conn_str();
        try {
            db = std::make_unique<DbContext<mysql>>(conn_str);
            ensure_tables();
            clear_tables();
        } catch (const std::exception& e) {
            state.SkipWithError(e.what());
        }
    }

    void TearDown(benchmark::State&) override {
        if (db) {
            try { clear_tables(); } catch (...) {}
        }
    }

    void ensure_tables() {
        db->ensure_table(bench_users);
        db->ensure_table(bench_orders);
    }

    void clear_tables() {
        try {
            db->execute_raw("TRUNCATE TABLE `bench_orders`");
            db->execute_raw("TRUNCATE TABLE `bench_users`");
        } catch (...) {
            try { db->execute_raw("DELETE FROM `bench_orders`"); } catch (...) {}
            try { db->execute_raw("DELETE FROM `bench_users`"); } catch (...) {}
        }
    }

    void seed_users(int64_t n) {
        auto users = generate_users(static_cast<size_t>(n));
        db->insert_many(bench_users, users);
    }

    void seed_all(int64_t n) {
        seed_users(n);
        auto orders = generate_orders(static_cast<size_t>(n));
        db->insert_many(bench_orders, orders);
    }

    std::unique_ptr<DbContext<mysql>> db;
};

// ── cpplinq InsertBulk ───────────────────────────────────────────────

BENCHMARK_DEFINE_F(CpplinqFixture, InsertBulk)(benchmark::State& state) {
    if (!db) return;
    auto users = generate_users(static_cast<size_t>(state.range(0)));
    for (auto _ : state) {
        state.PauseTiming();
        clear_tables();
        state.ResumeTiming();

        db->insert_many(bench_users, users);
    }
    state.SetItemsProcessed(state.iterations() * state.range(0));
}
BENCHMARK_REGISTER_F(CpplinqFixture, InsertBulk) BENCH_ARGS;

// ── cpplinq SelectAll ────────────────────────────────────────────────

BENCHMARK_DEFINE_F(CpplinqFixture, SelectAll)(benchmark::State& state) {
    if (!db) return;
    clear_tables();
    seed_users(state.range(0));
    for (auto _ : state) {
        auto results = db->from(bench_users).to_list();
        benchmark::DoNotOptimize(results.size());
    }
    state.SetItemsProcessed(state.iterations() * state.range(0));
}
BENCHMARK_REGISTER_F(CpplinqFixture, SelectAll) BENCH_ARGS;

// ── cpplinq SelectFiltered ───────────────────────────────────────────

BENCHMARK_DEFINE_F(CpplinqFixture, SelectFiltered)(benchmark::State& state) {
    if (!db) return;
    clear_tables();
    seed_users(state.range(0));
    for (auto _ : state) {
        auto results = db->from(bench_users)
            .where(bench_users["age"] > 30 && bench_users["email"].is_not_null())
            .order_by(bench_users["age"])
            .to_list();
        benchmark::DoNotOptimize(results.size());
    }
}
BENCHMARK_REGISTER_F(CpplinqFixture, SelectFiltered) BENCH_ARGS;

// ── cpplinq JoinQuery ────────────────────────────────────────────────

BENCHMARK_DEFINE_F(CpplinqFixture, JoinQuery)(benchmark::State& state) {
    if (!db) return;
    clear_tables();
    seed_all(state.range(0));
    for (auto _ : state) {
        auto results = db->from(bench_users)
            .join(bench_orders).on(bench_users["id"] == bench_orders["user_id"])
            .where(bench_orders["amount"] > 100.0)
            .to_list();
        benchmark::DoNotOptimize(results.size());
    }
}
BENCHMARK_REGISTER_F(CpplinqFixture, JoinQuery) BENCH_ARGS;

// ── cpplinq UpdateBulk ───────────────────────────────────────────────

BENCHMARK_DEFINE_F(CpplinqFixture, UpdateBulk)(benchmark::State& state) {
    if (!db) return;
    for (auto _ : state) {
        state.PauseTiming();
        clear_tables();
        seed_users(state.range(0));
        state.ResumeTiming();

        auto affected = db->from(bench_users)
            .where(bench_users["age"] >= 25 && bench_users["age"] <= 40)
            .update({
                bench_users["age"] = 99
            });
        benchmark::DoNotOptimize(affected);
    }
    state.SetItemsProcessed(state.iterations() * state.range(0));
}
BENCHMARK_REGISTER_F(CpplinqFixture, UpdateBulk) BENCH_ARGS;

// ── cpplinq DeleteBulk ───────────────────────────────────────────────

BENCHMARK_DEFINE_F(CpplinqFixture, DeleteBulk)(benchmark::State& state) {
    if (!db) return;
    for (auto _ : state) {
        state.PauseTiming();
        clear_tables();
        seed_all(state.range(0));
        state.ResumeTiming();

        auto affected = db->from(bench_orders)
            .where(bench_orders["status"] == std::string("cancelled"))
            .remove();
        benchmark::DoNotOptimize(affected);
    }
    state.SetItemsProcessed(state.iterations() * state.range(0));
}
BENCHMARK_REGISTER_F(CpplinqFixture, DeleteBulk) BENCH_ARGS;

// ############################################################################
//
//  SECTION 2: Raw ODBC Benchmarks
//
// ############################################################################

class OdbcFixture : public benchmark::Fixture {
public:
    void SetUp(benchmark::State& state) override {
        auto conn_str = get_odbc_conn_str();
        try {
            SQLRETURN rc;
            rc = SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &henv_);
            odbc_check(rc, SQL_HANDLE_ENV, henv_, "SQLAllocHandle(ENV)");
            SQLSetEnvAttr(henv_, SQL_ATTR_ODBC_VERSION, reinterpret_cast<void*>(SQL_OV_ODBC3), 0);

            rc = SQLAllocHandle(SQL_HANDLE_DBC, henv_, &hdbc_);
            odbc_check(rc, SQL_HANDLE_ENV, henv_, "SQLAllocHandle(DBC)");

            std::string full_cs = conn_str;
            if (full_cs.find('=') == std::string::npos)
                full_cs = "DSN=" + full_cs + ";";

            SQLCHAR out[1024];
            SQLSMALLINT out_len;
            rc = SQLDriverConnectA(hdbc_, nullptr,
                                   reinterpret_cast<SQLCHAR*>(full_cs.data()),
                                   SQL_NTS, out, sizeof(out), &out_len,
                                   SQL_DRIVER_NOPROMPT);
            odbc_check(rc, SQL_HANDLE_DBC, hdbc_, "SQLDriverConnect");

            connected_ = true;
            ensure_tables();
            clear_tables();
        } catch (const std::exception& e) {
            state.SkipWithError(e.what());
        }
    }

    void TearDown(benchmark::State&) override {
        if (connected_) {
            try { clear_tables(); } catch (...) {}
            SQLDisconnect(hdbc_);
        }
        if (hdbc_ != SQL_NULL_HDBC) SQLFreeHandle(SQL_HANDLE_DBC, hdbc_);
        if (henv_ != SQL_NULL_HENV) SQLFreeHandle(SQL_HANDLE_ENV, henv_);
        henv_ = SQL_NULL_HENV;
        hdbc_ = SQL_NULL_HDBC;
        connected_ = false;
    }

    void exec_sql(const char* sql) {
        SQLHSTMT hstmt = SQL_NULL_HSTMT;
        SQLRETURN rc = SQLAllocHandle(SQL_HANDLE_STMT, hdbc_, &hstmt);
        odbc_check(rc, SQL_HANDLE_DBC, hdbc_, "SQLAllocHandle(STMT)");
        rc = SQLExecDirectA(hstmt, reinterpret_cast<SQLCHAR*>(const_cast<char*>(sql)), SQL_NTS);
        if (!SQL_SUCCEEDED(rc) && rc != SQL_NO_DATA) {
            odbc_check(rc, SQL_HANDLE_STMT, hstmt, sql);
        }
        SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
    }

    void ensure_tables() {
        exec_sql(
            "CREATE TABLE IF NOT EXISTS `bench_users` ("
            "`id` BIGINT AUTO_INCREMENT PRIMARY KEY, "
            "`name` VARCHAR(255) NOT NULL, "
            "`email` VARCHAR(255), "
            "`age` INT NOT NULL)"
        );
        exec_sql(
            "CREATE TABLE IF NOT EXISTS `bench_orders` ("
            "`id` BIGINT AUTO_INCREMENT PRIMARY KEY, "
            "`user_id` INT NOT NULL, "
            "`amount` DOUBLE NOT NULL, "
            "`status` VARCHAR(64) NOT NULL)"
        );
    }

    void clear_tables() {
        try {
            exec_sql("TRUNCATE TABLE `bench_orders`");
            exec_sql("TRUNCATE TABLE `bench_users`");
        } catch (...) {
            try { exec_sql("DELETE FROM `bench_orders`"); } catch (...) {}
            try { exec_sql("DELETE FROM `bench_users`"); } catch (...) {}
        }
    }

    void insert_users_bulk(const std::vector<BenchUser>& users) {
        if (users.empty()) return;
        exec_sql("START TRANSACTION");

        constexpr size_t CHUNK_SIZE = 1000;
        for (size_t offset = 0; offset < users.size(); offset += CHUNK_SIZE) {
            size_t batch_size = std::min(CHUNK_SIZE, users.size() - offset);

            SQLHSTMT hstmt = SQL_NULL_HSTMT;
            SQLAllocHandle(SQL_HANDLE_STMT, hdbc_, &hstmt);
            SQLSetStmtAttr(hstmt, SQL_ATTR_PARAM_BIND_TYPE, SQL_PARAM_BIND_BY_COLUMN, 0);
            SQLSetStmtAttr(hstmt, SQL_ATTR_PARAMSET_SIZE, reinterpret_cast<SQLPOINTER>(batch_size), 0);

            SQLULEN processed = 0;
            SQLUSMALLINT status_array[CHUNK_SIZE];
            SQLSetStmtAttr(hstmt, SQL_ATTR_PARAMS_PROCESSED_PTR, &processed, 0);
            SQLSetStmtAttr(hstmt, SQL_ATTR_PARAM_STATUS_PTR, status_array, 0);

            SQLPrepareA(hstmt, reinterpret_cast<SQLCHAR*>(const_cast<char*>(
                "INSERT INTO `bench_users` (`name`, `email`, `age`) VALUES (?, ?, ?)"
            )), SQL_NTS);

            std::vector<char> name_buf(batch_size * 256, 0);
            std::vector<SQLLEN> name_inds(batch_size);
            std::vector<char> email_buf(batch_size * 256, 0);
            std::vector<SQLLEN> email_inds(batch_size);
            std::vector<int> age_buf(batch_size);
            std::vector<SQLLEN> age_inds(batch_size, 0);

            for (size_t i = 0; i < batch_size; ++i) {
                const auto& u = users[offset + i];
                std::memcpy(&name_buf[i * 256], u.name.data(), u.name.size());
                name_buf[i * 256 + u.name.size()] = '\0';
                name_inds[i] = static_cast<SQLLEN>(u.name.size());

                if (u.email.has_value()) {
                    std::memcpy(&email_buf[i * 256], u.email->data(), u.email->size());
                    email_buf[i * 256 + u.email->size()] = '\0';
                    email_inds[i] = static_cast<SQLLEN>(u.email->size());
                } else {
                    email_inds[i] = SQL_NULL_DATA;
                }
                age_buf[i] = u.age;
            }

            SQLBindParameter(hstmt, 1, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_VARCHAR,
                             255, 0, name_buf.data(), 256, name_inds.data());
            SQLBindParameter(hstmt, 2, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_VARCHAR,
                             255, 0, email_buf.data(), 256, email_inds.data());
            SQLBindParameter(hstmt, 3, SQL_PARAM_INPUT, SQL_C_SLONG, SQL_INTEGER,
                             0, 0, age_buf.data(), 0, age_inds.data());

            SQLExecute(hstmt);
            SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
        }

        exec_sql("COMMIT");
    }

    void insert_orders_bulk(const std::vector<BenchOrder>& orders) {
        if (orders.empty()) return;
        exec_sql("START TRANSACTION");

        constexpr size_t CHUNK_SIZE = 1000;
        for (size_t offset = 0; offset < orders.size(); offset += CHUNK_SIZE) {
            size_t batch_size = std::min(CHUNK_SIZE, orders.size() - offset);

            SQLHSTMT hstmt = SQL_NULL_HSTMT;
            SQLAllocHandle(SQL_HANDLE_STMT, hdbc_, &hstmt);
            SQLSetStmtAttr(hstmt, SQL_ATTR_PARAM_BIND_TYPE, SQL_PARAM_BIND_BY_COLUMN, 0);
            SQLSetStmtAttr(hstmt, SQL_ATTR_PARAMSET_SIZE, reinterpret_cast<SQLPOINTER>(batch_size), 0);

            SQLULEN processed = 0;
            SQLUSMALLINT status_array[CHUNK_SIZE];
            SQLSetStmtAttr(hstmt, SQL_ATTR_PARAMS_PROCESSED_PTR, &processed, 0);
            SQLSetStmtAttr(hstmt, SQL_ATTR_PARAM_STATUS_PTR, status_array, 0);

            SQLPrepareA(hstmt, reinterpret_cast<SQLCHAR*>(const_cast<char*>(
                "INSERT INTO `bench_orders` (`user_id`, `amount`, `status`) VALUES (?, ?, ?)"
            )), SQL_NTS);

            std::vector<int> uid_buf(batch_size);
            std::vector<SQLLEN> uid_inds(batch_size, 0);
            std::vector<double> amt_buf(batch_size);
            std::vector<SQLLEN> amt_inds(batch_size, 0);
            std::vector<char> status_buf(batch_size * 64, 0);
            std::vector<SQLLEN> status_inds(batch_size);

            for (size_t i = 0; i < batch_size; ++i) {
                const auto& o = orders[offset + i];
                uid_buf[i] = o.user_id;
                amt_buf[i] = o.amount;
                std::memcpy(&status_buf[i * 64], o.status.data(), o.status.size());
                status_buf[i * 64 + o.status.size()] = '\0';
                status_inds[i] = static_cast<SQLLEN>(o.status.size());
            }

            SQLBindParameter(hstmt, 1, SQL_PARAM_INPUT, SQL_C_SLONG, SQL_INTEGER,
                             0, 0, uid_buf.data(), 0, uid_inds.data());
            SQLBindParameter(hstmt, 2, SQL_PARAM_INPUT, SQL_C_DOUBLE, SQL_DOUBLE,
                             0, 0, amt_buf.data(), 0, amt_inds.data());
            SQLBindParameter(hstmt, 3, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_VARCHAR,
                             63, 0, status_buf.data(), 64, status_inds.data());

            SQLExecute(hstmt);
            SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
        }

        exec_sql("COMMIT");
    }

    void seed_users(int64_t n) {
        auto users = generate_users(static_cast<size_t>(n));
        insert_users_bulk(users);
    }

    void seed_all(int64_t n) {
        seed_users(n);
        auto orders = generate_orders(static_cast<size_t>(n));
        insert_orders_bulk(orders);
    }

    std::vector<BenchUser> select_all_raw() {
        SQLHSTMT hstmt = SQL_NULL_HSTMT;
        SQLAllocHandle(SQL_HANDLE_STMT, hdbc_, &hstmt);
        SQLExecDirectA(hstmt, reinterpret_cast<SQLCHAR*>(const_cast<char*>(
            "SELECT `id`, `name`, `email`, `age` FROM `bench_users`"
        )), SQL_NTS);

        SQLINTEGER id_val = 0, age_val = 0;
        char name_buf[256] = {0}, email_buf[256] = {0};
        SQLLEN ind_id = 0, ind_name = 0, ind_email = 0, ind_age = 0;

        SQLBindCol(hstmt, 1, SQL_C_SLONG, &id_val, 0, &ind_id);
        SQLBindCol(hstmt, 2, SQL_C_CHAR, name_buf, sizeof(name_buf), &ind_name);
        SQLBindCol(hstmt, 3, SQL_C_CHAR, email_buf, sizeof(email_buf), &ind_email);
        SQLBindCol(hstmt, 4, SQL_C_SLONG, &age_val, 0, &ind_age);

        std::vector<BenchUser> results;
        while (SQLFetch(hstmt) == SQL_SUCCESS) {
            BenchUser u;
            u.id = static_cast<int>(id_val);
            u.name = std::string(name_buf, ind_name > 0 ? static_cast<size_t>(ind_name) : 0);
            if (ind_email == SQL_NULL_DATA) {
                u.email = std::nullopt;
            } else {
                u.email = std::string(email_buf, ind_email > 0 ? static_cast<size_t>(ind_email) : 0);
            }
            u.age = static_cast<int>(age_val);
            results.push_back(std::move(u));
        }
        SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
        return results;
    }

    std::vector<BenchUser> select_filtered_raw() {
        SQLHSTMT hstmt = SQL_NULL_HSTMT;
        SQLAllocHandle(SQL_HANDLE_STMT, hdbc_, &hstmt);
        SQLPrepareA(hstmt, reinterpret_cast<SQLCHAR*>(const_cast<char*>(
            "SELECT `id`, `name`, `email`, `age` FROM `bench_users` "
            "WHERE `age` > ? AND `email` IS NOT NULL "
            "ORDER BY `age`"
        )), SQL_NTS);

        SQLINTEGER age_param = 30;
        SQLLEN ind_age_param = 0;
        SQLBindParameter(hstmt, 1, SQL_PARAM_INPUT, SQL_C_SLONG, SQL_INTEGER, 0, 0, &age_param, 0, &ind_age_param);

        SQLExecute(hstmt);

        SQLINTEGER id_val = 0, age_val = 0;
        char name_buf[256] = {0}, email_buf[256] = {0};
        SQLLEN ind_id = 0, ind_name = 0, ind_email = 0, ind_age = 0;

        SQLBindCol(hstmt, 1, SQL_C_SLONG, &id_val, 0, &ind_id);
        SQLBindCol(hstmt, 2, SQL_C_CHAR, name_buf, sizeof(name_buf), &ind_name);
        SQLBindCol(hstmt, 3, SQL_C_CHAR, email_buf, sizeof(email_buf), &ind_email);
        SQLBindCol(hstmt, 4, SQL_C_SLONG, &age_val, 0, &ind_age);

        std::vector<BenchUser> results;
        while (SQLFetch(hstmt) == SQL_SUCCESS) {
            BenchUser u;
            u.id = static_cast<int>(id_val);
            u.name = std::string(name_buf, ind_name > 0 ? static_cast<size_t>(ind_name) : 0);
            if (ind_email == SQL_NULL_DATA) {
                u.email = std::nullopt;
            } else {
                u.email = std::string(email_buf, ind_email > 0 ? static_cast<size_t>(ind_email) : 0);
            }
            u.age = static_cast<int>(age_val);
            results.push_back(std::move(u));
        }
        SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
        return results;
    }

    std::vector<std::pair<BenchUser, BenchOrder>> join_query_raw() {
        SQLHSTMT hstmt = SQL_NULL_HSTMT;
        SQLAllocHandle(SQL_HANDLE_STMT, hdbc_, &hstmt);
        SQLPrepareA(hstmt, reinterpret_cast<SQLCHAR*>(const_cast<char*>(
            "SELECT u.`id`, u.`name`, u.`email`, u.`age`, "
            "o.`id`, o.`user_id`, o.`amount`, o.`status` "
            "FROM `bench_users` u "
            "INNER JOIN `bench_orders` o ON u.`id` = o.`user_id` "
            "WHERE o.`amount` > ?"
        )), SQL_NTS);

        double amount_param = 100.0;
        SQLLEN ind_amt_param = 0;
        SQLBindParameter(hstmt, 1, SQL_PARAM_INPUT, SQL_C_DOUBLE, SQL_DOUBLE, 0, 0, &amount_param, 0, &ind_amt_param);

        SQLExecute(hstmt);

        SQLINTEGER u_id = 0, u_age = 0, o_id = 0, o_uid = 0;
        double o_amount = 0.0;
        char u_name[256] = {0}, u_email[256] = {0}, o_status[64] = {0};
        SQLLEN inds[8] = {0};

        SQLBindCol(hstmt, 1, SQL_C_SLONG, &u_id, 0, &inds[0]);
        SQLBindCol(hstmt, 2, SQL_C_CHAR, u_name, sizeof(u_name), &inds[1]);
        SQLBindCol(hstmt, 3, SQL_C_CHAR, u_email, sizeof(u_email), &inds[2]);
        SQLBindCol(hstmt, 4, SQL_C_SLONG, &u_age, 0, &inds[3]);
        SQLBindCol(hstmt, 5, SQL_C_SLONG, &o_id, 0, &inds[4]);
        SQLBindCol(hstmt, 6, SQL_C_SLONG, &o_uid, 0, &inds[5]);
        SQLBindCol(hstmt, 7, SQL_C_DOUBLE, &o_amount, 0, &inds[6]);
        SQLBindCol(hstmt, 8, SQL_C_CHAR, o_status, sizeof(o_status), &inds[7]);

        std::vector<std::pair<BenchUser, BenchOrder>> results;
        while (SQLFetch(hstmt) == SQL_SUCCESS) {
            BenchUser u;
            u.id = static_cast<int>(u_id);
            u.name = std::string(u_name, inds[1] > 0 ? static_cast<size_t>(inds[1]) : 0);
            if (inds[2] == SQL_NULL_DATA) {
                u.email = std::nullopt;
            } else {
                u.email = std::string(u_email, inds[2] > 0 ? static_cast<size_t>(inds[2]) : 0);
            }
            u.age = static_cast<int>(u_age);

            BenchOrder o;
            o.id = static_cast<int>(o_id);
            o.user_id = static_cast<int>(o_uid);
            o.amount = o_amount;
            o.status = std::string(o_status, inds[7] > 0 ? static_cast<size_t>(inds[7]) : 0);

            results.emplace_back(std::move(u), std::move(o));
        }
        SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
        return results;
    }

    size_t update_bulk_raw() {
        SQLHSTMT hstmt = SQL_NULL_HSTMT;
        SQLAllocHandle(SQL_HANDLE_STMT, hdbc_, &hstmt);
        SQLPrepareA(hstmt, reinterpret_cast<SQLCHAR*>(const_cast<char*>(
            "UPDATE `bench_users` SET `age` = ? "
            "WHERE `age` >= ? AND `age` <= ?"
        )), SQL_NTS);

        SQLINTEGER set_age = 99, min_age = 25, max_age = 40;
        SQLLEN ind1 = 0, ind2 = 0, ind3 = 0;
        SQLBindParameter(hstmt, 1, SQL_PARAM_INPUT, SQL_C_SLONG, SQL_INTEGER, 0, 0, &set_age, 0, &ind1);
        SQLBindParameter(hstmt, 2, SQL_PARAM_INPUT, SQL_C_SLONG, SQL_INTEGER, 0, 0, &min_age, 0, &ind2);
        SQLBindParameter(hstmt, 3, SQL_PARAM_INPUT, SQL_C_SLONG, SQL_INTEGER, 0, 0, &max_age, 0, &ind3);

        SQLExecute(hstmt);

        SQLLEN rows_affected = 0;
        SQLRowCount(hstmt, &rows_affected);
        SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
        return static_cast<size_t>(rows_affected);
    }

    size_t delete_bulk_raw() {
        SQLHSTMT hstmt = SQL_NULL_HSTMT;
        SQLAllocHandle(SQL_HANDLE_STMT, hdbc_, &hstmt);
        SQLPrepareA(hstmt, reinterpret_cast<SQLCHAR*>(const_cast<char*>(
            "DELETE FROM `bench_orders` WHERE `status` = ?"
        )), SQL_NTS);

        char status_buf[] = "cancelled";
        SQLLEN ind_status = SQL_NTS;
        SQLBindParameter(hstmt, 1, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_VARCHAR, 63, 0, status_buf, sizeof(status_buf), &ind_status);

        SQLExecute(hstmt);

        SQLLEN rows_affected = 0;
        SQLRowCount(hstmt, &rows_affected);
        SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
        return static_cast<size_t>(rows_affected);
    }

    SQLHENV henv_ = SQL_NULL_HENV;
    SQLHDBC hdbc_ = SQL_NULL_HDBC;
    bool connected_ = false;
};

// ── ODBC InsertBulk ──────────────────────────────────────────────────

BENCHMARK_DEFINE_F(OdbcFixture, InsertBulk)(benchmark::State& state) {
    if (!connected_) return;
    auto users = generate_users(static_cast<size_t>(state.range(0)));
    for (auto _ : state) {
        state.PauseTiming();
        clear_tables();
        state.ResumeTiming();

        insert_users_bulk(users);
    }
    state.SetItemsProcessed(state.iterations() * state.range(0));
}
BENCHMARK_REGISTER_F(OdbcFixture, InsertBulk) BENCH_ARGS;

// ── ODBC SelectAll ───────────────────────────────────────────────────

BENCHMARK_DEFINE_F(OdbcFixture, SelectAll)(benchmark::State& state) {
    if (!connected_) return;
    clear_tables();
    seed_users(state.range(0));
    for (auto _ : state) {
        auto results = select_all_raw();
        benchmark::DoNotOptimize(results.size());
    }
    state.SetItemsProcessed(state.iterations() * state.range(0));
}
BENCHMARK_REGISTER_F(OdbcFixture, SelectAll) BENCH_ARGS;

// ── ODBC SelectFiltered ──────────────────────────────────────────────

BENCHMARK_DEFINE_F(OdbcFixture, SelectFiltered)(benchmark::State& state) {
    if (!connected_) return;
    clear_tables();
    seed_users(state.range(0));
    for (auto _ : state) {
        auto results = select_filtered_raw();
        benchmark::DoNotOptimize(results.size());
    }
}
BENCHMARK_REGISTER_F(OdbcFixture, SelectFiltered) BENCH_ARGS;

// ── ODBC JoinQuery ───────────────────────────────────────────────────

BENCHMARK_DEFINE_F(OdbcFixture, JoinQuery)(benchmark::State& state) {
    if (!connected_) return;
    clear_tables();
    seed_all(state.range(0));
    for (auto _ : state) {
        auto results = join_query_raw();
        benchmark::DoNotOptimize(results.size());
    }
}
BENCHMARK_REGISTER_F(OdbcFixture, JoinQuery) BENCH_ARGS;

// ── ODBC UpdateBulk ──────────────────────────────────────────────────

BENCHMARK_DEFINE_F(OdbcFixture, UpdateBulk)(benchmark::State& state) {
    if (!connected_) return;
    for (auto _ : state) {
        state.PauseTiming();
        clear_tables();
        seed_users(state.range(0));
        state.ResumeTiming();

        auto n = update_bulk_raw();
        benchmark::DoNotOptimize(n);
    }
    state.SetItemsProcessed(state.iterations() * state.range(0));
}
BENCHMARK_REGISTER_F(OdbcFixture, UpdateBulk) BENCH_ARGS;

// ── ODBC DeleteBulk ──────────────────────────────────────────────────

BENCHMARK_DEFINE_F(OdbcFixture, DeleteBulk)(benchmark::State& state) {
    if (!connected_) return;
    for (auto _ : state) {
        state.PauseTiming();
        clear_tables();
        seed_all(state.range(0));
        state.ResumeTiming();

        auto n = delete_bulk_raw();
        benchmark::DoNotOptimize(n);
    }
    state.SetItemsProcessed(state.iterations() * state.range(0));
}
BENCHMARK_REGISTER_F(OdbcFixture, DeleteBulk) BENCH_ARGS;

// ############################################################################
//
//  SECTION 3: Native MySQL C API Benchmarks
//
// ############################################################################

#ifdef HAS_MYSQL

class MysqlClientFixture : public benchmark::Fixture {
public:
    void SetUp(benchmark::State& state) override {
        conn_ = mysql_init(nullptr);
        if (!conn_) {
            state.SkipWithError("mysql_init failed");
            return;
        }

        auto params = get_mysql_native_params();
        if (!mysql_real_connect(conn_, params.host.c_str(), params.user.c_str(),
                               params.password.c_str(), params.db.c_str(),
                               params.port, nullptr, 0)) {
            std::string err = mysql_error(conn_);
            mysql_close(conn_);
            conn_ = nullptr;
            state.SkipWithError(err.c_str());
            return;
        }

        ensure_tables();
        clear_tables();
    }

    void TearDown(benchmark::State&) override {
        if (conn_) {
            try { clear_tables(); } catch (...) {}
            mysql_close(conn_);
            conn_ = nullptr;
        }
    }

    void exec_sql(const char* sql) {
        if (mysql_query(conn_, sql) != 0) {
            throw std::runtime_error(mysql_error(conn_));
        }
        MYSQL_RES* res = mysql_store_result(conn_);
        if (res) mysql_free_result(res);
    }

    void ensure_tables() {
        exec_sql(
            "CREATE TABLE IF NOT EXISTS `bench_users` ("
            "`id` BIGINT AUTO_INCREMENT PRIMARY KEY, "
            "`name` VARCHAR(255) NOT NULL, "
            "`email` VARCHAR(255), "
            "`age` INT NOT NULL)"
        );
        exec_sql(
            "CREATE TABLE IF NOT EXISTS `bench_orders` ("
            "`id` BIGINT AUTO_INCREMENT PRIMARY KEY, "
            "`user_id` INT NOT NULL, "
            "`amount` DOUBLE NOT NULL, "
            "`status` VARCHAR(64) NOT NULL)"
        );
    }

    void clear_tables() {
        try {
            exec_sql("TRUNCATE TABLE `bench_orders`");
            exec_sql("TRUNCATE TABLE `bench_users`");
        } catch (...) {
            try { exec_sql("DELETE FROM `bench_orders`"); } catch (...) {}
            try { exec_sql("DELETE FROM `bench_users`"); } catch (...) {}
        }
    }

    void insert_users_bulk(const std::vector<BenchUser>& users) {
        if (users.empty()) return;
        exec_sql("START TRANSACTION");

        constexpr size_t CHUNK = 5000;
        for (size_t offset = 0; offset < users.size(); offset += CHUNK) {
            size_t batch = std::min(CHUNK, users.size() - offset);
            std::string sql = "INSERT INTO `bench_users` (`name`, `email`, `age`) VALUES ";
            sql.reserve(sql.size() + batch * 64);

            for (size_t i = 0; i < batch; ++i) {
                if (i > 0) sql += ",";
                const auto& u = users[offset + i];
                sql += "('";
                std::vector<char> escaped_name(u.name.size() * 2 + 1);
                unsigned long esc_len = mysql_real_escape_string(conn_, escaped_name.data(), u.name.data(), static_cast<unsigned long>(u.name.size()));
                sql.append(escaped_name.data(), esc_len);
                sql += "',";
                if (u.email.has_value()) {
                    sql += "'";
                    std::vector<char> escaped_email(u.email->size() * 2 + 1);
                    unsigned long email_esc_len = mysql_real_escape_string(conn_, escaped_email.data(), u.email->data(), static_cast<unsigned long>(u.email->size()));
                    sql.append(escaped_email.data(), email_esc_len);
                    sql += "',";
                } else {
                    sql += "NULL,";
                }
                sql += std::to_string(u.age);
                sql += ")";
            }
            exec_sql(sql.c_str());
        }

        exec_sql("COMMIT");
    }

    void insert_orders_bulk(const std::vector<BenchOrder>& orders) {
        if (orders.empty()) return;
        exec_sql("START TRANSACTION");

        constexpr size_t CHUNK = 5000;
        for (size_t offset = 0; offset < orders.size(); offset += CHUNK) {
            size_t batch = std::min(CHUNK, orders.size() - offset);
            std::string sql = "INSERT INTO `bench_orders` (`user_id`, `amount`, `status`) VALUES ";
            sql.reserve(sql.size() + batch * 64);

            for (size_t i = 0; i < batch; ++i) {
                if (i > 0) sql += ",";
                const auto& o = orders[offset + i];
                sql += "(";
                sql += std::to_string(o.user_id);
                sql += ",";
                char amt_buf[32];
                snprintf(amt_buf, sizeof(amt_buf), "%.2f", o.amount);
                sql += amt_buf;
                sql += ",'";
                std::vector<char> esc_status(o.status.size() * 2 + 1);
                unsigned long esc_len = mysql_real_escape_string(conn_, esc_status.data(), o.status.data(), static_cast<unsigned long>(o.status.size()));
                sql.append(esc_status.data(), esc_len);
                sql += "')";
            }
            exec_sql(sql.c_str());
        }

        exec_sql("COMMIT");
    }

    void seed_users(int64_t n) {
        auto users = generate_users(static_cast<size_t>(n));
        insert_users_bulk(users);
    }

    void seed_all(int64_t n) {
        seed_users(n);
        auto orders = generate_orders(static_cast<size_t>(n));
        insert_orders_bulk(orders);
    }

    std::vector<BenchUser> select_all_mysql() {
        if (mysql_query(conn_, "SELECT `id`, `name`, `email`, `age` FROM `bench_users`") != 0) {
            throw std::runtime_error(mysql_error(conn_));
        }
        MYSQL_RES* res = mysql_store_result(conn_);
        if (!res) throw std::runtime_error(mysql_error(conn_));

        int nrows = static_cast<int>(mysql_num_rows(res));
        std::vector<BenchUser> results;
        results.reserve(static_cast<size_t>(nrows));

        MYSQL_ROW row;
        while ((row = mysql_fetch_row(res))) {
            unsigned long* lengths = mysql_fetch_lengths(res);
            BenchUser u;
            u.id = row[0] ? std::atoi(row[0]) : 0;
            u.name = row[1] ? std::string(row[1], lengths[1]) : "";
            if (row[2]) {
                u.email = std::string(row[2], lengths[2]);
            } else {
                u.email = std::nullopt;
            }
            u.age = row[3] ? std::atoi(row[3]) : 0;
            results.push_back(std::move(u));
        }
        mysql_free_result(res);
        return results;
    }

    std::vector<BenchUser> select_filtered_mysql() {
        MYSQL_STMT* stmt = mysql_stmt_init(conn_);
        const char* sql = "SELECT `id`, `name`, `email`, `age` FROM `bench_users` "
                          "WHERE `age` > ? AND `email` IS NOT NULL "
                          "ORDER BY `age`";
        if (mysql_stmt_prepare(stmt, sql, static_cast<unsigned long>(std::strlen(sql))) != 0) {
            std::string err = mysql_stmt_error(stmt);
            mysql_stmt_close(stmt);
            throw std::runtime_error(err);
        }

        int age_param = 30;
        MYSQL_BIND param_bind[1];
        std::memset(param_bind, 0, sizeof(param_bind));
        param_bind[0].buffer_type = MYSQL_TYPE_LONG;
        param_bind[0].buffer = &age_param;
        param_bind[0].is_null = nullptr;
        param_bind[0].length = nullptr;

        mysql_stmt_bind_param(stmt, param_bind);
        mysql_stmt_execute(stmt);

        MYSQL_BIND res_bind[4];
        std::memset(res_bind, 0, sizeof(res_bind));

        int id_val = 0, age_val = 0;
        char name_buf[256] = {0}, email_buf[256] = {0};
        unsigned long name_len = 0, email_len = 0;
        my_bool is_null[4] = {0};


        res_bind[0].buffer_type = MYSQL_TYPE_LONG;
        res_bind[0].buffer = &id_val;
        res_bind[0].is_null = &is_null[0];

        res_bind[1].buffer_type = MYSQL_TYPE_STRING;
        res_bind[1].buffer = name_buf;
        res_bind[1].buffer_length = sizeof(name_buf);
        res_bind[1].length = &name_len;
        res_bind[1].is_null = &is_null[1];

        res_bind[2].buffer_type = MYSQL_TYPE_STRING;
        res_bind[2].buffer = email_buf;
        res_bind[2].buffer_length = sizeof(email_buf);
        res_bind[2].length = &email_len;
        res_bind[2].is_null = &is_null[2];

        res_bind[3].buffer_type = MYSQL_TYPE_LONG;
        res_bind[3].buffer = &age_val;
        res_bind[3].is_null = &is_null[3];

        mysql_stmt_bind_result(stmt, res_bind);
        mysql_stmt_store_result(stmt);

        std::vector<BenchUser> results;
        while (mysql_stmt_fetch(stmt) == 0) {
            BenchUser u;
            u.id = id_val;
            u.name = is_null[1] ? "" : std::string(name_buf, name_len);
            if (is_null[2]) {
                u.email = std::nullopt;
            } else {
                u.email = std::string(email_buf, email_len);
            }
            u.age = age_val;
            results.push_back(std::move(u));
        }

        mysql_stmt_close(stmt);
        return results;
    }

    std::vector<std::pair<BenchUser, BenchOrder>> join_query_mysql() {
        MYSQL_STMT* stmt = mysql_stmt_init(conn_);
        const char* sql = "SELECT u.`id`, u.`name`, u.`email`, u.`age`, "
                          "o.`id`, o.`user_id`, o.`amount`, o.`status` "
                          "FROM `bench_users` u "
                          "INNER JOIN `bench_orders` o ON u.`id` = o.`user_id` "
                          "WHERE o.`amount` > ?";
        if (mysql_stmt_prepare(stmt, sql, static_cast<unsigned long>(std::strlen(sql))) != 0) {
            std::string err = mysql_stmt_error(stmt);
            mysql_stmt_close(stmt);
            throw std::runtime_error(err);
        }

        double amount_param = 100.0;
        MYSQL_BIND param_bind[1];
        std::memset(param_bind, 0, sizeof(param_bind));
        param_bind[0].buffer_type = MYSQL_TYPE_DOUBLE;
        param_bind[0].buffer = &amount_param;

        mysql_stmt_bind_param(stmt, param_bind);
        mysql_stmt_execute(stmt);

        MYSQL_BIND res_bind[8];
        std::memset(res_bind, 0, sizeof(res_bind));

        int u_id = 0, u_age = 0, o_id = 0, o_uid = 0;
        double o_amount = 0.0;
        char u_name[256] = {0}, u_email[256] = {0}, o_status[64] = {0};
        unsigned long u_name_len = 0, u_email_len = 0, o_status_len = 0;
        my_bool is_null[8] = {0};


        res_bind[0].buffer_type = MYSQL_TYPE_LONG;
        res_bind[0].buffer = &u_id;
        res_bind[0].is_null = &is_null[0];

        res_bind[1].buffer_type = MYSQL_TYPE_STRING;
        res_bind[1].buffer = u_name;
        res_bind[1].buffer_length = sizeof(u_name);
        res_bind[1].length = &u_name_len;
        res_bind[1].is_null = &is_null[1];

        res_bind[2].buffer_type = MYSQL_TYPE_STRING;
        res_bind[2].buffer = u_email;
        res_bind[2].buffer_length = sizeof(u_email);
        res_bind[2].length = &u_email_len;
        res_bind[2].is_null = &is_null[2];

        res_bind[3].buffer_type = MYSQL_TYPE_LONG;
        res_bind[3].buffer = &u_age;
        res_bind[3].is_null = &is_null[3];

        res_bind[4].buffer_type = MYSQL_TYPE_LONG;
        res_bind[4].buffer = &o_id;
        res_bind[4].is_null = &is_null[4];

        res_bind[5].buffer_type = MYSQL_TYPE_LONG;
        res_bind[5].buffer = &o_uid;
        res_bind[5].is_null = &is_null[5];

        res_bind[6].buffer_type = MYSQL_TYPE_DOUBLE;
        res_bind[6].buffer = &o_amount;
        res_bind[6].is_null = &is_null[6];

        res_bind[7].buffer_type = MYSQL_TYPE_STRING;
        res_bind[7].buffer = o_status;
        res_bind[7].buffer_length = sizeof(o_status);
        res_bind[7].length = &o_status_len;
        res_bind[7].is_null = &is_null[7];

        mysql_stmt_bind_result(stmt, res_bind);
        mysql_stmt_store_result(stmt);

        std::vector<std::pair<BenchUser, BenchOrder>> results;
        while (mysql_stmt_fetch(stmt) == 0) {
            BenchUser u;
            u.id = u_id;
            u.name = is_null[1] ? "" : std::string(u_name, u_name_len);
            if (is_null[2]) {
                u.email = std::nullopt;
            } else {
                u.email = std::string(u_email, u_email_len);
            }
            u.age = u_age;

            BenchOrder o;
            o.id = o_id;
            o.user_id = o_uid;
            o.amount = o_amount;
            o.status = is_null[7] ? "" : std::string(o_status, o_status_len);

            results.emplace_back(std::move(u), std::move(o));
        }

        mysql_stmt_close(stmt);
        return results;
    }

    size_t update_bulk_mysql() {
        MYSQL_STMT* stmt = mysql_stmt_init(conn_);
        const char* sql = "UPDATE `bench_users` SET `age` = ? "
                          "WHERE `age` >= ? AND `age` <= ?";
        if (mysql_stmt_prepare(stmt, sql, static_cast<unsigned long>(std::strlen(sql))) != 0) {
            std::string err = mysql_stmt_error(stmt);
            mysql_stmt_close(stmt);
            throw std::runtime_error(err);
        }

        int set_age = 99, min_age = 25, max_age = 40;
        MYSQL_BIND params[3];
        std::memset(params, 0, sizeof(params));

        params[0].buffer_type = MYSQL_TYPE_LONG;
        params[0].buffer = &set_age;
        params[1].buffer_type = MYSQL_TYPE_LONG;
        params[1].buffer = &min_age;
        params[2].buffer_type = MYSQL_TYPE_LONG;
        params[2].buffer = &max_age;

        mysql_stmt_bind_param(stmt, params);
        mysql_stmt_execute(stmt);

        size_t affected = static_cast<size_t>(mysql_stmt_affected_rows(stmt));
        mysql_stmt_close(stmt);
        return affected;
    }

    size_t delete_bulk_mysql() {
        MYSQL_STMT* stmt = mysql_stmt_init(conn_);
        const char* sql = "DELETE FROM `bench_orders` WHERE `status` = ?";
        if (mysql_stmt_prepare(stmt, sql, static_cast<unsigned long>(std::strlen(sql))) != 0) {
            std::string err = mysql_stmt_error(stmt);
            mysql_stmt_close(stmt);
            throw std::runtime_error(err);
        }

        char status_buf[] = "cancelled";
        unsigned long status_len = static_cast<unsigned long>(std::strlen(status_buf));
        MYSQL_BIND params[1];
        std::memset(params, 0, sizeof(params));

        params[0].buffer_type = MYSQL_TYPE_STRING;
        params[0].buffer = status_buf;
        params[0].buffer_length = sizeof(status_buf);
        params[0].length = &status_len;

        mysql_stmt_bind_param(stmt, params);
        mysql_stmt_execute(stmt);

        size_t affected = static_cast<size_t>(mysql_stmt_affected_rows(stmt));
        mysql_stmt_close(stmt);
        return affected;
    }

    MYSQL* conn_ = nullptr;
};

// ── MySQL Client InsertBulk ──────────────────────────────────────────

BENCHMARK_DEFINE_F(MysqlClientFixture, InsertBulk)(benchmark::State& state) {
    if (!conn_) return;
    auto users = generate_users(static_cast<size_t>(state.range(0)));
    for (auto _ : state) {
        state.PauseTiming();
        clear_tables();
        state.ResumeTiming();

        insert_users_bulk(users);
    }
    state.SetItemsProcessed(state.iterations() * state.range(0));
}
BENCHMARK_REGISTER_F(MysqlClientFixture, InsertBulk) BENCH_ARGS;

// ── MySQL Client SelectAll ───────────────────────────────────────────

BENCHMARK_DEFINE_F(MysqlClientFixture, SelectAll)(benchmark::State& state) {
    if (!conn_) return;
    clear_tables();
    seed_users(state.range(0));
    for (auto _ : state) {
        auto results = select_all_mysql();
        benchmark::DoNotOptimize(results.size());
    }
    state.SetItemsProcessed(state.iterations() * state.range(0));
}
BENCHMARK_REGISTER_F(MysqlClientFixture, SelectAll) BENCH_ARGS;

// ── MySQL Client SelectFiltered ──────────────────────────────────────

BENCHMARK_DEFINE_F(MysqlClientFixture, SelectFiltered)(benchmark::State& state) {
    if (!conn_) return;
    clear_tables();
    seed_users(state.range(0));
    for (auto _ : state) {
        auto results = select_filtered_mysql();
        benchmark::DoNotOptimize(results.size());
    }
}
BENCHMARK_REGISTER_F(MysqlClientFixture, SelectFiltered) BENCH_ARGS;

// ── MySQL Client JoinQuery ───────────────────────────────────────────

BENCHMARK_DEFINE_F(MysqlClientFixture, JoinQuery)(benchmark::State& state) {
    if (!conn_) return;
    clear_tables();
    seed_all(state.range(0));
    for (auto _ : state) {
        auto results = join_query_mysql();
        benchmark::DoNotOptimize(results.size());
    }
}
BENCHMARK_REGISTER_F(MysqlClientFixture, JoinQuery) BENCH_ARGS;

// ── MySQL Client UpdateBulk ──────────────────────────────────────────

BENCHMARK_DEFINE_F(MysqlClientFixture, UpdateBulk)(benchmark::State& state) {
    if (!conn_) return;
    for (auto _ : state) {
        state.PauseTiming();
        clear_tables();
        seed_users(state.range(0));
        state.ResumeTiming();

        auto n = update_bulk_mysql();
        benchmark::DoNotOptimize(n);
    }
    state.SetItemsProcessed(state.iterations() * state.range(0));
}
BENCHMARK_REGISTER_F(MysqlClientFixture, UpdateBulk) BENCH_ARGS;

// ── MySQL Client DeleteBulk ──────────────────────────────────────────

BENCHMARK_DEFINE_F(MysqlClientFixture, DeleteBulk)(benchmark::State& state) {
    if (!conn_) return;
    for (auto _ : state) {
        state.PauseTiming();
        clear_tables();
        seed_all(state.range(0));
        state.ResumeTiming();

        auto n = delete_bulk_mysql();
        benchmark::DoNotOptimize(n);
    }
    state.SetItemsProcessed(state.iterations() * state.range(0));
}
BENCHMARK_REGISTER_F(MysqlClientFixture, DeleteBulk) BENCH_ARGS;

#endif // HAS_MYSQL
