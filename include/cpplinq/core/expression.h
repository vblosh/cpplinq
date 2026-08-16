#pragma once
#include <string>
#include <string_view>
#include <vector>
#include <initializer_list>
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
struct BetweenExpr;
struct LikeExpr;
struct InListExpr;
struct FunctionExpr;
struct SubqueryExpr;
struct ExistsExpr;
struct InSubqueryExpr;

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
    std::shared_ptr<UnaryExpr>,
    std::shared_ptr<BetweenExpr>,
    std::shared_ptr<LikeExpr>,
    std::shared_ptr<InListExpr>,
    std::shared_ptr<FunctionExpr>,
    std::shared_ptr<SubqueryExpr>,
    std::shared_ptr<ExistsExpr>,
    std::shared_ptr<InSubqueryExpr>
>;

// Subquery expression AST node: (SELECT col1, col2 FROM tbl WHERE ...)
struct SubqueryExpr {
    std::string table_name;
    std::vector<std::string> select_columns;
    std::shared_ptr<ExprNode> where;
    bool is_distinct = false;

    SubqueryExpr() = default;
    SubqueryExpr(
        std::string tbl,
        std::vector<std::string> cols = {},
        std::shared_ptr<ExprNode> w = nullptr,
        bool dist = false
    ) : table_name(std::move(tbl)),
        select_columns(std::move(cols)),
        where(std::move(w)),
        is_distinct(dist) {}

    SubqueryExpr(
        std::string tbl,
        std::vector<std::string> cols,
        ExprNode w,
        bool dist = false
    ) : table_name(std::move(tbl)),
        select_columns(std::move(cols)),
        where(std::make_shared<ExprNode>(std::move(w))),
        is_distinct(dist) {}
};

// EXISTS expression AST node: EXISTS (SELECT ... FROM ...)
struct ExistsExpr {
    SubqueryExpr subquery;
    bool is_not = false;

    ExistsExpr(SubqueryExpr sub, bool n = false)
        : subquery(std::move(sub)), is_not(n) {}
};

// IN (subquery) expression AST node: col IN (SELECT col FROM ...)
struct InSubqueryExpr {
    ExprNode expr;
    SubqueryExpr subquery;
    bool is_not = false;

    InSubqueryExpr(ExprNode e, SubqueryExpr sub, bool n = false)
        : expr(std::move(e)), subquery(std::move(sub)), is_not(n) {}
};

// Function call expression AST node: FUNC(arg1, arg2, ...)
struct FunctionExpr {
    std::string function_name;
    std::vector<ExprNode> arguments;

    FunctionExpr(std::string name, std::vector<ExprNode> args)
        : function_name(std::move(name)), arguments(std::move(args)) {}
};

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

// Between expression AST node: col BETWEEN low AND high
struct BetweenExpr {
    ExprNode expr;
    ExprNode low;
    ExprNode high;
    bool is_not = false;

    BetweenExpr(ExprNode e, ExprNode l, ExprNode h, bool n = false)
        : expr(std::move(e)), low(std::move(l)), high(std::move(h)), is_not(n) {}
};

// LIKE expression AST node: col LIKE pattern
struct LikeExpr {
    ExprNode expr;
    ExprNode pattern;
    bool is_not = false;

    LikeExpr(ExprNode e, ExprNode p, bool n = false)
        : expr(std::move(e)), pattern(std::move(p)), is_not(n) {}
};

// IN expression AST node: col IN (val1, val2, ...)
struct InListExpr {
    ExprNode expr;
    std::vector<ExprNode> values;
    bool is_not = false;

    InListExpr(ExprNode e, std::vector<ExprNode> v, bool n = false)
        : expr(std::move(e)), values(std::move(v)), is_not(n) {}
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

    Expr between(const Expr& low, const Expr& high) const {
        return Expr(std::make_shared<BetweenExpr>(node, low.node, high.node, false));
    }

    Expr not_between(const Expr& low, const Expr& high) const {
        return Expr(std::make_shared<BetweenExpr>(node, low.node, high.node, true));
    }

    Expr like(const Expr& pattern) const {
        return Expr(std::make_shared<LikeExpr>(node, pattern.node, false));
    }

    Expr not_like(const Expr& pattern) const {
        return Expr(std::make_shared<LikeExpr>(node, pattern.node, true));
    }

    template <typename T>
    Expr in_list(const std::vector<T>& values) const {
        std::vector<ExprNode> val_nodes;
        val_nodes.reserve(values.size());
        for (const auto& v : values) {
            val_nodes.push_back(Expr(v).node);
        }
        return Expr(std::make_shared<InListExpr>(node, std::move(val_nodes), false));
    }

    template <typename T>
    Expr in_list(std::initializer_list<T> values) const {
        std::vector<ExprNode> val_nodes;
        val_nodes.reserve(values.size());
        for (const auto& v : values) {
            val_nodes.push_back(Expr(v).node);
        }
        return Expr(std::make_shared<InListExpr>(node, std::move(val_nodes), false));
    }

    template <typename T>
    Expr not_in_list(const std::vector<T>& values) const {
        std::vector<ExprNode> val_nodes;
        val_nodes.reserve(values.size());
        for (const auto& v : values) {
            val_nodes.push_back(Expr(v).node);
        }
        return Expr(std::make_shared<InListExpr>(node, std::move(val_nodes), true));
    }

    template <typename T>
    Expr not_in_list(std::initializer_list<T> values) const {
        std::vector<ExprNode> val_nodes;
        val_nodes.reserve(values.size());
        for (const auto& v : values) {
            val_nodes.push_back(Expr(v).node);
        }
        return Expr(std::make_shared<InListExpr>(node, std::move(val_nodes), true));
    }

    Expr lower() const {
        return Expr(std::make_shared<FunctionExpr>("LOWER", std::vector<ExprNode>{node}));
    }

    Expr upper() const {
        return Expr(std::make_shared<FunctionExpr>("UPPER", std::vector<ExprNode>{node}));
    }

    Expr length() const {
        return Expr(std::make_shared<FunctionExpr>("LENGTH", std::vector<ExprNode>{node}));
    }

    Expr trim() const {
        return Expr(std::make_shared<FunctionExpr>("TRIM", std::vector<ExprNode>{node}));
    }

    Expr substr(const Expr& start, const Expr& len) const {
        return Expr(std::make_shared<FunctionExpr>("SUBSTR", std::vector<ExprNode>{node, start.node, len.node}));
    }

    Expr abs_val() const {
        return Expr(std::make_shared<FunctionExpr>("ABS", std::vector<ExprNode>{node}));
    }

    Expr round_val(const Expr& decimals = 0) const {
        return Expr(std::make_shared<FunctionExpr>("ROUND", std::vector<ExprNode>{node, decimals.node}));
    }

    Expr coalesce(const Expr& fallback) const {
        return Expr(std::make_shared<FunctionExpr>("COALESCE", std::vector<ExprNode>{node, fallback.node}));
    }

    Expr in(SubqueryExpr sub) const {
        return Expr(std::make_shared<InSubqueryExpr>(node, std::move(sub), false));
    }

    Expr not_in(SubqueryExpr sub) const {
        return Expr(std::make_shared<InSubqueryExpr>(node, std::move(sub), true));
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

    Expr between(const Expr& low, const Expr& high) const {
        return Expr(ref).between(low, high);
    }

    Expr not_between(const Expr& low, const Expr& high) const {
        return Expr(ref).not_between(low, high);
    }

    Expr like(const Expr& pattern) const {
        return Expr(ref).like(pattern);
    }

    Expr not_like(const Expr& pattern) const {
        return Expr(ref).not_like(pattern);
    }

    template <typename T>
    Expr in_list(const std::vector<T>& values) const {
        return Expr(ref).in_list(values);
    }

    template <typename T>
    Expr in_list(std::initializer_list<T> values) const {
        return Expr(ref).in_list(values);
    }

    template <typename T>
    Expr not_in_list(const std::vector<T>& values) const {
        return Expr(ref).not_in_list(values);
    }

    template <typename T>
    Expr not_in_list(std::initializer_list<T> values) const {
        return Expr(ref).not_in_list(values);
    }

    Expr in(SubqueryExpr sub) const {
        return Expr(ref).in(std::move(sub));
    }

    Expr not_in(SubqueryExpr sub) const {
        return Expr(ref).not_in(std::move(sub));
    }

    Expr lower() const {
        return Expr(ref).lower();
    }

    Expr upper() const {
        return Expr(ref).upper();
    }

    Expr length() const {
        return Expr(ref).length();
    }

    Expr trim() const {
        return Expr(ref).trim();
    }

    Expr substr(const Expr& start, const Expr& len) const {
        return Expr(ref).substr(start, len);
    }

    Expr abs_val() const {
        return Expr(ref).abs_val();
    }

    Expr round_val(const Expr& decimals = 0) const {
        return Expr(ref).round_val(decimals);
    }

    Expr coalesce(const Expr& fallback) const {
        return Expr(ref).coalesce(fallback);
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

// Built-in SQL functions
inline Expr lower(const Expr& e) {
    return e.lower();
}

inline Expr upper(const Expr& e) {
    return e.upper();
}

inline Expr length(const Expr& e) {
    return e.length();
}

inline Expr trim(const Expr& e) {
    return e.trim();
}

inline Expr substr(const Expr& e, const Expr& start, const Expr& len) {
    return e.substr(start, len);
}

inline Expr abs_val(const Expr& e) {
    return e.abs_val();
}

inline Expr round_val(const Expr& e, const Expr& decimals = 0) {
    return e.round_val(decimals);
}

inline Expr coalesce(const Expr& e, const Expr& fallback) {
    return e.coalesce(fallback);
}

inline Expr exists(SubqueryExpr sub) {
    return Expr(std::make_shared<ExistsExpr>(std::move(sub), false));
}

inline Expr not_exists(SubqueryExpr sub) {
    return Expr(std::make_shared<ExistsExpr>(std::move(sub), true));
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
using expr::BetweenExpr;
using expr::LikeExpr;
using expr::InListExpr;
using expr::FunctionExpr;
using expr::SubqueryExpr;
using expr::ExistsExpr;
using expr::InSubqueryExpr;
using expr::ExprNode;
using expr::AssignExpr;
using expr::Expr;
using expr::ColumnHandle;
using expr::OrderByExpr;
using expr::is_null;
using expr::is_not_null;
using expr::lower;
using expr::upper;
using expr::length;
using expr::trim;
using expr::substr;
using expr::abs_val;
using expr::round_val;
using expr::coalesce;
using expr::exists;
using expr::not_exists;
using expr::asc;
using expr::desc;

} // namespace cpplinq
