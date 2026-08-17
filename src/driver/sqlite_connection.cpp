#include "driver/sqlite_connection.h"
#include <utility>
#include <type_traits>

namespace cpplinq {

// ----------------------------------------------------------------------------
// SqliteDataReader
// ----------------------------------------------------------------------------

SqliteDataReader::SqliteDataReader(std::shared_ptr<sqlite3_stmt> stmt, sqlite3* db)
    : stmt_(std::move(stmt)), db_(db)
{}

SqliteDataReader::~SqliteDataReader() {
    if (stmt_) {
        sqlite3_reset(stmt_.get());
    }
}

bool SqliteDataReader::next() {
    if (!stmt_) return false;
    int rc = sqlite3_step(stmt_.get());
    if (rc == SQLITE_ROW) {
        return true;
    }
    if (rc == SQLITE_DONE) {
        return false;
    }
    std::string err = db_ ? sqlite3_errmsg(db_) : sqlite3_errstr(rc);
    throw DbException("SQLite step error: " + err);
}

int SqliteDataReader::column_count() const {
    if (!stmt_) return 0;
    return sqlite3_column_count(stmt_.get());
}

bool SqliteDataReader::is_null(int col) const {
    if (!stmt_) return true;
    return sqlite3_column_type(stmt_.get(), col) == SQLITE_NULL;
}

int64_t SqliteDataReader::get_int64(int col) const {
    if (!stmt_) return 0;
    return sqlite3_column_int64(stmt_.get(), col);
}

uint64_t SqliteDataReader::get_uint64(int col) const {
    if (!stmt_) return 0;
    return static_cast<uint64_t>(sqlite3_column_int64(stmt_.get(), col));
}

double SqliteDataReader::get_double(int col) const {
    if (!stmt_) return 0.0;
    return sqlite3_column_double(stmt_.get(), col);
}

std::string SqliteDataReader::get_string(int col) const {
    if (!stmt_) return {};
    const unsigned char* txt = sqlite3_column_text(stmt_.get(), col);
    int bytes = sqlite3_column_bytes(stmt_.get(), col);
    return txt ? std::string(reinterpret_cast<const char*>(txt), static_cast<size_t>(bytes)) : std::string();
}

bool SqliteDataReader::get_bool(int col) const {
    if (!stmt_) return false;
    return sqlite3_column_int64(stmt_.get(), col) != 0;
}

std::vector<uint8_t> SqliteDataReader::get_blob(int col) const {
    if (!stmt_) return {};
    const void* blob = sqlite3_column_blob(stmt_.get(), col);
    int bytes = sqlite3_column_bytes(stmt_.get(), col);
    if (!blob || bytes <= 0) return {};
    const auto* ptr = static_cast<const uint8_t*>(blob);
    return std::vector<uint8_t>(ptr, ptr + bytes);
}

SqlNumeric SqliteDataReader::get_numeric(int col) const {
    return SqlNumeric(get_string(col));
}

SqlDate SqliteDataReader::get_date(int col) const {
    return SqlDate::from_string(get_string(col));
}

SqlTime SqliteDataReader::get_time(int col) const {
    return SqlTime::from_string(get_string(col));
}

SqlTimestamp SqliteDataReader::get_timestamp(int col) const {
    return SqlTimestamp::from_string(get_string(col));
}

SqlInterval SqliteDataReader::get_interval(int col) const {
    return SqlInterval::from_string(get_string(col));
}

// ----------------------------------------------------------------------------
// SqlitePreparedStatement
// ----------------------------------------------------------------------------

SqlitePreparedStatement::SqlitePreparedStatement(sqlite3* db, sqlite3_stmt* stmt)
    : db_(db)
    , stmt_(stmt, [](sqlite3_stmt* s) { if (s) sqlite3_finalize(s); })
{}

void SqlitePreparedStatement::bind(int index, const BoundValue& value) {
    if (!stmt_) {
        throw DbException("PreparedStatement is invalid");
    }
    // index is 0-based; SQLite uses 1-based indexing for parameters
    int sql_index = index + 1;
    int rc = SQLITE_OK;
    std::visit([&](const auto& val) {
        using T = std::decay_t<decltype(val)>;
        if constexpr (std::is_same_v<T, std::monostate>) {
            rc = sqlite3_bind_null(stmt_.get(), sql_index);
        } else if constexpr (std::is_same_v<T, int64_t>) {
            rc = sqlite3_bind_int64(stmt_.get(), sql_index, val);
        } else if constexpr (std::is_same_v<T, uint64_t>) {
            rc = sqlite3_bind_int64(stmt_.get(), sql_index, static_cast<sqlite3_int64>(val));
        } else if constexpr (std::is_same_v<T, double>) {
            rc = sqlite3_bind_double(stmt_.get(), sql_index, val);
        } else if constexpr (std::is_same_v<T, std::string>) {
            rc = sqlite3_bind_text(stmt_.get(), sql_index, val.data(), static_cast<int>(val.size()), SQLITE_TRANSIENT);
        } else if constexpr (std::is_same_v<T, bool>) {
            rc = sqlite3_bind_int(stmt_.get(), sql_index, val ? 1 : 0);
        } else if constexpr (std::is_same_v<T, std::vector<uint8_t>>) {
            rc = sqlite3_bind_blob(stmt_.get(), sql_index, val.data(), static_cast<int>(val.size()), SQLITE_TRANSIENT);
        } else if constexpr (std::is_same_v<T, SqlNumeric>) {
            std::string s = val.to_string();
            rc = sqlite3_bind_text(stmt_.get(), sql_index, s.data(), static_cast<int>(s.size()), SQLITE_TRANSIENT);
        } else if constexpr (std::is_same_v<T, SqlDate>) {
            std::string s = val.to_string();
            rc = sqlite3_bind_text(stmt_.get(), sql_index, s.data(), static_cast<int>(s.size()), SQLITE_TRANSIENT);
        } else if constexpr (std::is_same_v<T, SqlTime>) {
            std::string s = val.to_string();
            rc = sqlite3_bind_text(stmt_.get(), sql_index, s.data(), static_cast<int>(s.size()), SQLITE_TRANSIENT);
        } else if constexpr (std::is_same_v<T, SqlTimestamp>) {
            std::string s = val.to_string();
            rc = sqlite3_bind_text(stmt_.get(), sql_index, s.data(), static_cast<int>(s.size()), SQLITE_TRANSIENT);
        } else if constexpr (std::is_same_v<T, SqlInterval>) {
            std::string s = val.to_string();
            rc = sqlite3_bind_text(stmt_.get(), sql_index, s.data(), static_cast<int>(s.size()), SQLITE_TRANSIENT);
        }
    }, value);

    if (rc != SQLITE_OK) {
        std::string err = db_ ? sqlite3_errmsg(db_) : sqlite3_errstr(rc);
        throw DbException("SQLite bind error: " + err);
    }
}

std::unique_ptr<IDataReader> SqlitePreparedStatement::execute_query() {
    if (!stmt_) {
        throw DbException("PreparedStatement is invalid");
    }
    sqlite3_reset(stmt_.get());
    return std::make_unique<SqliteDataReader>(stmt_, db_);
}

size_t SqlitePreparedStatement::execute_non_query() {
    if (!stmt_) {
        throw DbException("PreparedStatement is invalid");
    }
    int rc = sqlite3_step(stmt_.get());
    if (rc != SQLITE_DONE && rc != SQLITE_ROW) {
        std::string err = db_ ? sqlite3_errmsg(db_) : sqlite3_errstr(rc);
        sqlite3_reset(stmt_.get());
        throw DbException("SQLite step error: " + err);
    }
    int changes = db_ ? sqlite3_changes(db_) : 0;
    sqlite3_reset(stmt_.get());
    return static_cast<size_t>(changes);
}

void SqlitePreparedStatement::reset() {
    if (stmt_) {
        sqlite3_reset(stmt_.get());
        sqlite3_clear_bindings(stmt_.get());
    }
}

void SqlitePreparedStatement::cancel() {
    if (db_) {
        sqlite3_interrupt(db_);
    }
}

void SqlitePreparedStatement::set_timeout(uint32_t seconds) {
    if (db_) {
        sqlite3_busy_timeout(db_, static_cast<int>(seconds * 1000));
    }
}

void SqlitePreparedStatement::set_stop_token(std::stop_token token) {
    stop_token_ = token;
    if (token.stop_possible() && db_) {
        stop_cb_.emplace(token, [this]() {
            if (db_) sqlite3_interrupt(db_);
        });
    }
}

// ----------------------------------------------------------------------------
// SqliteConnection
// ----------------------------------------------------------------------------

SqliteConnection::SqliteConnection(std::string connection_string)
    : connection_string_(std::move(connection_string))
    , db_(nullptr)
{}

SqliteConnection::~SqliteConnection() {
    close();
}

void SqliteConnection::open() {
    if (is_open()) return;
    int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_URI;
    int rc = sqlite3_open_v2(connection_string_.c_str(), &db_, flags, nullptr);
    if (rc != SQLITE_OK) {
        std::string err = db_ ? sqlite3_errmsg(db_) : "Failed to open SQLite database";
        close();
        throw DbException("Failed to open SQLite database: " + err);
    }
    sqlite3_busy_timeout(db_, 10000);
    execute("PRAGMA journal_mode=WAL;");
}

void SqliteConnection::close() {
    if (db_) {
        sqlite3_close_v2(db_);
        db_ = nullptr;
    }
}

bool SqliteConnection::is_open() const {
    return db_ != nullptr;
}

std::unique_ptr<IPreparedStatement> SqliteConnection::prepare(std::string_view sql) {
    if (!is_open()) {
        throw DbException("Cannot prepare statement: database connection is not open");
    }
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql.data(), static_cast<int>(sql.size()), &stmt, nullptr);
    if (rc != SQLITE_OK) {
        throw DbException(std::string("Failed to prepare statement: ") + sqlite3_errmsg(db_));
    }
    return std::make_unique<SqlitePreparedStatement>(db_, stmt);
}

void SqliteConnection::execute(std::string_view sql) {
    if (!is_open()) {
        throw DbException("Cannot execute statement: database connection is not open");
    }
    char* errmsg = nullptr;
    int rc = sqlite3_exec(db_, std::string(sql).c_str(), nullptr, nullptr, &errmsg);
    if (rc != SQLITE_OK) {
        std::string err = errmsg ? errmsg : sqlite3_errmsg(db_);
        sqlite3_free(errmsg);
        throw DbException("SQLite execute failed: " + err);
    }
}

void SqliteConnection::begin_transaction() {
    execute("BEGIN");
}

void SqliteConnection::commit() {
    execute("COMMIT");
}

void SqliteConnection::rollback() {
    execute("ROLLBACK");
}

const ISqlDialect& SqliteConnection::dialect() const {
    return dialect_;
}

DriverInfo SqliteConnection::info() const {
    DriverInfo i;
    i.driver_name = "SQLite";
    i.driver_version = sqlite3_libversion();
    i.dbms_name = "SQLite";
    i.dbms_version = sqlite3_libversion();
    i.odbc_version = "N/A";
    return i;
}

DriverCapabilities SqliteConnection::capabilities() const {
    DriverCapabilities caps;
    caps.cancel = true;
    caps.streaming = true;
    caps.query_timeout = true;
    caps.transactions = true;
    caps.savepoints = true;
    caps.returning_clause = true;
    caps.output_clause = false;
    caps.upsert = true;
    caps.window_functions = true;
    caps.ctes = true;
    return caps;
}

// ----------------------------------------------------------------------------
// make_connection<sqlite> Specialization
// ----------------------------------------------------------------------------

template <>
std::unique_ptr<IConnection> make_connection<sqlite>(const std::string& connection_string) {
    auto conn = std::make_unique<SqliteConnection>(connection_string);
    return conn;
}

} // namespace cpplinq
