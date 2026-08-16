#include "driver/postgres_connection.h"

#ifdef CPPLINQ_HAS_POSTGRES

#include <atomic>
#include <cstdlib>
#include <type_traits>
#include <utility>

namespace cpplinq {

// ----------------------------------------------------------------------------
// PgDataReader
// ----------------------------------------------------------------------------

PgDataReader::PgDataReader(PGresult* res)
    : res_(res)
    , current_row_(-1)
    , total_rows_(res ? PQntuples(res) : 0)
{}

PgDataReader::~PgDataReader() {
    if (res_) {
        PQclear(res_);
    }
}

bool PgDataReader::next() {
    if (!res_) return false;
    current_row_++;
    return current_row_ < total_rows_;
}

int PgDataReader::column_count() const {
    if (!res_) return 0;
    return PQnfields(res_);
}

bool PgDataReader::is_null(int col) const {
    if (!res_ || current_row_ < 0 || current_row_ >= total_rows_) return true;
    return PQgetisnull(res_, current_row_, col) != 0;
}

int64_t PgDataReader::get_int64(int col) const {
    if (is_null(col)) return 0;
    const char* val = PQgetvalue(res_, current_row_, col);
    if (!val) return 0;
    return std::stoll(val);
}

double PgDataReader::get_double(int col) const {
    if (is_null(col)) return 0.0;
    const char* val = PQgetvalue(res_, current_row_, col);
    if (!val) return 0.0;
    return std::stod(val);
}

std::string PgDataReader::get_string(int col) const {
    if (is_null(col)) return {};
    const char* val = PQgetvalue(res_, current_row_, col);
    if (!val) return {};
    int len = PQgetlength(res_, current_row_, col);
    return std::string(val, static_cast<size_t>(len));
}

bool PgDataReader::get_bool(int col) const {
    if (is_null(col)) return false;
    const char* val = PQgetvalue(res_, current_row_, col);
    if (!val || val[0] == '\0') return false;
    return (val[0] == 't' || val[0] == 'T' || val[0] == '1' ||
            std::string_view(val) == "true" || std::string_view(val) == "TRUE");
}

std::vector<uint8_t> PgDataReader::get_blob(int col) const {
    if (is_null(col)) return {};
    const unsigned char* val = reinterpret_cast<const unsigned char*>(PQgetvalue(res_, current_row_, col));
    if (!val) return {};
    size_t to_len = 0;
    unsigned char* unescaped = PQunescapeBytea(val, &to_len);
    if (!unescaped) return {};
    std::vector<uint8_t> blob(unescaped, unescaped + to_len);
    PQfreemem(unescaped);
    return blob;
}

// ----------------------------------------------------------------------------
// PgPreparedStatement
// ----------------------------------------------------------------------------

static std::atomic<uint64_t> s_pg_stmt_counter{0};

PgPreparedStatement::PgPreparedStatement(PGconn* conn, std::string_view sql)
    : conn_(conn)
    , sql_(sql)
    , stmt_name_("cpplinq_stmt_" + std::to_string(++s_pg_stmt_counter))
{}

void PgPreparedStatement::bind(int index, const BoundValue& value) {
    if (index < 0) {
        throw DbException("Invalid parameter index: " + std::to_string(index));
    }
    if (static_cast<size_t>(index) >= params_.size()) {
        params_.resize(index + 1);
    }
    params_[index] = value;
}

void PgPreparedStatement::reset() {
    params_.clear();
}

std::unique_ptr<IDataReader> PgPreparedStatement::execute_query() {
    if (!conn_) {
        throw DbException("Invalid PostgreSQL connection");
    }

    int nParams = static_cast<int>(params_.size());
    std::vector<std::string> param_strings(nParams);
    std::vector<const char*> param_values(nParams, nullptr);

    for (int i = 0; i < nParams; ++i) {
        const auto& p = params_[i];
        std::visit([&](const auto& val) {
            using T = std::decay_t<decltype(val)>;
            if constexpr (std::is_same_v<T, std::monostate>) {
                param_values[i] = nullptr;
            } else if constexpr (std::is_same_v<T, int64_t>) {
                param_strings[i] = std::to_string(val);
                param_values[i] = param_strings[i].c_str();
            } else if constexpr (std::is_same_v<T, double>) {
                param_strings[i] = std::to_string(val);
                param_values[i] = param_strings[i].c_str();
            } else if constexpr (std::is_same_v<T, std::string>) {
                param_strings[i] = val;
                param_values[i] = param_strings[i].c_str();
            } else if constexpr (std::is_same_v<T, bool>) {
                param_strings[i] = val ? "TRUE" : "FALSE";
                param_values[i] = param_strings[i].c_str();
            } else if constexpr (std::is_same_v<T, std::vector<uint8_t>>) {
                static const char hex_digits[] = "0123456789abcdef";
                std::string hex = "\\x";
                hex.reserve(2 + val.size() * 2);
                for (uint8_t byte : val) {
                    hex.push_back(hex_digits[(byte >> 4) & 0x0F]);
                    hex.push_back(hex_digits[byte & 0x0F]);
                }
                param_strings[i] = std::move(hex);
                param_values[i] = param_strings[i].c_str();
            }
        }, p);
    }

    PGresult* res = PQexecParams(
        conn_,
        sql_.c_str(),
        nParams,
        nullptr,
        nParams > 0 ? param_values.data() : nullptr,
        nullptr,
        nullptr,
        0
    );

    if (!res) {
        throw DbException("PostgreSQL query execution failed: " + std::string(PQerrorMessage(conn_)));
    }

    ExecStatusType status = PQresultStatus(res);
    if (status != PGRES_TUPLES_OK && status != PGRES_COMMAND_OK) {
        std::string err = PQerrorMessage(conn_);
        PQclear(res);
        throw DbException("PostgreSQL query execution failed: " + err);
    }

    return std::make_unique<PgDataReader>(res);
}

size_t PgPreparedStatement::execute_non_query() {
    if (!conn_) {
        throw DbException("Invalid PostgreSQL connection");
    }

    int nParams = static_cast<int>(params_.size());
    std::vector<std::string> param_strings(nParams);
    std::vector<const char*> param_values(nParams, nullptr);

    for (int i = 0; i < nParams; ++i) {
        const auto& p = params_[i];
        std::visit([&](const auto& val) {
            using T = std::decay_t<decltype(val)>;
            if constexpr (std::is_same_v<T, std::monostate>) {
                param_values[i] = nullptr;
            } else if constexpr (std::is_same_v<T, int64_t>) {
                param_strings[i] = std::to_string(val);
                param_values[i] = param_strings[i].c_str();
            } else if constexpr (std::is_same_v<T, double>) {
                param_strings[i] = std::to_string(val);
                param_values[i] = param_strings[i].c_str();
            } else if constexpr (std::is_same_v<T, std::string>) {
                param_strings[i] = val;
                param_values[i] = param_strings[i].c_str();
            } else if constexpr (std::is_same_v<T, bool>) {
                param_strings[i] = val ? "TRUE" : "FALSE";
                param_values[i] = param_strings[i].c_str();
            } else if constexpr (std::is_same_v<T, std::vector<uint8_t>>) {
                static const char hex_digits[] = "0123456789abcdef";
                std::string hex = "\\x";
                hex.reserve(2 + val.size() * 2);
                for (uint8_t byte : val) {
                    hex.push_back(hex_digits[(byte >> 4) & 0x0F]);
                    hex.push_back(hex_digits[byte & 0x0F]);
                }
                param_strings[i] = std::move(hex);
                param_values[i] = param_strings[i].c_str();
            }
        }, p);
    }

    PGresult* res = PQexecParams(
        conn_,
        sql_.c_str(),
        nParams,
        nullptr,
        nParams > 0 ? param_values.data() : nullptr,
        nullptr,
        nullptr,
        0
    );

    if (!res) {
        throw DbException("PostgreSQL command execution failed: " + std::string(PQerrorMessage(conn_)));
    }

    ExecStatusType status = PQresultStatus(res);
    if (status != PGRES_COMMAND_OK && status != PGRES_TUPLES_OK) {
        std::string err = PQerrorMessage(conn_);
        PQclear(res);
        throw DbException("PostgreSQL command execution failed: " + err);
    }

    const char* tuples = PQcmdTuples(res);
    size_t affected = 0;
    if (tuples && tuples[0] != '\0') {
        affected = static_cast<size_t>(std::strtoull(tuples, nullptr, 10));
    }
    PQclear(res);
    return affected;
}

void PgPreparedStatement::cancel() {
    if (conn_) {
        PGcancel* cancel_obj = PQgetCancel(conn_);
        if (cancel_obj) {
            char errbuf[256];
            PQcancel(cancel_obj, errbuf, sizeof(errbuf));
            PQfreeCancel(cancel_obj);
        }
    }
}

void PgPreparedStatement::set_timeout(uint32_t seconds) {
    if (conn_) {
        std::string sql = "SET statement_timeout = " + std::to_string(seconds * 1000);
        PGresult* res = PQexec(conn_, sql.c_str());
        if (res) PQclear(res);
    }
}

void PgPreparedStatement::set_stop_token(std::stop_token token) {
    stop_token_ = token;
    if (token.stop_possible() && conn_) {
        stop_cb_.emplace(token, [this]() {
            cancel();
        });
    }
}

// ----------------------------------------------------------------------------
// PgConnection
// ----------------------------------------------------------------------------

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

static std::string get_reg_string(HKEY hKey, const char* subkey, const char* value_name) {
    HKEY hSubKey;
    if (RegOpenKeyExA(hKey, subkey, 0, KEY_READ, &hSubKey) != ERROR_SUCCESS) {
        return "";
    }
    char buf[1024] = {0};
    DWORD bufSize = sizeof(buf);
    DWORD type = 0;
    LONG res = RegQueryValueExA(hSubKey, value_name, nullptr, &type, reinterpret_cast<LPBYTE>(buf), &bufSize);
    RegCloseKey(hSubKey);
    if (res == ERROR_SUCCESS && (type == REG_SZ || type == REG_EXPAND_SZ)) {
        return std::string(buf);
    }
    return "";
}

static std::string resolve_odbc_dsn(const std::string& conn_str) {
    std::string dsn_name;
    if (conn_str.rfind("DSN=", 0) == 0 || conn_str.rfind("dsn=", 0) == 0) {
        size_t semi = conn_str.find(';');
        if (semi != std::string::npos) {
            dsn_name = conn_str.substr(4, semi - 4);
        } else {
            dsn_name = conn_str.substr(4);
        }
    } else if (conn_str.find('=') == std::string::npos &&
               conn_str.rfind("postgres://", 0) != 0 &&
               conn_str.rfind("postgresql://", 0) != 0) {
        dsn_name = conn_str;
    }

    if (dsn_name.empty()) {
        return conn_str;
    }

    std::string subkey = "Software\\ODBC\\ODBC.INI\\" + dsn_name;
    std::string server = get_reg_string(HKEY_CURRENT_USER, subkey.c_str(), "Servername");
    if (server.empty()) server = get_reg_string(HKEY_LOCAL_MACHINE, subkey.c_str(), "Servername");

    std::string db = get_reg_string(HKEY_CURRENT_USER, subkey.c_str(), "Database");
    if (db.empty()) db = get_reg_string(HKEY_LOCAL_MACHINE, subkey.c_str(), "Database");

    std::string user = get_reg_string(HKEY_CURRENT_USER, subkey.c_str(), "Username");
    if (user.empty()) user = get_reg_string(HKEY_CURRENT_USER, subkey.c_str(), "UID");
    if (user.empty()) user = get_reg_string(HKEY_LOCAL_MACHINE, subkey.c_str(), "Username");
    if (user.empty()) user = get_reg_string(HKEY_LOCAL_MACHINE, subkey.c_str(), "UID");

    std::string pwd = get_reg_string(HKEY_CURRENT_USER, subkey.c_str(), "Password");
    if (pwd.empty()) pwd = get_reg_string(HKEY_CURRENT_USER, subkey.c_str(), "PWD");
    if (pwd.empty()) pwd = get_reg_string(HKEY_LOCAL_MACHINE, subkey.c_str(), "Password");
    if (pwd.empty()) pwd = get_reg_string(HKEY_LOCAL_MACHINE, subkey.c_str(), "PWD");

    std::string port = get_reg_string(HKEY_CURRENT_USER, subkey.c_str(), "Port");
    if (port.empty()) port = get_reg_string(HKEY_LOCAL_MACHINE, subkey.c_str(), "Port");

    std::string ssl = get_reg_string(HKEY_CURRENT_USER, subkey.c_str(), "SSLmode");
    if (ssl.empty()) ssl = get_reg_string(HKEY_LOCAL_MACHINE, subkey.c_str(), "SSLmode");

    if (server.empty() && db.empty() && user.empty()) {
        return conn_str;
    }

    std::string result;
    if (!server.empty()) result += "host=" + server + " ";
    if (!port.empty()) result += "port=" + port + " ";
    if (!db.empty()) result += "dbname=" + db + " ";
    if (!user.empty()) result += "user=" + user + " ";
    if (!pwd.empty()) result += "password=" + pwd + " ";
    if (!ssl.empty()) result += "sslmode=" + ssl + " ";

    return result;
}
#endif

static std::string parse_odbc_conn_str(const std::string& conn_str) {
    if (conn_str.find("Driver=") == std::string::npos &&
        conn_str.find("driver=") == std::string::npos &&
        conn_str.find("Server=") == std::string::npos &&
        conn_str.find("server=") == std::string::npos) {
        return "";
    }

    std::string host, port, dbname, user, pwd, sslmode;
    size_t start = 0;
    while (start < conn_str.size()) {
        size_t end = conn_str.find(';', start);
        if (end == std::string::npos) end = conn_str.size();
        std::string token = conn_str.substr(start, end - start);
        start = end + 1;

        size_t eq = token.find('=');
        if (eq == std::string::npos) continue;

        std::string key = token.substr(0, eq);
        std::string val = token.substr(eq + 1);

        auto trim = [](std::string& s) {
            while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.erase(s.begin());
            while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.pop_back();
        };
        trim(key);
        trim(val);

        std::string lower_key = key;
        for (char& c : lower_key) c = static_cast<char>(::tolower(c));

        if (lower_key == "server" || lower_key == "host") host = val;
        else if (lower_key == "port") port = val;
        else if (lower_key == "database" || lower_key == "dbname" || lower_key == "db") dbname = val;
        else if (lower_key == "uid" || lower_key == "user" || lower_key == "username") user = val;
        else if (lower_key == "pwd" || lower_key == "password") pwd = val;
        else if (lower_key == "sslmode") sslmode = val;
    }

    if (host.empty() && dbname.empty() && user.empty()) return "";

    std::string result;
    if (!host.empty()) result += "host=" + host + " ";
    if (!port.empty()) result += "port=" + port + " ";
    if (!dbname.empty()) result += "dbname=" + dbname + " ";
    if (!user.empty()) result += "user=" + user + " ";
    if (!pwd.empty()) result += "password=" + pwd + " ";
    if (!sslmode.empty()) result += "sslmode=" + sslmode + " ";

    return result;
}

PgConnection::PgConnection(std::string connection_string)
    : connection_string_(std::move(connection_string))
    , conn_(nullptr)
{}

PgConnection::~PgConnection() {
    close();
}

void PgConnection::open() {
    if (is_open()) return;
    std::string effective_conn_str = parse_odbc_conn_str(connection_string_);
    if (effective_conn_str.empty()) {
#ifdef _WIN32
        effective_conn_str = resolve_odbc_dsn(connection_string_);
#else
        effective_conn_str = connection_string_;
#endif
    }
    conn_ = PQconnectdb(effective_conn_str.c_str());
    if (!conn_ || PQstatus(conn_) != CONNECTION_OK) {
        std::string err = conn_ ? PQerrorMessage(conn_) : "Unable to allocate PGconn";
        close();
        throw DbException("Failed to connect to PostgreSQL: " + err);
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
    if (!is_open()) {
        throw DbException("Cannot prepare statement: database connection is not open");
    }
    return std::make_unique<PgPreparedStatement>(conn_, sql);
}

void PgConnection::execute(std::string_view sql) {
    if (!is_open()) {
        throw DbException("Cannot execute statement: database connection is not open");
    }
    PGresult* res = PQexec(conn_, std::string(sql).c_str());
    if (!res) {
        throw DbException("PostgreSQL execute failed: " + std::string(PQerrorMessage(conn_)));
    }
    ExecStatusType status = PQresultStatus(res);
    if (status != PGRES_COMMAND_OK && status != PGRES_TUPLES_OK) {
        std::string err = PQerrorMessage(conn_);
        PQclear(res);
        throw DbException("PostgreSQL execute failed: " + err);
    }
    PQclear(res);
}

void PgConnection::begin_transaction() {
    execute("BEGIN");
}

void PgConnection::commit() {
    execute("COMMIT");
}

void PgConnection::rollback() {
    execute("ROLLBACK");
}

const ISqlDialect& PgConnection::dialect() const {
    return dialect_;
}

DriverInfo PgConnection::info() const {
    DriverInfo i;
    i.driver_name = "PostgreSQL (libpq)";
    i.driver_version = "16";
    i.dbms_name = "PostgreSQL";
    if (conn_) {
        int ver = PQserverVersion(conn_);
        i.dbms_version = std::to_string(ver / 10000) + "." + std::to_string((ver / 100) % 100);
    } else {
        i.dbms_version = "16.0";
    }
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
    caps.window_functions = true;
    caps.ctes = true;
    return caps;
}

// ----------------------------------------------------------------------------
// make_connection<postgres> Specialization
// ----------------------------------------------------------------------------

template <>
std::unique_ptr<IConnection> make_connection<postgres>(const std::string& connection_string) {
    auto conn = std::make_unique<PgConnection>(connection_string);
    return conn;
}

} // namespace cpplinq

#endif // CPPLINQ_HAS_POSTGRES
