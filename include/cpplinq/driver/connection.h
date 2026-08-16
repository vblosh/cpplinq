#pragma once
#include "cpplinq/dialect/dialect.h"
#include <memory>
#include <string>
#include <string_view>
#include <variant>
#include <vector>
#include <optional>
#include <cstdint>
#include <stdexcept>

namespace cpplinq {

// Type-erased bound parameter
using BoundValue = std::variant<
    std::monostate,           // NULL
    int64_t,
    double,
    std::string,
    bool,
    std::vector<uint8_t>      // BLOB
>;

// Database error
class DbException : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Result row reader (cursor-based)
class IDataReader {
public:
    virtual ~IDataReader() = default;

    virtual bool next() = 0;  // advance to next row, returns false when done
    virtual int  column_count() const = 0;

    virtual bool        is_null(int col) const = 0;
    virtual int64_t     get_int64(int col) const = 0;
    virtual double      get_double(int col) const = 0;
    virtual std::string get_string(int col) const = 0;
    virtual bool        get_bool(int col) const = 0;
    virtual std::vector<uint8_t> get_blob(int col) const = 0;
};

// Prepared statement
class IPreparedStatement {
public:
    virtual ~IPreparedStatement() = default;

    virtual void bind(int index, const BoundValue& value) = 0;
    virtual std::unique_ptr<IDataReader> execute_query() = 0;
    virtual size_t execute_non_query() = 0;  // returns affected rows
    virtual void reset() = 0;
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

    virtual void begin_transaction() = 0;
    virtual void commit() = 0;
    virtual void rollback() = 0;

    virtual const ISqlDialect& dialect() const = 0;
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

// Connection factory (specializations in driver .cpp files)
template <typename Backend>
std::unique_ptr<IConnection> make_connection(const std::string& connection_string);

} // namespace cpplinq
