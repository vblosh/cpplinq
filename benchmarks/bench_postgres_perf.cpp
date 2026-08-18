// ============================================================================
// PostgreSQL Performance Benchmarks: libpq vs Raw ODBC vs cpplinq ORM
// Scales: 1,000, 10,000, 100,000 rows
// ============================================================================
//
// Compares three data access layers against the same PostgreSQL instance:
//   1. Native libpq   — PostgreSQL C client library (baseline)
//   2. Raw ODBC       — Direct ODBC 3.x API calls (no ORM overhead)
//   3. cpplinq ORM    — Full ORM: AST → SqlGenerator → ODBC → RowMapper
//
// Environment variables:
//   CPPLINQ_POSTGRES_ODBC  — ODBC DSN or connection string (for raw ODBC + cpplinq)
//   CPPLINQ_POSTGRES_LIBPQ — libpq connection string (for native libpq benchmarks)
//
// ============================================================================

#include <benchmark/benchmark.h>
#include "cpplinq/cpplinq.hpp"

#ifdef HAS_LIBPQ
#include <libpq-fe.h>
#endif

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
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
    const char* env = std::getenv("CPPLINQ_POSTGRES_ODBC");
    return (env && env[0] != '\0') ? std::string(env) : "";
}

static std::string get_libpq_conn_str() {
    const char* env = std::getenv("CPPLINQ_POSTGRES_LIBPQ");
    return (env && env[0] != '\0') ? std::string(env) : "";
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
        if (conn_str.empty()) {
            state.SkipWithError("CPPLINQ_POSTGRES_ODBC not set");
            return;
        }
        try {
            db = std::make_unique<DbContext<postgres>>(conn_str);
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
            db->execute_raw("TRUNCATE TABLE \"bench_orders\", \"bench_users\" RESTART IDENTITY CASCADE");
        } catch (...) {
            try { db->execute_raw("DELETE FROM \"bench_orders\""); } catch (...) {}
            try { db->execute_raw("DELETE FROM \"bench_users\""); } catch (...) {}
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

    std::unique_ptr<DbContext<postgres>> db;
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
            .limit(100)
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
            .limit(200)
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
        if (conn_str.empty()) {
            state.SkipWithError("CPPLINQ_POSTGRES_ODBC not set");
            return;
        }
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
            "CREATE TABLE IF NOT EXISTS \"bench_users\" ("
            "\"id\" BIGSERIAL PRIMARY KEY, "
            "\"name\" TEXT NOT NULL, "
            "\"email\" TEXT, "
            "\"age\" INTEGER NOT NULL)"
        );
        exec_sql(
            "CREATE TABLE IF NOT EXISTS \"bench_orders\" ("
            "\"id\" BIGSERIAL PRIMARY KEY, "
            "\"user_id\" INTEGER NOT NULL, "
            "\"amount\" DOUBLE PRECISION NOT NULL, "
            "\"status\" TEXT NOT NULL)"
        );
    }

    void clear_tables() {
        try {
            exec_sql("TRUNCATE TABLE \"bench_orders\", \"bench_users\" RESTART IDENTITY CASCADE");
        } catch (...) {
            try { exec_sql("DELETE FROM \"bench_orders\""); } catch (...) {}
            try { exec_sql("DELETE FROM \"bench_users\""); } catch (...) {}
        }
    }

    void insert_users_bulk(const std::vector<BenchUser>& users) {
        if (users.empty()) return;
        exec_sql("BEGIN");

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
                "INSERT INTO \"bench_users\" (\"name\", \"email\", \"age\") VALUES (?, ?, ?)"
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
        exec_sql("BEGIN");

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
                "INSERT INTO \"bench_orders\" (\"user_id\", \"amount\", \"status\") VALUES (?, ?, ?)"
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

    size_t select_all_raw() {
        SQLHSTMT hstmt = SQL_NULL_HSTMT;
        SQLAllocHandle(SQL_HANDLE_STMT, hdbc_, &hstmt);
        SQLExecDirectA(hstmt, reinterpret_cast<SQLCHAR*>(const_cast<char*>(
            "SELECT \"id\", \"name\", \"email\", \"age\" FROM \"bench_users\""
        )), SQL_NTS);

        SQLINTEGER id_val = 0, age_val = 0;
        char name_buf[256] = {0}, email_buf[256] = {0};
        SQLLEN ind_id = 0, ind_name = 0, ind_email = 0, ind_age = 0;

        SQLBindCol(hstmt, 1, SQL_C_SLONG, &id_val, 0, &ind_id);
        SQLBindCol(hstmt, 2, SQL_C_CHAR, name_buf, sizeof(name_buf), &ind_name);
        SQLBindCol(hstmt, 3, SQL_C_CHAR, email_buf, sizeof(email_buf), &ind_email);
        SQLBindCol(hstmt, 4, SQL_C_SLONG, &age_val, 0, &ind_age);

        size_t count = 0;
        while (SQLFetch(hstmt) == SQL_SUCCESS) {
            benchmark::DoNotOptimize(id_val);
            benchmark::DoNotOptimize(name_buf[0]);
            benchmark::DoNotOptimize(age_val);
            ++count;
        }
        SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
        return count;
    }

    size_t select_filtered_raw() {
        SQLHSTMT hstmt = SQL_NULL_HSTMT;
        SQLAllocHandle(SQL_HANDLE_STMT, hdbc_, &hstmt);
        SQLExecDirectA(hstmt, reinterpret_cast<SQLCHAR*>(const_cast<char*>(
            "SELECT \"id\", \"name\", \"email\", \"age\" FROM \"bench_users\" "
            "WHERE \"age\" > 30 AND \"email\" IS NOT NULL "
            "ORDER BY \"age\" LIMIT 100"
        )), SQL_NTS);

        SQLINTEGER id_val = 0, age_val = 0;
        char name_buf[256] = {0}, email_buf[256] = {0};
        SQLLEN ind_id = 0, ind_name = 0, ind_email = 0, ind_age = 0;

        SQLBindCol(hstmt, 1, SQL_C_SLONG, &id_val, 0, &ind_id);
        SQLBindCol(hstmt, 2, SQL_C_CHAR, name_buf, sizeof(name_buf), &ind_name);
        SQLBindCol(hstmt, 3, SQL_C_CHAR, email_buf, sizeof(email_buf), &ind_email);
        SQLBindCol(hstmt, 4, SQL_C_SLONG, &age_val, 0, &ind_age);

        size_t count = 0;
        while (SQLFetch(hstmt) == SQL_SUCCESS) {
            benchmark::DoNotOptimize(id_val);
            benchmark::DoNotOptimize(name_buf[0]);
            benchmark::DoNotOptimize(age_val);
            ++count;
        }
        SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
        return count;
    }

    size_t join_query_raw() {
        SQLHSTMT hstmt = SQL_NULL_HSTMT;
        SQLAllocHandle(SQL_HANDLE_STMT, hdbc_, &hstmt);
        SQLExecDirectA(hstmt, reinterpret_cast<SQLCHAR*>(const_cast<char*>(
            "SELECT u.\"id\", u.\"name\", u.\"email\", u.\"age\", "
            "o.\"id\", o.\"user_id\", o.\"amount\", o.\"status\" "
            "FROM \"bench_users\" u "
            "INNER JOIN \"bench_orders\" o ON u.\"id\" = o.\"user_id\" "
            "WHERE o.\"amount\" > 100.0 "
            "LIMIT 200"
        )), SQL_NTS);

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

        size_t count = 0;
        while (SQLFetch(hstmt) == SQL_SUCCESS) {
            benchmark::DoNotOptimize(u_id);
            benchmark::DoNotOptimize(o_amount);
            ++count;
        }
        SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
        return count;
    }

    size_t update_bulk_raw() {
        SQLHSTMT hstmt = SQL_NULL_HSTMT;
        SQLAllocHandle(SQL_HANDLE_STMT, hdbc_, &hstmt);
        SQLExecDirectA(hstmt, reinterpret_cast<SQLCHAR*>(const_cast<char*>(
            "UPDATE \"bench_users\" SET \"age\" = 99 "
            "WHERE \"age\" >= 25 AND \"age\" <= 40"
        )), SQL_NTS);

        SQLLEN rows_affected = 0;
        SQLRowCount(hstmt, &rows_affected);
        SQLFreeHandle(SQL_HANDLE_STMT, hstmt);
        return static_cast<size_t>(rows_affected);
    }

    size_t delete_bulk_raw() {
        SQLHSTMT hstmt = SQL_NULL_HSTMT;
        SQLAllocHandle(SQL_HANDLE_STMT, hdbc_, &hstmt);
        SQLExecDirectA(hstmt, reinterpret_cast<SQLCHAR*>(const_cast<char*>(
            "DELETE FROM \"bench_orders\" WHERE \"status\" = 'cancelled'"
        )), SQL_NTS);

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
        auto n = select_all_raw();
        benchmark::DoNotOptimize(n);
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
        auto n = select_filtered_raw();
        benchmark::DoNotOptimize(n);
    }
}
BENCHMARK_REGISTER_F(OdbcFixture, SelectFiltered) BENCH_ARGS;

// ── ODBC JoinQuery ───────────────────────────────────────────────────

BENCHMARK_DEFINE_F(OdbcFixture, JoinQuery)(benchmark::State& state) {
    if (!connected_) return;
    clear_tables();
    seed_all(state.range(0));
    for (auto _ : state) {
        auto n = join_query_raw();
        benchmark::DoNotOptimize(n);
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
//  SECTION 3: Native libpq Benchmarks
//
// ############################################################################

#ifdef HAS_LIBPQ

class LibpqFixture : public benchmark::Fixture {
public:
    void SetUp(benchmark::State& state) override {
        auto conn_str = get_libpq_conn_str();
        if (conn_str.empty()) {
            state.SkipWithError("CPPLINQ_POSTGRES_LIBPQ not set");
            return;
        }
        conn_ = PQconnectdb(conn_str.c_str());
        if (PQstatus(conn_) != CONNECTION_OK) {
            std::string err = PQerrorMessage(conn_);
            PQfinish(conn_);
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
            PQfinish(conn_);
            conn_ = nullptr;
        }
    }

    void exec_sql(const char* sql) {
        PGresult* res = PQexec(conn_, sql);
        if (PQresultStatus(res) != PGRES_COMMAND_OK &&
            PQresultStatus(res) != PGRES_TUPLES_OK) {
            std::string err = PQerrorMessage(conn_);
            PQclear(res);
            throw std::runtime_error(err);
        }
        PQclear(res);
    }

    void ensure_tables() {
        exec_sql(
            "CREATE TABLE IF NOT EXISTS \"bench_users\" ("
            "\"id\" BIGSERIAL PRIMARY KEY, "
            "\"name\" TEXT NOT NULL, "
            "\"email\" TEXT, "
            "\"age\" INTEGER NOT NULL)"
        );
        exec_sql(
            "CREATE TABLE IF NOT EXISTS \"bench_orders\" ("
            "\"id\" BIGSERIAL PRIMARY KEY, "
            "\"user_id\" INTEGER NOT NULL, "
            "\"amount\" DOUBLE PRECISION NOT NULL, "
            "\"status\" TEXT NOT NULL)"
        );
    }

    void clear_tables() {
        try {
            exec_sql("TRUNCATE TABLE \"bench_orders\", \"bench_users\" RESTART IDENTITY CASCADE");
        } catch (...) {
            try { exec_sql("DELETE FROM \"bench_orders\""); } catch (...) {}
            try { exec_sql("DELETE FROM \"bench_users\""); } catch (...) {}
        }
    }

    void insert_users_copy(const std::vector<BenchUser>& users) {
        PGresult* res = PQexec(conn_,
            "COPY \"bench_users\" (\"name\", \"email\", \"age\") FROM STDIN WITH (FORMAT text)");
        PQclear(res);

        constexpr size_t CHUNK = 50000;
        for (size_t offset = 0; offset < users.size(); offset += CHUNK) {
            size_t batch = std::min(CHUNK, users.size() - offset);
            std::string buffer;
            buffer.reserve(batch * 48);
            for (size_t i = 0; i < batch; ++i) {
                const auto& u = users[offset + i];
                buffer += u.name;
                buffer += '\t';
                if (u.email.has_value()) {
                    buffer += *u.email;
                } else {
                    buffer += "\\N";
                }
                buffer += '\t';
                buffer += std::to_string(u.age);
                buffer += '\n';
            }
            PQputCopyData(conn_, buffer.c_str(), static_cast<int>(buffer.size()));
        }
        PQputCopyEnd(conn_, nullptr);

        res = PQgetResult(conn_);
        PQclear(res);
    }

    void insert_orders_copy(const std::vector<BenchOrder>& orders) {
        PGresult* res = PQexec(conn_,
            "COPY \"bench_orders\" (\"user_id\", \"amount\", \"status\") FROM STDIN WITH (FORMAT text)");
        PQclear(res);

        constexpr size_t CHUNK = 50000;
        for (size_t offset = 0; offset < orders.size(); offset += CHUNK) {
            size_t batch = std::min(CHUNK, orders.size() - offset);
            std::string buffer;
            buffer.reserve(batch * 48);
            for (size_t i = 0; i < batch; ++i) {
                const auto& o = orders[offset + i];
                buffer += std::to_string(o.user_id);
                buffer += '\t';
                buffer += std::to_string(o.amount);
                buffer += '\t';
                buffer += o.status;
                buffer += '\n';
            }
            PQputCopyData(conn_, buffer.c_str(), static_cast<int>(buffer.size()));
        }
        PQputCopyEnd(conn_, nullptr);

        res = PQgetResult(conn_);
        PQclear(res);
    }

    void seed_users(int64_t n) {
        auto users = generate_users(static_cast<size_t>(n));
        insert_users_copy(users);
    }

    void seed_all(int64_t n) {
        seed_users(n);
        auto orders = generate_orders(static_cast<size_t>(n));
        insert_orders_copy(orders);
    }

    size_t select_all_libpq() {
        PGresult* res = PQexec(conn_,
            "SELECT \"id\", \"name\", \"email\", \"age\" FROM \"bench_users\"");
        int nrows = PQntuples(res);
        for (int r = 0; r < nrows; ++r) {
            benchmark::DoNotOptimize(PQgetvalue(res, r, 0));
            benchmark::DoNotOptimize(PQgetvalue(res, r, 1));
            benchmark::DoNotOptimize(PQgetvalue(res, r, 2));
            benchmark::DoNotOptimize(PQgetvalue(res, r, 3));
        }
        PQclear(res);
        return static_cast<size_t>(nrows);
    }

    size_t select_filtered_libpq() {
        PGresult* res = PQexec(conn_,
            "SELECT \"id\", \"name\", \"email\", \"age\" FROM \"bench_users\" "
            "WHERE \"age\" > 30 AND \"email\" IS NOT NULL "
            "ORDER BY \"age\" LIMIT 100");
        int nrows = PQntuples(res);
        for (int r = 0; r < nrows; ++r) {
            benchmark::DoNotOptimize(PQgetvalue(res, r, 0));
            benchmark::DoNotOptimize(PQgetvalue(res, r, 1));
            benchmark::DoNotOptimize(PQgetvalue(res, r, 2));
            benchmark::DoNotOptimize(PQgetvalue(res, r, 3));
        }
        PQclear(res);
        return static_cast<size_t>(nrows);
    }

    size_t join_query_libpq() {
        PGresult* res = PQexec(conn_,
            "SELECT u.\"id\", u.\"name\", u.\"email\", u.\"age\", "
            "o.\"id\", o.\"user_id\", o.\"amount\", o.\"status\" "
            "FROM \"bench_users\" u "
            "INNER JOIN \"bench_orders\" o ON u.\"id\" = o.\"user_id\" "
            "WHERE o.\"amount\" > 100.0 "
            "LIMIT 200");
        int nrows = PQntuples(res);
        for (int r = 0; r < nrows; ++r) {
            benchmark::DoNotOptimize(PQgetvalue(res, r, 0));
            benchmark::DoNotOptimize(PQgetvalue(res, r, 6));
        }
        PQclear(res);
        return static_cast<size_t>(nrows);
    }

    size_t update_bulk_libpq() {
        PGresult* res = PQexec(conn_,
            "UPDATE \"bench_users\" SET \"age\" = 99 "
            "WHERE \"age\" >= 25 AND \"age\" <= 40");
        const char* affected = PQcmdTuples(res);
        size_t n = affected ? static_cast<size_t>(std::atoll(affected)) : 0;
        PQclear(res);
        return n;
    }

    size_t delete_bulk_libpq() {
        PGresult* res = PQexec(conn_,
            "DELETE FROM \"bench_orders\" WHERE \"status\" = 'cancelled'");
        const char* affected = PQcmdTuples(res);
        size_t n = affected ? static_cast<size_t>(std::atoll(affected)) : 0;
        PQclear(res);
        return n;
    }

    PGconn* conn_ = nullptr;
};

// ── libpq InsertCopy (COPY protocol) ─────────────────────────────────

BENCHMARK_DEFINE_F(LibpqFixture, InsertCopy)(benchmark::State& state) {
    if (!conn_) return;
    auto users = generate_users(static_cast<size_t>(state.range(0)));
    for (auto _ : state) {
        state.PauseTiming();
        clear_tables();
        state.ResumeTiming();

        insert_users_copy(users);
    }
    state.SetItemsProcessed(state.iterations() * state.range(0));
}
BENCHMARK_REGISTER_F(LibpqFixture, InsertCopy) BENCH_ARGS;

// ── libpq SelectAll ──────────────────────────────────────────────────

BENCHMARK_DEFINE_F(LibpqFixture, SelectAll)(benchmark::State& state) {
    if (!conn_) return;
    clear_tables();
    seed_users(state.range(0));
    for (auto _ : state) {
        auto n = select_all_libpq();
        benchmark::DoNotOptimize(n);
    }
    state.SetItemsProcessed(state.iterations() * state.range(0));
}
BENCHMARK_REGISTER_F(LibpqFixture, SelectAll) BENCH_ARGS;

// ── libpq SelectFiltered ─────────────────────────────────────────────

BENCHMARK_DEFINE_F(LibpqFixture, SelectFiltered)(benchmark::State& state) {
    if (!conn_) return;
    clear_tables();
    seed_users(state.range(0));
    for (auto _ : state) {
        auto n = select_filtered_libpq();
        benchmark::DoNotOptimize(n);
    }
}
BENCHMARK_REGISTER_F(LibpqFixture, SelectFiltered) BENCH_ARGS;

// ── libpq JoinQuery ──────────────────────────────────────────────────

BENCHMARK_DEFINE_F(LibpqFixture, JoinQuery)(benchmark::State& state) {
    if (!conn_) return;
    clear_tables();
    seed_all(state.range(0));
    for (auto _ : state) {
        auto n = join_query_libpq();
        benchmark::DoNotOptimize(n);
    }
}
BENCHMARK_REGISTER_F(LibpqFixture, JoinQuery) BENCH_ARGS;

// ── libpq UpdateBulk ─────────────────────────────────────────────────

BENCHMARK_DEFINE_F(LibpqFixture, UpdateBulk)(benchmark::State& state) {
    if (!conn_) return;
    for (auto _ : state) {
        state.PauseTiming();
        clear_tables();
        seed_users(state.range(0));
        state.ResumeTiming();

        auto n = update_bulk_libpq();
        benchmark::DoNotOptimize(n);
    }
    state.SetItemsProcessed(state.iterations() * state.range(0));
}
BENCHMARK_REGISTER_F(LibpqFixture, UpdateBulk) BENCH_ARGS;

// ── libpq DeleteBulk ─────────────────────────────────────────────────

BENCHMARK_DEFINE_F(LibpqFixture, DeleteBulk)(benchmark::State& state) {
    if (!conn_) return;
    for (auto _ : state) {
        state.PauseTiming();
        clear_tables();
        seed_all(state.range(0));
        state.ResumeTiming();

        auto n = delete_bulk_libpq();
        benchmark::DoNotOptimize(n);
    }
    state.SetItemsProcessed(state.iterations() * state.range(0));
}
BENCHMARK_REGISTER_F(LibpqFixture, DeleteBulk) BENCH_ARGS;

#endif // HAS_LIBPQ
