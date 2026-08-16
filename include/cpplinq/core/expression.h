#pragma once
#include <string>
#include <string_view>
#include <variant>
#include <memory>
#include <optional>
#include <cstdint>
#include <utility>
#include <type_traits>

namespace cpplinq {
namespace expr {

// SQL value variant representing literal values or NULL (monostate)
using SqlValue = std::variant<std::monostate, int64_t, double, std::string, bool>;

// Comparison operators
enum class CompareOp {
    Eq,
    Ne,
    Lt,
    Le,
    Gt,
    Ge
};

// Logical operators
enum class LogicOp {
    And,
    Or
};

// Unary operators
enum class UnaryOp {
    Not,
    IsNull,
    IsNotNull
};

// Sort direction
enum class SortDir {
    Asc,
    Desc
};

// AST forward declarations
struct BinaryExpr;
struct LogicExpr;
struct UnaryExpr;

// Column reference node
struct ColumnRef {
    std::string table_name;
    std::string column_name;

    ColumnRef() = default;
    ColumnRef(std::string col) : column_name(std::move(col)) {}
    ColumnRef(std::string tbl, std::string col)
        : table_name(std::move(tbl)), column_name(std::move(col)) {}

    bool operator==(const ColumnRef& other) const = default;
};

// Literal node
struct Literal {
    SqlValue value;

    Literal() : value(std::monostate{}) {}
    Literal(SqlValue val) : value(std::move(val)) {}
};

// Expression AST node variant
using ExprNode = std::variant<
    ColumnRef,
    Literal,
    std::shared_ptr<BinaryExpr>,
    std::shared_ptr<LogicExpr>,
    std::shared_ptr<UnaryExpr>
>;

// Binary expression AST node (comparisons)
struct BinaryExpr {
    ExprNode left;
    CompareOp op;
    ExprNode right;

    BinaryExpr(ExprNode l, CompareOp o, ExprNode r)
        : left(std::move(l)), op(o), right(std::move(r)) {}
};

// Logical expression AST node (AND, OR)
struct LogicExpr {
    ExprNode left;
    LogicOp op;
    ExprNode right;

    LogicExpr(ExprNode l, LogicOp o, ExprNode r)
        : left(std::move(l)), op(o), right(std::move(r)) {}
};

// Unary expression AST node (NOT, IS NULL, IS NOT NULL)
struct UnaryExpr {
    UnaryOp op;
    ExprNode operand;

    UnaryExpr(UnaryOp o, ExprNode opnd)
        : op(o), operand(std::move(opnd)) {}
};

// Assignment expression for UPDATE SET clauses
struct AssignExpr {
    ColumnRef column;
    ExprNode value;

    AssignExpr() = default;
    AssignExpr(ColumnRef col, ExprNode val)
        : column(std::move(col)), value(std::move(val)) {}
};

// Expression wrapper class
class Expr {
public:
    ExprNode node;

    Expr() : node(Literal{SqlValue{std::monostate{}}}) {}
    Expr(ExprNode n) : node(std::move(n)) {}
    Expr(ColumnRef ref) : node(std::move(ref)) {}
    Expr(Literal lit) : node(std::move(lit)) {}

    // Implicit constructors from standard C++ types
    Expr(int val) : node(Literal{SqlValue(static_cast<int64_t>(val))}) {}
    Expr(int64_t val) : node(Literal{SqlValue(val)}) {}
    Expr(double val) : node(Literal{SqlValue(val)}) {}
    Expr(float val) : node(Literal{SqlValue(static_cast<double>(val))}) {}
    Expr(const char* val) : node(Literal{SqlValue(std::string(val))}) {}
    Expr(std::string val) : node(Literal{SqlValue(std::move(val))}) {}
    Expr(std::string_view val) : node(Literal{SqlValue(std::string(val))}) {}
    Expr(bool val) : node(Literal{SqlValue(val)}) {}
    Expr(std::nullopt_t) : node(Literal{SqlValue(std::monostate{})}) {}

    template <typename T>
    Expr(const std::optional<T>& opt) {
        if (opt.has_value()) {
            *this = Expr(*opt);
        } else {
            node = Literal{SqlValue(std::monostate{})};
        }
    }

    // Unary helper methods
    Expr is_null() const {
        return Expr(std::make_shared<UnaryExpr>(UnaryOp::IsNull, node));
    }

    Expr is_not_null() const {
        return Expr(std::make_shared<UnaryExpr>(UnaryOp::IsNotNull, node));
    }
};

// ColumnHandle class representing a table column in expressions
class ColumnHandle {
public:
    ColumnRef ref;

    ColumnHandle() = default;
    ColumnHandle(ColumnRef r) : ref(std::move(r)) {}
    ColumnHandle(std::string table, std::string col)
        : ref(std::move(table), std::move(col)) {}
    ColumnHandle(std::string col)
        : ref(std::move(col)) {}

    // Implicit conversion to Expr
    operator Expr() const {
        return Expr(ref);
    }

    // Template operator= for UPDATE SET clauses
    template <typename T>
    AssignExpr operator=(T&& val) const {
        return AssignExpr{ref, Expr(std::forward<T>(val)).node};
    }

    AssignExpr operator=(const ColumnHandle& other) const {
        return AssignExpr{ref, Expr(other).node};
    }

    AssignExpr operator=(const Expr& other) const {
        return AssignExpr{ref, other.node};
    }

    Expr is_null() const {
        return Expr(ref).is_null();
    }

    Expr is_not_null() const {
        return Expr(ref).is_not_null();
    }
};

// Comparison operators (==, !=, <, <=, >, >=)
inline Expr operator==(const Expr& lhs, const Expr& rhs) {
    return Expr(std::make_shared<BinaryExpr>(lhs.node, CompareOp::Eq, rhs.node));
}

inline Expr operator!=(const Expr& lhs, const Expr& rhs) {
    return Expr(std::make_shared<BinaryExpr>(lhs.node, CompareOp::Ne, rhs.node));
}

inline Expr operator<(const Expr& lhs, const Expr& rhs) {
    return Expr(std::make_shared<BinaryExpr>(lhs.node, CompareOp::Lt, rhs.node));
}

inline Expr operator<=(const Expr& lhs, const Expr& rhs) {
    return Expr(std::make_shared<BinaryExpr>(lhs.node, CompareOp::Le, rhs.node));
}

inline Expr operator>(const Expr& lhs, const Expr& rhs) {
    return Expr(std::make_shared<BinaryExpr>(lhs.node, CompareOp::Gt, rhs.node));
}

inline Expr operator>=(const Expr& lhs, const Expr& rhs) {
    return Expr(std::make_shared<BinaryExpr>(lhs.node, CompareOp::Ge, rhs.node));
}

// Logical operators (&&, ||, !)
inline Expr operator&&(const Expr& lhs, const Expr& rhs) {
    return Expr(std::make_shared<LogicExpr>(lhs.node, LogicOp::And, rhs.node));
}

inline Expr operator||(const Expr& lhs, const Expr& rhs) {
    return Expr(std::make_shared<LogicExpr>(lhs.node, LogicOp::Or, rhs.node));
}

inline Expr operator!(const Expr& operand) {
    return Expr(std::make_shared<UnaryExpr>(UnaryOp::Not, operand.node));
}

// Helper functions for unary IS NULL / IS NOT NULL
inline Expr is_null(const Expr& e) {
    return e.is_null();
}

inline Expr is_not_null(const Expr& e) {
    return e.is_not_null();
}

// Order-by expression and helpers
struct OrderByExpr {
    ExprNode expr;
    SortDir direction = SortDir::Asc;
};

inline OrderByExpr asc(const Expr& e) {
    return OrderByExpr{e.node, SortDir::Asc};
}

inline OrderByExpr desc(const Expr& e) {
    return OrderByExpr{e.node, SortDir::Desc};
}

} // namespace expr

// Export types to namespace cpplinq
using expr::SqlValue;
using expr::CompareOp;
using expr::LogicOp;
using expr::UnaryOp;
using expr::SortDir;
using expr::ColumnRef;
using expr::Literal;
using expr::BinaryExpr;
using expr::LogicExpr;
using expr::UnaryExpr;
using expr::ExprNode;
using expr::AssignExpr;
using expr::Expr;
using expr::ColumnHandle;
using expr::OrderByExpr;
using expr::is_null;
using expr::is_not_null;
using expr::asc;
using expr::desc;

} // namespace cpplinq
