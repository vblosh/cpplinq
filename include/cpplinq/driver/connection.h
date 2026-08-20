#pragma once
#include "cpplinq/dialect/dialect.h"
#include "cpplinq/mapping/data_types.h"
#include <memory>
#include <string>
#include <string_view>
#include <variant>
#include <vector>
#include <optional>
#include <cstdint>
#include <stdexcept>
#include <stop_token>
#include <functional>

namespace cpplinq {

// Database error
class DbException : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Specific cancellation exception
class OperationCancelled : public DbException {
public:
    explicit OperationCancelled(const std::string& msg = "Operation cancelled by stop token or user request")
        : DbException(msg) {}
};
using operation_cancelled = OperationCancelled;

// Unsupported feature exception
class UnsupportedFeature : public DbException {
public:
    explicit UnsupportedFeature(const std::string& msg = "Requested operation is not supported by driver/dialect")
        : DbException(msg) {}
};
using unsupported_feature = UnsupportedFeature;

// Execution options for queries and streaming
struct ExecutionOptions {
    std::optional<uint32_t> query_timeout_seconds;
    std::optional<std::stop_token> stop_token;
};
using execution_options = ExecutionOptions;

// Driver info introspection
struct DriverInfo {
    std::string driver_name;
    std::string driver_version;
    std::string dbms_name;
    std::string dbms_version;
    std::string odbc_version;
};

// Driver capabilities introspection
struct DriverCapabilities {
    bool cancel = true;
    bool streaming = true;
    bool query_timeout = true;
    bool transactions = true;
    bool savepoints = false;
    bool returning_clause = false;
    bool output_clause = false;
    bool upsert = false;
    bool array_batch_insert = false;
    size_t default_batch_chunk_size = 1000;
    bool window_functions = true;
    bool ctes = true;
};

// Forward declaration of streaming range
class RowStream;

// Result row reader (cursor-based)
class IDataReader {
public:
    virtual ~IDataReader() = default;

    virtual bool next() = 0;  // advance to next row, returns false when done
    virtual int  column_count() const = 0;

    virtual bool         is_null(int col) const = 0;
    virtual int64_t      get_int64(int col) const = 0;
    virtual uint64_t     get_uint64(int col) const = 0;
    virtual double       get_double(int col) const = 0;
    virtual std::string  get_string(int col) const = 0;
    virtual std::string_view get_string_view(int /*col*/) const {
        return {};
    }
    virtual std::wstring get_wstring(int col) const = 0;
    virtual bool         get_bool(int col) const = 0;
    virtual std::vector<uint8_t> get_blob(int col) const = 0;
    virtual SqlNumeric   get_numeric(int col) const = 0;
    virtual SqlDate      get_date(int col) const = 0;
    virtual SqlTime      get_time(int col) const = 0;
    virtual SqlTimestamp get_timestamp(int col) const = 0;
    virtual SqlInterval  get_interval(int col) const = 0;
    virtual SqlGuid      get_guid(int col) const = 0;
    virtual BoundValue   get_value(int col) const {
        if (is_null(col)) return std::monostate{};
        return get_string(col);
    }
};

// Prepared statement
class IPreparedStatement {
public:
    virtual ~IPreparedStatement() = default;

    virtual void bind(int index, const BoundValue& value) = 0;
    virtual std::unique_ptr<IDataReader> execute_query() = 0;
    virtual size_t execute_non_query() = 0;  // returns affected rows
    virtual void reset() = 0;

    // Cancellation and execution options
    virtual void cancel() {
        throw UnsupportedFeature("Prepared statement cancellation is not supported by this driver");
    }
    virtual void set_timeout(uint32_t seconds) { (void)seconds; }
    virtual void set_stop_token(std::stop_token token) { (void)token; }
};

// Database connection
class IConnection {
public:
    virtual ~IConnection() = default;

    virtual void open() = 0;
    virtual void close() = 0;
    virtual bool is_open() const = 0;

    virtual std::unique_ptr<IPreparedStatement> prepare(std::string_view sql) = 0;
    virtual void execute(std::string_view sql) = 0;  // direct exec, no results

    // Direct execution fast path for parameterless queries
    virtual std::unique_ptr<IDataReader> execute_query_direct(std::string_view sql);
    virtual size_t execute_non_query_direct(std::string_view sql);

    virtual void begin_transaction() = 0;
    virtual void commit() = 0;
    virtual void rollback() = 0;

    virtual const ISqlDialect& dialect() const = 0;

    // Driver capabilities & information
    virtual DriverInfo info() const = 0;
    virtual DriverCapabilities capabilities() const = 0;

    // Batch insert/update using array binding (or fallback loop)
    virtual size_t insert_many_batch(
        std::string_view sql,
        const std::vector<BoundValue>& flat_params,
        size_t col_count,
        size_t row_count
    );

    // Streaming range over raw query
    virtual RowStream stream(
        std::string_view sql,
        const std::vector<BoundValue>& params = {},
        ExecutionOptions options = {}
    );
};

// RAII transaction guard
class Transaction {
public:
    explicit Transaction(IConnection& conn) : conn_(conn), committed_(false) {
        conn_.begin_transaction();
    }
    void commit() { conn_.commit(); committed_ = true; }
    ~Transaction() { if (!committed_) try { conn_.rollback(); } catch(...) {} }

    Transaction(const Transaction&) = delete;
    Transaction& operator=(const Transaction&) = delete;
private:
    IConnection& conn_;
    bool committed_;
};

// Backend tag types
struct sqlite {};
struct postgres {};
struct mssql {};
using sqlserver = mssql;
struct mysql {};
using mariadb = mysql;
struct informix {};
struct oracle {};

// Connection factory (specializations in driver .cpp files)
template <typename Backend>
std::unique_ptr<IConnection> make_connection(const std::string& connection_string);

} // namespace cpplinq
