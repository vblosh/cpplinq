#include "driver/postgres_connection.h"
#include <cstring>
#include <atomic>
#include <algorithm>
#include <cctype>

#ifdef CPPLINQ_HAS_POSTGRES

namespace cpplinq {

static std::atomic<uint64_t> s_pg_stmt_counter{1};

// ----------------------------------------------------------------------------
// Helper: convert '?' placeholders outside quotes to '$1', '$2', ...
// ----------------------------------------------------------------------------
static std::string convert_placeholders_to_pg(std::string_view sql) {
    std::string result;
    result.reserve(sql.size() + 16);
    bool in_single_quote = false;
    bool in_double_quote = false;
    int param_idx = 1;

    for (size_t i = 0; i < sql.size(); ++i) {
        char c = sql[i];
        if (c == '\'' && !in_double_quote) {
            in_single_quote = !in_single_quote;
            result.push_back(c);
        } else if (c == '"' && !in_single_quote) {
            in_double_quote = !in_double_quote;
            result.push_back(c);
        } else if (c == '?' && !in_single_quote && !in_double_quote) {
            result.push_back('$');
            result += std::to_string(param_idx++);
        } else {
            result.push_back(c);
        }
    }
    return result;
}

// ----------------------------------------------------------------------------
// PgDataReader
// ----------------------------------------------------------------------------

PgDataReader::PgDataReader(std::shared_ptr<PGresult> res)
    : res_(std::move(res))
    , num_rows_(res_ ? PQntuples(res_.get()) : 0)
    , current_row_(-1)
    , num_fields_(res_ ? PQnfields(res_.get()) : 0)
{}

bool PgDataReader::next() {
    if (!res_) return false;
    ++current_row_;
    return current_row_ < num_rows_;
}

int PgDataReader::column_count() const {
    return num_fields_;
}

bool PgDataReader::is_null(int col) const {
    if (!res_ || current_row_ < 0 || current_row_ >= num_rows_ || col < 0 || col >= num_fields_) {
        return true;
    }
    return PQgetisnull(res_.get(), current_row_, col) != 0;
}

int64_t PgDataReader::get_int64(int col) const {
    if (is_null(col)) return 0;
    return std::strtoll(PQgetvalue(res_.get(), current_row_, col), nullptr, 10);
}

uint64_t PgDataReader::get_uint64(int col) const {
    if (is_null(col)) return 0;
    return std::strtoull(PQgetvalue(res_.get(), current_row_, col), nullptr, 10);
}

double PgDataReader::get_double(int col) const {
    if (is_null(col)) return 0.0;
    return std::strtod(PQgetvalue(res_.get(), current_row_, col), nullptr);
}

std::string_view PgDataReader::get_string_view(int col) const {
    if (is_null(col)) return {};
    const char* v = PQgetvalue(res_.get(), current_row_, col);
    int len = PQgetlength(res_.get(), current_row_, col);
    return std::string_view(v, static_cast<size_t>(len));
}

std::string PgDataReader::get_string(int col) const {
    return std::string(get_string_view(col));
}

std::wstring PgDataReader::get_wstring(int col) const {
    if (is_null(col)) return {};
    return utf8_to_wstring(get_string(col));
}

bool PgDataReader::get_bool(int col) const {
    if (is_null(col)) return false;
    char c = PQgetvalue(res_.get(), current_row_, col)[0];
    return (c == 't' || c == 'T' || c == '1' || c == 'y' || c == 'Y');
}

std::vector<uint8_t> PgDataReader::get_blob(int col) const {
    if (is_null(col)) return {};
    size_t to_len = 0;
    unsigned char* unescaped = PQunescapeBytea(
        reinterpret_cast<const unsigned char*>(PQgetvalue(res_.get(), current_row_, col)),
        &to_len
    );
    if (!unescaped) return {};
    std::vector<uint8_t> result(unescaped, unescaped + to_len);
    PQfreemem(unescaped);
    return result;
}

SqlNumeric PgDataReader::get_numeric(int col) const {
    return SqlNumeric(get_string(col));
}

SqlDate PgDataReader::get_date(int col) const {
    return SqlDate::from_string(get_string(col));
}

SqlTime PgDataReader::get_time(int col) const {
    return SqlTime::from_string(get_string(col));
}

SqlTimestamp PgDataReader::get_timestamp(int col) const {
    return SqlTimestamp::from_string(get_string(col));
}

SqlInterval PgDataReader::get_interval(int col) const {
    return SqlInterval::from_string(get_string(col));
}

SqlGuid PgDataReader::get_guid(int col) const {
    return SqlGuid::from_string(get_string(col));
}

BoundValue PgDataReader::get_value(int col) const {
    if (is_null(col)) return std::monostate{};
    Oid oid = PQftype(res_.get(), col);
    switch (oid) {
        case 16: // BOOLOID
            return get_bool(col);
        case 20: // INT8OID
        case 21: // INT2OID
        case 23: // INT4OID
            return get_int64(col);
        case 700: // FLOAT4OID
        case 701: // FLOAT8OID
        case 1700: // NUMERICOID
            return get_double(col);
        case 17: // BYTEAOID
            return get_blob(col);
        default:
            return get_string(col);
    }
}

// ----------------------------------------------------------------------------
// PgPreparedStatement
// ----------------------------------------------------------------------------

PgPreparedStatement::PgPreparedStatement(PGconn* conn, std::string sql, std::string stmt_name)
    : conn_(conn)
    , sql_(convert_placeholders_to_pg(sql))
    , stmt_name_(std::move(stmt_name))
    , is_prepared_(false)
{
    if (stmt_name_.empty()) {
        stmt_name_ = "cpplinq_stmt_" + std::to_string(s_pg_stmt_counter.fetch_add(1, std::memory_order_relaxed));
    }
}

PgPreparedStatement::~PgPreparedStatement() {
    if (conn_ && is_prepared_ && PQstatus(conn_) == CONNECTION_OK) {
        std::string dealloc = "DEALLOCATE \"" + stmt_name_ + "\"";
        PGresult* res = PQexec(conn_, dealloc.c_str());
        if (res) PQclear(res);
    }
}

void PgPreparedStatement::ensure_prepared() {
    if (is_prepared_ || !conn_) return;
    PGresult* res = PQprepare(conn_, stmt_name_.c_str(), sql_.c_str(), 0, nullptr);
    if (PQresultStatus(res) != PGRES_COMMAND_OK) {
        std::string err = PQerrorMessage(conn_);
        PQclear(res);
        throw DbException("PostgreSQL prepare error: " + err);
    }
    PQclear(res);
    is_prepared_ = true;
}

void PgPreparedStatement::bind(int index, const BoundValue& value) {
    if (index < 0) throw DbException("Invalid parameter index");
    if (static_cast<size_t>(index) >= param_strings_.size()) {
        param_strings_.resize(static_cast<size_t>(index) + 1, std::nullopt);
    }

    std::visit([&](const auto& val) {
        using T = std::decay_t<decltype(val)>;
        if constexpr (std::is_same_v<T, std::monostate>) {
            param_strings_[index] = std::nullopt;
        } else if constexpr (std::is_same_v<T, int64_t>) {
            param_strings_[index] = std::to_string(val);
        } else if constexpr (std::is_same_v<T, uint64_t>) {
            param_strings_[index] = std::to_string(val);
        } else if constexpr (std::is_same_v<T, double>) {
            char buf[64];
            snprintf(buf, sizeof(buf), "%.17g", val);
            param_strings_[index] = std::string(buf);
        } else if constexpr (std::is_same_v<T, std::string>) {
            param_strings_[index] = val;
        } else if constexpr (std::is_same_v<T, std::wstring>) {
            param_strings_[index] = wstring_to_utf8(val);
        } else if constexpr (std::is_same_v<T, bool>) {
            param_strings_[index] = val ? "t" : "f";
        } else if constexpr (std::is_same_v<T, std::vector<uint8_t>>) {
            std::string hex = "\\x";
            hex.reserve(2 + val.size() * 2);
            static const char hex_chars[] = "0123456789abcdef";
            for (uint8_t b : val) {
                hex.push_back(hex_chars[b >> 4]);
                hex.push_back(hex_chars[b & 0x0f]);
            }
            param_strings_[index] = std::move(hex);
        } else if constexpr (std::is_same_v<T, SqlNumeric> ||
                              std::is_same_v<T, SqlDate> ||
                              std::is_same_v<T, SqlTime> ||
                              std::is_same_v<T, SqlTimestamp> ||
                              std::is_same_v<T, SqlInterval> ||
                              std::is_same_v<T, SqlGuid>) {
            param_strings_[index] = val.to_string();
        }
    }, value);
}

std::unique_ptr<IDataReader> PgPreparedStatement::execute_query() {
    if (!conn_) throw DbException("Connection is closed");
    ensure_prepared();

    std::vector<const char*> param_ptrs(param_strings_.size(), nullptr);
    for (size_t i = 0; i < param_strings_.size(); ++i) {
        if (param_strings_[i].has_value()) {
            param_ptrs[i] = param_strings_[i]->c_str();
        }
    }

    PGresult* res = PQexecPrepared(
        conn_,
        stmt_name_.c_str(),
        static_cast<int>(param_ptrs.size()),
        param_ptrs.data(),
        nullptr,
        nullptr,
        0
    );

    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
        std::string err = PQerrorMessage(conn_);
        PQclear(res);
        throw DbException("PostgreSQL execute query error: " + err);
    }

    return std::make_unique<PgDataReader>(
        std::shared_ptr<PGresult>(res, [](PGresult* r) { if (r) PQclear(r); })
    );
}

size_t PgPreparedStatement::execute_non_query() {
    if (!conn_) throw DbException("Connection is closed");
    ensure_prepared();

    std::vector<const char*> param_ptrs(param_strings_.size(), nullptr);
    for (size_t i = 0; i < param_strings_.size(); ++i) {
        if (param_strings_[i].has_value()) {
            param_ptrs[i] = param_strings_[i]->c_str();
        }
    }

    PGresult* res = PQexecPrepared(
        conn_,
        stmt_name_.c_str(),
        static_cast<int>(param_ptrs.size()),
        param_ptrs.data(),
        nullptr,
        nullptr,
        0
    );

    if (PQresultStatus(res) != PGRES_COMMAND_OK && PQresultStatus(res) != PGRES_TUPLES_OK) {
        std::string err = PQerrorMessage(conn_);
        PQclear(res);
        throw DbException("PostgreSQL execute non-query error: " + err);
    }

    const char* cmd_tuples = PQcmdTuples(res);
    size_t affected = (cmd_tuples && cmd_tuples[0] != '\0') ? static_cast<size_t>(std::atoll(cmd_tuples)) : 0;
    PQclear(res);
    return affected;
}

void PgPreparedStatement::reset() {
    param_strings_.clear();
}

void PgPreparedStatement::cancel() {
    if (conn_) {
        PGcancel* cancel_handle = PQgetCancel(conn_);
        if (cancel_handle) {
            char errbuf[256] = {0};
            PQcancel(cancel_handle, errbuf, sizeof(errbuf));
            PQfreeCancel(cancel_handle);
        }
    }
}

void PgPreparedStatement::set_timeout(uint32_t seconds) {
    if (conn_) {
        std::string timeout_sql = "SET statement_timeout = " + std::to_string(seconds * 1000);
        PGresult* res = PQexec(conn_, timeout_sql.c_str());
        if (res) PQclear(res);
    }
}

void PgPreparedStatement::set_stop_token(std::stop_token token) {
    stop_token_ = token;
    if (token.stop_possible() && conn_) {
        stop_cb_.emplace(token, [this]() {
            this->cancel();
        });
    }
}

// ----------------------------------------------------------------------------
// PgConnection
// ----------------------------------------------------------------------------

PgConnection::PgConnection(std::string connection_string)
    : connection_string_(std::move(connection_string))
    , conn_(nullptr)
{}

PgConnection::~PgConnection() {
    close();
}

static std::string trim_str(std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
    return std::string(s);
}

std::string PgConnection::resolve_connection_string() const {
    if (connection_string_.empty()) {
        const char* env = std::getenv("CPPLINQ_POSTGRES_LIBPQ");
        if (env && env[0] != '\0') return std::string(env);
        return "host=localhost port=5432 dbname=cppdb user=cppdb password=cppdb_password";
    }

    // Direct libpq format: URI or keyword=value with host/dbname
    if (connection_string_.rfind("postgresql://", 0) == 0 ||
        connection_string_.rfind("postgres://", 0) == 0 ||
        connection_string_.find("host=") != std::string::npos ||
        connection_string_.find("dbname=") != std::string::npos) {
        return connection_string_;
    }

    // Single word DSN (e.g. "PostgreSQL35W")
    if (connection_string_.find('=') == std::string::npos) {
        const char* env = std::getenv("CPPLINQ_POSTGRES_LIBPQ");
        if (env && env[0] != '\0') return std::string(env);
        return "host=localhost port=5432 dbname=cppdb user=cppdb password=cppdb_password";
    }

    // ODBC-style connection string parser (e.g. Server=...;Database=...;Uid=...;Pwd=...;)
    std::string host = "localhost";
    std::string port = "5432";
    std::string dbname = "cppdb";
    std::string user = "cppdb";
    std::string password = "cppdb_password";
    std::string sslmode;

    size_t start = 0;
    while (start < connection_string_.size()) {
        size_t end = connection_string_.find(';', start);
        if (end == std::string::npos) end = connection_string_.size();
        std::string_view token(connection_string_.data() + start, end - start);
        start = end + 1;

        size_t eq = token.find('=');
        if (eq != std::string_view::npos) {
            std::string key = trim_str(token.substr(0, eq));
            std::string val = trim_str(token.substr(eq + 1));
            if (val.size() >= 2 && val.front() == '{' && val.back() == '}') {
                val = val.substr(1, val.size() - 2);
            }
            std::string lower_key = key;
            std::transform(lower_key.begin(), lower_key.end(), lower_key.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

            if (lower_key == "server" || lower_key == "host") {
                host = val;
            } else if (lower_key == "port") {
                port = val;
            } else if (lower_key == "database" || lower_key == "db" || lower_key == "dbname") {
                dbname = val;
            } else if (lower_key == "uid" || lower_key == "user" || lower_key == "username") {
                user = val;
            } else if (lower_key == "pwd" || lower_key == "password") {
                password = val;
            } else if (lower_key == "sslmode") {
                sslmode = val;
            }
        }
    }

    std::string result = "host=" + host + " port=" + port + " dbname=" + dbname + " user=" + user + " password=" + password;
    if (!sslmode.empty()) result += " sslmode=" + sslmode;
    return result;
}

void PgConnection::open() {
    if (is_open()) return;
    std::string conn_str = resolve_connection_string();
    conn_ = PQconnectdb(conn_str.c_str());
    if (PQstatus(conn_) != CONNECTION_OK) {
        std::string err = PQerrorMessage(conn_);
        close();
        throw DbException("Failed to open PostgreSQL connection: " + err);
    }
}

void PgConnection::close() {
    if (conn_) {
        PQfinish(conn_);
        conn_ = nullptr;
    }
}

bool PgConnection::is_open() const {
    return conn_ != nullptr && PQstatus(conn_) == CONNECTION_OK;
}

std::unique_ptr<IPreparedStatement> PgConnection::prepare(std::string_view sql) {
    if (!is_open()) throw DbException("Cannot prepare statement: PostgreSQL connection is not open");
    return std::make_unique<PgPreparedStatement>(conn_, std::string(sql));
}

void PgConnection::execute(std::string_view sql) {
    if (!is_open()) throw DbException("Cannot execute statement: PostgreSQL connection is not open");
    PGresult* res = PQexec(conn_, std::string(sql).c_str());
    if (PQresultStatus(res) != PGRES_COMMAND_OK && PQresultStatus(res) != PGRES_TUPLES_OK) {
        std::string err = PQerrorMessage(conn_);
        PQclear(res);
        throw DbException("PostgreSQL execute failed: " + err);
    }
    PQclear(res);
}

std::unique_ptr<IDataReader> PgConnection::execute_query_direct(std::string_view sql) {
    if (!is_open()) throw DbException("Cannot execute query: PostgreSQL connection is not open");
    PGresult* res = PQexec(conn_, std::string(sql).c_str());
    if (PQresultStatus(res) != PGRES_TUPLES_OK) {
        std::string err = PQerrorMessage(conn_);
        PQclear(res);
        throw DbException("PostgreSQL direct query failed: " + err);
    }
    return std::make_unique<PgDataReader>(
        std::shared_ptr<PGresult>(res, [](PGresult* r) { if (r) PQclear(r); })
    );
}

size_t PgConnection::execute_non_query_direct(std::string_view sql) {
    if (!is_open()) throw DbException("Cannot execute non-query: PostgreSQL connection is not open");
    PGresult* res = PQexec(conn_, std::string(sql).c_str());
    if (PQresultStatus(res) != PGRES_COMMAND_OK && PQresultStatus(res) != PGRES_TUPLES_OK) {
        std::string err = PQerrorMessage(conn_);
        PQclear(res);
        throw DbException("PostgreSQL direct non-query failed: " + err);
    }
    const char* cmd_tuples = PQcmdTuples(res);
    size_t affected = (cmd_tuples && cmd_tuples[0] != '\0') ? static_cast<size_t>(std::atoll(cmd_tuples)) : 0;
    PQclear(res);
    return affected;
}

void PgConnection::begin_transaction() {
    execute("BEGIN;");
}

void PgConnection::commit() {
    execute("COMMIT;");
}

void PgConnection::rollback() {
    execute("ROLLBACK;");
}

const ISqlDialect& PgConnection::dialect() const {
    return dialect_;
}

DriverInfo PgConnection::info() const {
    DriverInfo i;
    i.driver_name = "PostgreSQL Native Driver (libpq)";
    i.driver_version = std::to_string(PQlibVersion());
    i.dbms_name = "PostgreSQL";
    i.dbms_version = conn_ ? std::to_string(PQserverVersion(conn_)) : "unknown";
    i.odbc_version = "N/A";
    return i;
}

DriverCapabilities PgConnection::capabilities() const {
    DriverCapabilities caps;
    caps.cancel = true;
    caps.streaming = true;
    caps.query_timeout = true;
    caps.transactions = true;
    caps.savepoints = true;
    caps.returning_clause = true;
    caps.output_clause = false;
    caps.upsert = true;
    caps.array_batch_insert = true;
    caps.default_batch_chunk_size = 50000;
    caps.window_functions = true;
    caps.ctes = true;
    return caps;
}

static void append_copy_escaped(std::string& buf, std::string_view s) {
    for (char c : s) {
        if (c == '\t') { buf += "\\t"; }
        else if (c == '\n') { buf += "\\n"; }
        else if (c == '\r') { buf += "\\r"; }
        else if (c == '\\') { buf += "\\\\"; }
        else { buf.push_back(c); }
    }
}

size_t PgConnection::insert_many_batch(
    std::string_view sql,
    const std::vector<BoundValue>& flat_params,
    size_t col_count,
    size_t row_count
) {
    if (row_count == 0 || col_count == 0) return 0;
    if (!is_open()) throw DbException("PostgreSQL connection is not open");

    // Check if SQL is standard INSERT INTO <table> (<cols>) VALUES ...
    // Attempt COPY protocol fast-path
    std::string sql_str(sql);
    size_t insert_pos = sql_str.find("INSERT INTO ");
    size_t values_pos = sql_str.find(" VALUES");

    if (insert_pos != std::string::npos && values_pos != std::string::npos && values_pos > insert_pos + 12) {
        std::string target_cols = sql_str.substr(insert_pos + 12, values_pos - (insert_pos + 12));
        std::string copy_sql = "COPY " + target_cols + " FROM STDIN WITH (FORMAT text)";

        PGresult* res = PQexec(conn_, copy_sql.c_str());
        if (PQresultStatus(res) == PGRES_COPY_IN) {
            PQclear(res);

            std::string buffer;
            buffer.reserve(65536);

            for (size_t r = 0; r < row_count; ++r) {
                for (size_t c = 0; c < col_count; ++c) {
                    if (c > 0) buffer += '\t';
                    const auto& val = flat_params[r * col_count + c];
                    std::visit([&](const auto& v) {
                        using T = std::decay_t<decltype(v)>;
                        if constexpr (std::is_same_v<T, std::monostate>) {
                            buffer += "\\N";
                        } else if constexpr (std::is_same_v<T, int64_t> || std::is_same_v<T, uint64_t>) {
                            buffer += std::to_string(v);
                        } else if constexpr (std::is_same_v<T, double>) {
                            char dbuf[64];
                            snprintf(dbuf, sizeof(dbuf), "%.17g", v);
                            buffer += dbuf;
                        } else if constexpr (std::is_same_v<T, std::string>) {
                            append_copy_escaped(buffer, v);
                        } else if constexpr (std::is_same_v<T, std::wstring>) {
                            append_copy_escaped(buffer, wstring_to_utf8(v));
                        } else if constexpr (std::is_same_v<T, bool>) {
                            buffer += v ? "t" : "f";
                        } else if constexpr (std::is_same_v<T, std::vector<uint8_t>>) {
                            buffer += "\\\\x";
                            static const char hex_chars[] = "0123456789abcdef";
                            for (uint8_t b : v) {
                                buffer.push_back(hex_chars[b >> 4]);
                                buffer.push_back(hex_chars[b & 0x0f]);
                            }
                        } else if constexpr (std::is_same_v<T, SqlNumeric> ||
                                              std::is_same_v<T, SqlDate> ||
                                              std::is_same_v<T, SqlTime> ||
                                              std::is_same_v<T, SqlTimestamp> ||
                                              std::is_same_v<T, SqlInterval> ||
                                              std::is_same_v<T, SqlGuid>) {
                            append_copy_escaped(buffer, v.to_string());
                        }
                    }, val);
                }
                buffer += '\n';

                if (buffer.size() >= 32768) {
                    if (PQputCopyData(conn_, buffer.data(), static_cast<int>(buffer.size())) != 1) {
                        std::string err = PQerrorMessage(conn_);
                        PQputCopyEnd(conn_, "Error writing COPY data");
                        PGresult* err_res = PQgetResult(conn_);
                        if (err_res) PQclear(err_res);
                        throw DbException("PostgreSQL PQputCopyData failed: " + err);
                    }
                    buffer.clear();
                }
            }

            if (!buffer.empty()) {
                if (PQputCopyData(conn_, buffer.data(), static_cast<int>(buffer.size())) != 1) {
                    std::string err = PQerrorMessage(conn_);
                    PQputCopyEnd(conn_, "Error writing COPY data");
                    PGresult* err_res = PQgetResult(conn_);
                    if (err_res) PQclear(err_res);
                    throw DbException("PostgreSQL PQputCopyData failed: " + err);
                }
            }

            if (PQputCopyEnd(conn_, nullptr) != 1) {
                std::string err = PQerrorMessage(conn_);
                PGresult* err_res = PQgetResult(conn_);
                if (err_res) PQclear(err_res);
                throw DbException("PostgreSQL PQputCopyEnd failed: " + err);
            }

            PGresult* end_res = PQgetResult(conn_);
            if (PQresultStatus(end_res) != PGRES_COMMAND_OK) {
                std::string err = PQerrorMessage(conn_);
                PQclear(end_res);
                throw DbException("PostgreSQL COPY commit failed: " + err);
            }
            PQclear(end_res);
            return row_count;
        } else {
            PQclear(res);
        }
    }

    // Fallback: prepared statement reuse loop
    auto stmt = prepare(sql);
    for (size_t r = 0; r < row_count; ++r) {
        stmt->reset();
        for (size_t c = 0; c < col_count; ++c) {
            stmt->bind(static_cast<int>(c), flat_params[r * col_count + c]);
        }
        stmt->execute_non_query();
    }
    return row_count;
}

// ----------------------------------------------------------------------------
// make_connection<postgres> Specialization
// ----------------------------------------------------------------------------

template <>
std::unique_ptr<IConnection> make_connection<postgres>(const std::string& connection_string) {
    return std::make_unique<PgConnection>(connection_string);
}

} // namespace cpplinq

#endif // CPPLINQ_HAS_POSTGRES
