#include "driver/mysql_connection.h"
#include <cstring>
#include <atomic>
#include <algorithm>
#include <cctype>
#include <iostream>

#ifdef CPPLINQ_HAS_MYSQL

namespace cpplinq {

static std::string trim_str(std::string_view s) {
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.remove_suffix(1);
    return std::string(s);
}

// ----------------------------------------------------------------------------
// MysqlDataReader (Direct Query Results)
// ----------------------------------------------------------------------------

MysqlDataReader::MysqlDataReader(std::shared_ptr<MYSQL_RES> res)
    : res_(std::move(res))
    , num_fields_(res_ ? mysql_num_fields(res_.get()) : 0)
    , fields_(res_ ? mysql_fetch_fields(res_.get()) : nullptr)
    , current_row_(nullptr)
    , current_lengths_(nullptr)
{}

bool MysqlDataReader::next() {
    if (!res_) return false;
    current_row_ = mysql_fetch_row(res_.get());
    current_lengths_ = current_row_ ? mysql_fetch_lengths(res_.get()) : nullptr;
    return current_row_ != nullptr;
}

int MysqlDataReader::column_count() const {
    return num_fields_;
}

bool MysqlDataReader::is_null(int col) const {
    if (!res_ || !current_row_ || col < 0 || col >= num_fields_) {
        return true;
    }
    return current_row_[col] == nullptr;
}

std::string_view MysqlDataReader::get_string_view(int col) const {
    if (is_null(col)) return {};
    return std::string_view(current_row_[col], current_lengths_[col]);
}

std::string MysqlDataReader::get_string(int col) const {
    return std::string(get_string_view(col));
}

std::wstring MysqlDataReader::get_wstring(int col) const {
    if (is_null(col)) return {};
    return utf8_to_wstring(get_string_view(col));
}

int64_t MysqlDataReader::get_int64(int col) const {
    if (is_null(col)) return 0;
    return std::strtoll(current_row_[col], nullptr, 10);
}

uint64_t MysqlDataReader::get_uint64(int col) const {
    if (is_null(col)) return 0;
    return std::strtoull(current_row_[col], nullptr, 10);
}

double MysqlDataReader::get_double(int col) const {
    if (is_null(col)) return 0.0;
    return std::strtod(current_row_[col], nullptr);
}

bool MysqlDataReader::get_bool(int col) const {
    if (is_null(col)) return false;
    char c = current_row_[col][0];
    return (c == '1' || c == 't' || c == 'T' || c == 'y' || c == 'Y');
}

std::vector<uint8_t> MysqlDataReader::get_blob(int col) const {
    if (is_null(col)) return {};
    const uint8_t* p = reinterpret_cast<const uint8_t*>(current_row_[col]);
    return std::vector<uint8_t>(p, p + current_lengths_[col]);
}

SqlNumeric MysqlDataReader::get_numeric(int col) const {
    return SqlNumeric(get_string(col));
}

SqlDate MysqlDataReader::get_date(int col) const {
    return SqlDate::from_string(get_string(col));
}

SqlTime MysqlDataReader::get_time(int col) const {
    return SqlTime::from_string(get_string(col));
}

SqlTimestamp MysqlDataReader::get_timestamp(int col) const {
    return SqlTimestamp::from_string(get_string(col));
}

SqlInterval MysqlDataReader::get_interval(int col) const {
    return SqlInterval::from_string(get_string(col));
}

SqlGuid MysqlDataReader::get_guid(int col) const {
    return SqlGuid::from_string(get_string(col));
}

BoundValue MysqlDataReader::get_value(int col) const {
    if (is_null(col)) return std::monostate{};
    if (!fields_) return get_string(col);

    enum_field_types t = fields_[col].type;
    switch (t) {
        case MYSQL_TYPE_TINY:
        case MYSQL_TYPE_SHORT:
        case MYSQL_TYPE_LONG:
        case MYSQL_TYPE_LONGLONG:
        case MYSQL_TYPE_INT24:
            if (fields_[col].flags & UNSIGNED_FLAG) {
                return get_uint64(col);
            }
            return get_int64(col);
        case MYSQL_TYPE_FLOAT:
        case MYSQL_TYPE_DOUBLE:
        case MYSQL_TYPE_DECIMAL:
        case MYSQL_TYPE_NEWDECIMAL:
            return get_double(col);
        case MYSQL_TYPE_BLOB:
        case MYSQL_TYPE_TINY_BLOB:
        case MYSQL_TYPE_MEDIUM_BLOB:
        case MYSQL_TYPE_LONG_BLOB:
        case MYSQL_TYPE_BIT:
            if (fields_[col].flags & BINARY_FLAG) {
                return get_blob(col);
            }
            return get_string(col);
        default:
            return get_string(col);
    }
}

// ----------------------------------------------------------------------------
// MysqlStmtDataReader (Prepared Statement Results)
// ----------------------------------------------------------------------------

MysqlStmtDataReader::MysqlStmtDataReader(MYSQL_STMT* stmt, std::shared_ptr<MYSQL_RES> meta)
    : stmt_(stmt)
    , meta_(std::move(meta))
    , num_fields_(meta_ ? mysql_num_fields(meta_.get()) : 0)
    , fields_(meta_ ? mysql_fetch_fields(meta_.get()) : nullptr)
    , col_binds_(num_fields_)
    , binds_(num_fields_)
    , has_fetched_(false)
{
    if (num_fields_ > 0) {
        std::memset(binds_.data(), 0, sizeof(MYSQL_BIND) * num_fields_);
        for (int i = 0; i < num_fields_; ++i) {
            size_t max_len = fields_[i].max_length;
            size_t def_len = fields_[i].length;
            size_t buf_size = std::max(max_len, def_len);
            if (buf_size < 256) buf_size = 256;
            if (buf_size > 65536) buf_size = 65536;

            col_binds_[i].buffer.resize(buf_size + 1, 0);
            binds_[i].buffer_type = MYSQL_TYPE_STRING;
            binds_[i].buffer = col_binds_[i].buffer.data();
            binds_[i].buffer_length = static_cast<unsigned long>(buf_size);
            binds_[i].length = &col_binds_[i].length;
            binds_[i].is_null = &col_binds_[i].is_null;
            binds_[i].error = &col_binds_[i].error;
        }
        if (mysql_stmt_bind_result(stmt_, binds_.data()) != 0) {
            std::string err = mysql_stmt_error(stmt_);
            throw DbException("mysql_stmt_bind_result failed: " + err);
        }
    }
}

MysqlStmtDataReader::~MysqlStmtDataReader() {
    if (stmt_) {
        mysql_stmt_free_result(stmt_);
    }
}

bool MysqlStmtDataReader::next() {
    if (!stmt_ || num_fields_ == 0) return false;
    int rc = mysql_stmt_fetch(stmt_);
    if (rc == 1) {
        std::string err = mysql_stmt_error(stmt_);
        throw DbException("mysql_stmt_fetch failed: " + err);
    }
    if (rc == MYSQL_NO_DATA) {
        return false;
    }

    if (rc == MYSQL_DATA_TRUNCATED) {
        for (int i = 0; i < num_fields_; ++i) {
            if (col_binds_[i].error || col_binds_[i].length > col_binds_[i].buffer.size() - 1) {
                size_t needed = col_binds_[i].length;
                col_binds_[i].buffer.resize(needed + 1, 0);
                binds_[i].buffer = col_binds_[i].buffer.data();
                binds_[i].buffer_length = static_cast<unsigned long>(needed);
                mysql_stmt_fetch_column(stmt_, &binds_[i], static_cast<unsigned int>(i), 0);
            }
        }
    }

    has_fetched_ = true;
    return true;
}

int MysqlStmtDataReader::column_count() const {
    return num_fields_;
}

bool MysqlStmtDataReader::is_null(int col) const {
    if (col < 0 || col >= num_fields_ || !has_fetched_) return true;
    return col_binds_[col].is_null != 0;
}

std::string_view MysqlStmtDataReader::get_string_view(int col) const {
    if (is_null(col)) return {};
    return std::string_view(col_binds_[col].buffer.data(), col_binds_[col].length);
}

std::string MysqlStmtDataReader::get_string(int col) const {
    return std::string(get_string_view(col));
}

std::wstring MysqlStmtDataReader::get_wstring(int col) const {
    if (is_null(col)) return {};
    return utf8_to_wstring(get_string_view(col));
}

int64_t MysqlStmtDataReader::get_int64(int col) const {
    if (is_null(col)) return 0;
    auto sv = get_string_view(col);
    return std::strtoll(sv.data(), nullptr, 10);
}

uint64_t MysqlStmtDataReader::get_uint64(int col) const {
    if (is_null(col)) return 0;
    auto sv = get_string_view(col);
    return std::strtoull(sv.data(), nullptr, 10);
}

double MysqlStmtDataReader::get_double(int col) const {
    if (is_null(col)) return 0.0;
    auto sv = get_string_view(col);
    return std::strtod(sv.data(), nullptr);
}

bool MysqlStmtDataReader::get_bool(int col) const {
    if (is_null(col)) return false;
    auto sv = get_string_view(col);
    if (sv.empty()) return false;
    char c = sv.front();
    return (c == '1' || c == 't' || c == 'T' || c == 'y' || c == 'Y');
}

std::vector<uint8_t> MysqlStmtDataReader::get_blob(int col) const {
    if (is_null(col)) return {};
    const uint8_t* p = reinterpret_cast<const uint8_t*>(col_binds_[col].buffer.data());
    return std::vector<uint8_t>(p, p + col_binds_[col].length);
}

SqlNumeric MysqlStmtDataReader::get_numeric(int col) const {
    return SqlNumeric(get_string(col));
}

SqlDate MysqlStmtDataReader::get_date(int col) const {
    return SqlDate::from_string(get_string(col));
}

SqlTime MysqlStmtDataReader::get_time(int col) const {
    return SqlTime::from_string(get_string(col));
}

SqlTimestamp MysqlStmtDataReader::get_timestamp(int col) const {
    return SqlTimestamp::from_string(get_string(col));
}

SqlInterval MysqlStmtDataReader::get_interval(int col) const {
    return SqlInterval::from_string(get_string(col));
}

SqlGuid MysqlStmtDataReader::get_guid(int col) const {
    return SqlGuid::from_string(get_string(col));
}

BoundValue MysqlStmtDataReader::get_value(int col) const {
    if (is_null(col)) return std::monostate{};
    if (!fields_) return get_string(col);

    enum_field_types t = fields_[col].type;
    switch (t) {
        case MYSQL_TYPE_TINY:
        case MYSQL_TYPE_SHORT:
        case MYSQL_TYPE_LONG:
        case MYSQL_TYPE_LONGLONG:
        case MYSQL_TYPE_INT24:
            if (fields_[col].flags & UNSIGNED_FLAG) {
                return get_uint64(col);
            }
            return get_int64(col);
        case MYSQL_TYPE_FLOAT:
        case MYSQL_TYPE_DOUBLE:
        case MYSQL_TYPE_DECIMAL:
        case MYSQL_TYPE_NEWDECIMAL:
            return get_double(col);
        case MYSQL_TYPE_BLOB:
        case MYSQL_TYPE_TINY_BLOB:
        case MYSQL_TYPE_MEDIUM_BLOB:
        case MYSQL_TYPE_LONG_BLOB:
        case MYSQL_TYPE_BIT:
            if (fields_[col].flags & BINARY_FLAG) {
                return get_blob(col);
            }
            return get_string(col);
        default:
            return get_string(col);
    }
}

// ----------------------------------------------------------------------------
// MysqlPreparedStatement
// ----------------------------------------------------------------------------

MysqlPreparedStatement::MysqlPreparedStatement(MYSQL* conn, std::string sql)
    : conn_(conn)
    , stmt_(nullptr)
    , sql_(std::move(sql))
    , param_count_(0)
{
    if (!conn_) throw DbException("MySQL connection is null");

    stmt_ = mysql_stmt_init(conn_);
    if (!stmt_) {
        std::string err = mysql_error(conn_);
        throw DbException("mysql_stmt_init failed: " + err);
    }

    if (mysql_stmt_prepare(stmt_, sql_.data(), static_cast<unsigned long>(sql_.size())) != 0) {
        std::string err = mysql_stmt_error(stmt_);
        mysql_stmt_close(stmt_);
        stmt_ = nullptr;
        throw DbException("mysql_stmt_prepare failed: " + err);
    }

    param_count_ = mysql_stmt_param_count(stmt_);
    params_.resize(param_count_);
    binds_.resize(param_count_);
    if (param_count_ > 0) {
        std::memset(binds_.data(), 0, sizeof(MYSQL_BIND) * param_count_);
        for (size_t i = 0; i < param_count_; ++i) {
            binds_[i].buffer_type = MYSQL_TYPE_NULL;
            binds_[i].is_null = &params_[i].is_null;
        }
    }
}

MysqlPreparedStatement::~MysqlPreparedStatement() {
    if (stmt_) {
        mysql_stmt_close(stmt_);
        stmt_ = nullptr;
    }
}

void MysqlPreparedStatement::bind(int index, const BoundValue& value) {
    if (index < 0) throw DbException("Invalid parameter index: " + std::to_string(index));
    if (static_cast<size_t>(index) >= param_count_) {
        param_count_ = static_cast<unsigned long>(index + 1);
        params_.resize(param_count_);
        binds_.resize(param_count_);
    }

    auto& p = params_[index];
    p = ParamStorage{};

    std::visit([&](const auto& val) {
        using T = std::decay_t<decltype(val)>;
        if constexpr (std::is_same_v<T, std::monostate>) {
            p.type = MYSQL_TYPE_NULL;
            p.is_null = 1;
        } else if constexpr (std::is_same_v<T, int64_t>) {
            p.type = MYSQL_TYPE_LONGLONG;
            p.buffer.resize(sizeof(int64_t));
            std::memcpy(p.buffer.data(), &val, sizeof(int64_t));
            p.length = sizeof(int64_t);
            p.is_null = 0;
            p.is_unsigned = false;
        } else if constexpr (std::is_same_v<T, uint64_t>) {
            p.type = MYSQL_TYPE_LONGLONG;
            p.buffer.resize(sizeof(uint64_t));
            std::memcpy(p.buffer.data(), &val, sizeof(uint64_t));
            p.length = sizeof(uint64_t);
            p.is_null = 0;
            p.is_unsigned = true;
        } else if constexpr (std::is_same_v<T, double>) {
            p.type = MYSQL_TYPE_DOUBLE;
            p.buffer.resize(sizeof(double));
            std::memcpy(p.buffer.data(), &val, sizeof(double));
            p.length = sizeof(double);
            p.is_null = 0;
        } else if constexpr (std::is_same_v<T, std::string>) {
            p.type = MYSQL_TYPE_STRING;
            p.buffer.assign(val.begin(), val.end());
            p.length = static_cast<unsigned long>(val.size());
            p.is_null = 0;
        } else if constexpr (std::is_same_v<T, std::wstring>) {
            std::string utf8 = wstring_to_utf8(val);
            p.type = MYSQL_TYPE_STRING;
            p.buffer.assign(utf8.begin(), utf8.end());
            p.length = static_cast<unsigned long>(utf8.size());
            p.is_null = 0;
        } else if constexpr (std::is_same_v<T, bool>) {
            p.type = MYSQL_TYPE_TINY;
            p.buffer.resize(1);
            p.buffer[0] = val ? 1 : 0;
            p.length = 1;
            p.is_null = 0;
        } else if constexpr (std::is_same_v<T, std::vector<uint8_t>>) {
            p.type = MYSQL_TYPE_BLOB;
            p.buffer.assign(val.begin(), val.end());
            p.length = static_cast<unsigned long>(val.size());
            p.is_null = 0;
        } else if constexpr (std::is_same_v<T, SqlNumeric> ||
                              std::is_same_v<T, SqlDate> ||
                              std::is_same_v<T, SqlTime> ||
                              std::is_same_v<T, SqlTimestamp> ||
                              std::is_same_v<T, SqlInterval> ||
                              std::is_same_v<T, SqlGuid>) {
            std::string s = val.to_string();
            p.type = MYSQL_TYPE_STRING;
            p.buffer.assign(s.begin(), s.end());
            p.length = static_cast<unsigned long>(s.size());
            p.is_null = 0;
        }
    }, value);
}

void MysqlPreparedStatement::sync_binds() {
    if (param_count_ == 0) return;
    std::memset(binds_.data(), 0, sizeof(MYSQL_BIND) * param_count_);

    for (size_t i = 0; i < param_count_; ++i) {
        auto& p = params_[i];
        auto& b = binds_[i];
        b.buffer_type = p.type;
        b.buffer = p.is_null ? nullptr : p.buffer.data();
        b.buffer_length = p.length;
        b.length = &p.length;
        b.is_null = &p.is_null;
        b.is_unsigned = p.is_unsigned ? 1 : 0;
    }

    if (mysql_stmt_bind_param(stmt_, binds_.data()) != 0) {
        std::string err = mysql_stmt_error(stmt_);
        throw DbException("mysql_stmt_bind_param failed: " + err);
    }
}

std::unique_ptr<IDataReader> MysqlPreparedStatement::execute_query() {
    if (!stmt_) throw DbException("Prepared statement is not initialized");
    sync_binds();

    if (mysql_stmt_execute(stmt_) != 0) {
        std::string err = mysql_stmt_error(stmt_);
        throw DbException("mysql_stmt_execute query failed: " + err);
    }

    MYSQL_RES* meta = mysql_stmt_result_metadata(stmt_);
    if (!meta) {
        std::string err = mysql_stmt_error(stmt_);
        throw DbException("mysql_stmt_result_metadata failed: " + (err.empty() ? "Statement produced no result set" : err));
    }

    if (mysql_stmt_store_result(stmt_) != 0) {
        std::string err = mysql_stmt_error(stmt_);
        mysql_free_result(meta);
        throw DbException("mysql_stmt_store_result failed: " + err);
    }

    return std::make_unique<MysqlStmtDataReader>(
        stmt_,
        std::shared_ptr<MYSQL_RES>(meta, mysql_free_result)
    );
}

size_t MysqlPreparedStatement::execute_non_query() {
    if (!stmt_) throw DbException("Prepared statement is not initialized");
    sync_binds();

    if (mysql_stmt_execute(stmt_) != 0) {
        std::string err = mysql_stmt_error(stmt_);
        throw DbException("mysql_stmt_execute non-query failed: " + err);
    }

    my_ulonglong affected = mysql_stmt_affected_rows(stmt_);
    return affected == static_cast<my_ulonglong>(-1) ? 0 : static_cast<size_t>(affected);
}

void MysqlPreparedStatement::reset() {
    for (auto& p : params_) {
        p = ParamStorage{};
    }
    if (stmt_) {
        mysql_stmt_reset(stmt_);
    }
}

void MysqlPreparedStatement::cancel() {
    if (conn_) {
        unsigned long th_id = mysql_thread_id(conn_);
        if (th_id > 0) {
            std::string kill_sql = "KILL QUERY " + std::to_string(th_id);
            // Execute on separate raw query or mysql_kill
            mysql_kill(conn_, th_id);
        }
    }
}

void MysqlPreparedStatement::set_timeout(uint32_t seconds) {
    if (conn_) {
        std::string timeout_sql = "SET max_execution_time = " + std::to_string(seconds * 1000);
        mysql_real_query(conn_, timeout_sql.data(), timeout_sql.size());
        MYSQL_RES* res = mysql_store_result(conn_);
        if (res) mysql_free_result(res);
    }
}

void MysqlPreparedStatement::set_stop_token(std::stop_token token) {
    stop_token_ = token;
    if (token.stop_possible() && conn_) {
        stop_cb_.emplace(token, [this]() {
            this->cancel();
        });
    }
}

// ----------------------------------------------------------------------------
// MysqlConnection
// ----------------------------------------------------------------------------

MysqlConnection::MysqlConnection(std::string connection_string)
    : connection_string_(std::move(connection_string))
    , conn_(nullptr)
{}

MysqlConnection::~MysqlConnection() {
    close();
}

MysqlConnection::ParsedParams MysqlConnection::parse_connection_params() const {
    ParsedParams p;
    std::string raw = connection_string_;
    if (raw.empty()) {
        const char* env_client = std::getenv("CPPLINQ_MYSQL_CLIENT");
        const char* env_odbc = std::getenv("CPPLINQ_MYSQL_ODBC");
        raw = env_client ? env_client : (env_odbc ? env_odbc : "");
    }

    if (raw.empty()) return p;

    // Single DSN check (e.g. "MySQLDSN")
    if (raw.find('=') == std::string::npos && raw.find('/') == std::string::npos) {
        const char* env_client = std::getenv("CPPLINQ_MYSQL_CLIENT");
        if (env_client && env_client[0] != '\0') raw = env_client;
        else return p;
    }

    // URI check (mysql://user:pass@host:port/dbname)
    if (raw.rfind("mysql://", 0) == 0 || raw.rfind("mariadb://", 0) == 0) {
        size_t proto_end = raw.find("://") + 3;
        size_t at_pos = raw.find('@', proto_end);
        size_t slash_pos = raw.find('/', at_pos != std::string::npos ? at_pos : proto_end);

        if (at_pos != std::string::npos) {
            std::string user_pass = raw.substr(proto_end, at_pos - proto_end);
            size_t colon = user_pass.find(':');
            if (colon != std::string::npos) {
                p.user = user_pass.substr(0, colon);
                p.password = user_pass.substr(colon + 1);
            } else {
                p.user = user_pass;
            }
            proto_end = at_pos + 1;
        }

        std::string host_port = (slash_pos != std::string::npos)
            ? raw.substr(proto_end, slash_pos - proto_end)
            : raw.substr(proto_end);

        size_t colon_hp = host_port.find(':');
        if (colon_hp != std::string::npos) {
            p.host = host_port.substr(0, colon_hp);
            p.port = static_cast<unsigned int>(std::atoi(host_port.substr(colon_hp + 1).c_str()));
        } else if (!host_port.empty()) {
            p.host = host_port;
        }

        if (slash_pos != std::string::npos && slash_pos + 1 < raw.size()) {
            p.db = raw.substr(slash_pos + 1);
            size_t qmark = p.db.find('?');
            if (qmark != std::string::npos) {
                p.db = p.db.substr(0, qmark);
            }
        }
        return p;
    }

    // Key=Value; string parser
    size_t start = 0;
    while (start < raw.size()) {
        size_t end = raw.find(';', start);
        if (end == std::string::npos) end = raw.size();
        std::string_view token(raw.data() + start, end - start);
        start = end + 1;

        size_t eq = token.find('=');
        if (eq != std::string_view::npos) {
            std::string key = trim_str(token.substr(0, eq));
            std::string val = trim_str(token.substr(eq + 1));
            if (val.size() >= 2 && val.front() == '{' && val.back() == '}') {
                val = val.substr(1, val.size() - 2);
            }
            std::string lk = key;
            std::transform(lk.begin(), lk.end(), lk.begin(), [](unsigned char c) { return std::tolower(c); });

            if (lk == "host" || lk == "server" || lk == "servername") {
                p.host = val;
            } else if (lk == "port") {
                p.port = static_cast<unsigned int>(std::atoi(val.c_str()));
            } else if (lk == "user" || lk == "uid" || lk == "username") {
                p.user = val;
            } else if (lk == "pwd" || lk == "password") {
                p.password = val;
            } else if (lk == "db" || lk == "database" || lk == "dbname") {
                p.db = val;
            } else if (lk == "socket" || lk == "unix_socket") {
                p.unix_socket = val;
            } else if (lk == "charset") {
                p.charset = val;
            }
        }
    }
    return p;
}

void MysqlConnection::open() {
    if (is_open()) return;

    conn_ = mysql_init(nullptr);
    if (!conn_) {
        throw DbException("mysql_init failed");
    }

    ParsedParams p = parse_connection_params();

    mysql_options(conn_, MYSQL_OPT_CONNECT_TIMEOUT, &p.timeout_sec);
    mysql_options(conn_, MYSQL_SET_CHARSET_NAME, p.charset.c_str());
    my_bool reconnect = 1;
    mysql_options(conn_, MYSQL_OPT_RECONNECT, &reconnect);

    if (!mysql_real_connect(conn_,
                            p.host.c_str(),
                            p.user.c_str(),
                            p.password.c_str(),
                            p.db.c_str(),
                            p.port,
                            p.unix_socket.empty() ? nullptr : p.unix_socket.c_str(),
                            CLIENT_MULTI_STATEMENTS)) {
        std::string err = mysql_error(conn_);
        mysql_close(conn_);
        conn_ = nullptr;
        throw DbException("Failed to connect to MySQL: " + err);
    }
}

void MysqlConnection::close() {
    if (conn_) {
        mysql_close(conn_);
        conn_ = nullptr;
    }
}

bool MysqlConnection::is_open() const {
    return conn_ != nullptr && mysql_ping(conn_) == 0;
}

std::unique_ptr<IPreparedStatement> MysqlConnection::prepare(std::string_view sql) {
    if (!is_open()) throw DbException("Cannot prepare statement: MySQL connection is not open");
    return std::make_unique<MysqlPreparedStatement>(conn_, std::string(sql));
}

void MysqlConnection::execute(std::string_view sql) {
    if (!is_open()) throw DbException("Cannot execute SQL: MySQL connection is not open");
    if (mysql_real_query(conn_, sql.data(), static_cast<unsigned long>(sql.size())) != 0) {
        std::string err = mysql_error(conn_);
        throw DbException("MySQL execute failed: " + err);
    }
    // Clean up result if any
    MYSQL_RES* res = mysql_store_result(conn_);
    if (res) mysql_free_result(res);
    while (mysql_next_result(conn_) == 0) {
        MYSQL_RES* next_res = mysql_store_result(conn_);
        if (next_res) mysql_free_result(next_res);
    }
}

std::unique_ptr<IDataReader> MysqlConnection::execute_query_direct(std::string_view sql) {
    if (!is_open()) throw DbException("Cannot execute query: MySQL connection is not open");
    if (mysql_real_query(conn_, sql.data(), static_cast<unsigned long>(sql.size())) != 0) {
        std::string err = mysql_error(conn_);
        throw DbException("MySQL direct query failed: " + err);
    }
    MYSQL_RES* res = mysql_store_result(conn_);
    if (!res && mysql_field_count(conn_) > 0) {
        std::string err = mysql_error(conn_);
        throw DbException("mysql_store_result failed: " + err);
    }
    return std::make_unique<MysqlDataReader>(
        std::shared_ptr<MYSQL_RES>(res, mysql_free_result)
    );
}

size_t MysqlConnection::execute_non_query_direct(std::string_view sql) {
    if (!is_open()) throw DbException("Cannot execute non-query: MySQL connection is not open");
    if (mysql_real_query(conn_, sql.data(), static_cast<unsigned long>(sql.size())) != 0) {
        std::string err = mysql_error(conn_);
        throw DbException("MySQL direct non-query failed: " + err);
    }
    MYSQL_RES* res = mysql_store_result(conn_);
    if (res) mysql_free_result(res);
    while (mysql_next_result(conn_) == 0) {
        MYSQL_RES* next_res = mysql_store_result(conn_);
        if (next_res) mysql_free_result(next_res);
    }
    my_ulonglong affected = mysql_affected_rows(conn_);
    return affected == static_cast<my_ulonglong>(-1) ? 0 : static_cast<size_t>(affected);
}

void MysqlConnection::begin_transaction() {
    execute("START TRANSACTION;");
}

void MysqlConnection::commit() {
    execute("COMMIT;");
}

void MysqlConnection::rollback() {
    execute("ROLLBACK;");
}

const ISqlDialect& MysqlConnection::dialect() const {
    return dialect_;
}

DriverInfo MysqlConnection::info() const {
    DriverInfo i;
    i.driver_name = "MySQL Native Driver (libmariadb / libmysqlclient)";
    i.driver_version = std::to_string(mysql_get_client_version());
    i.dbms_name = "MySQL";
    i.dbms_version = conn_ ? std::to_string(mysql_get_server_version(conn_)) : "unknown";
    i.odbc_version = "N/A";
    return i;
}

DriverCapabilities MysqlConnection::capabilities() const {
    DriverCapabilities caps;
    caps.cancel = true;
    caps.streaming = true;
    caps.query_timeout = true;
    caps.transactions = true;
    caps.savepoints = true;
    caps.returning_clause = false;
    caps.output_clause = false;
    caps.upsert = true;
    caps.array_batch_insert = true;
    caps.default_batch_chunk_size = 5000;
    caps.window_functions = true;
    caps.ctes = true;
    return caps;
}

static void append_escaped_string(MYSQL* conn, std::string& buf, std::string_view s) {
    buf.push_back('\'');
    size_t cur = buf.size();
    buf.resize(cur + s.size() * 2 + 1);
    unsigned long esc_len = mysql_real_escape_string(conn, &buf[cur], s.data(), static_cast<unsigned long>(s.size()));
    buf.resize(cur + esc_len);
    buf.push_back('\'');
}

size_t MysqlConnection::insert_many_batch(
    std::string_view sql,
    const std::vector<BoundValue>& flat_params,
    size_t col_count,
    size_t row_count
) {
    if (row_count == 0 || col_count == 0) return 0;
    if (!is_open()) throw DbException("MySQL connection is not open");

    std::string sql_str(sql);
    size_t values_pos = sql_str.find(" VALUES");
    if (values_pos != std::string::npos) {
        std::string prefix = sql_str.substr(0, values_pos + 7) + " ";

        constexpr size_t BATCH_CHUNK = 2000;
        for (size_t offset = 0; offset < row_count; offset += BATCH_CHUNK) {
            size_t batch = std::min(BATCH_CHUNK, row_count - offset);
            std::string query_sql = prefix;
            query_sql.reserve(prefix.size() + batch * col_count * 32);

            for (size_t r = 0; r < batch; ++r) {
                if (r > 0) query_sql += ",";
                query_sql += "(";
                for (size_t c = 0; c < col_count; ++c) {
                    if (c > 0) query_sql += ",";
                    const auto& val = flat_params[(offset + r) * col_count + c];
                    std::visit([&](const auto& v) {
                        using T = std::decay_t<decltype(v)>;
                        if constexpr (std::is_same_v<T, std::monostate>) {
                            query_sql += "NULL";
                        } else if constexpr (std::is_same_v<T, int64_t> || std::is_same_v<T, uint64_t>) {
                            query_sql += std::to_string(v);
                        } else if constexpr (std::is_same_v<T, double>) {
                            char dbuf[64];
                            snprintf(dbuf, sizeof(dbuf), "%.17g", v);
                            query_sql += dbuf;
                        } else if constexpr (std::is_same_v<T, std::string>) {
                            append_escaped_string(conn_, query_sql, v);
                        } else if constexpr (std::is_same_v<T, std::wstring>) {
                            append_escaped_string(conn_, query_sql, wstring_to_utf8(v));
                        } else if constexpr (std::is_same_v<T, bool>) {
                            query_sql += v ? "1" : "0";
                        } else if constexpr (std::is_same_v<T, std::vector<uint8_t>>) {
                            query_sql += "X'";
                            static const char hex_chars[] = "0123456789abcdef";
                            for (uint8_t b : v) {
                                query_sql.push_back(hex_chars[b >> 4]);
                                query_sql.push_back(hex_chars[b & 0x0f]);
                            }
                            query_sql += "'";
                        } else if constexpr (std::is_same_v<T, SqlNumeric> ||
                                              std::is_same_v<T, SqlDate> ||
                                              std::is_same_v<T, SqlTime> ||
                                              std::is_same_v<T, SqlTimestamp> ||
                                              std::is_same_v<T, SqlInterval> ||
                                              std::is_same_v<T, SqlGuid>) {
                            append_escaped_string(conn_, query_sql, v.to_string());
                        }
                    }, val);
                }
                query_sql += ")";
            }

            if (mysql_real_query(conn_, query_sql.data(), static_cast<unsigned long>(query_sql.size())) != 0) {
                std::string err = mysql_error(conn_);
                throw DbException("MySQL batch insert failed: " + err);
            }
            MYSQL_RES* res = mysql_store_result(conn_);
            if (res) mysql_free_result(res);
        }
        return row_count;
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
// make_connection<mysql> Specialization
// ----------------------------------------------------------------------------

template <>
std::unique_ptr<IConnection> make_connection<mysql>(const std::string& connection_string) {
    return std::make_unique<MysqlConnection>(connection_string);
}

} // namespace cpplinq

#endif // CPPLINQ_HAS_MYSQL
