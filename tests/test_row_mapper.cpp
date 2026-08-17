#include <gtest/gtest.h>
#include "cpplinq/mapping/row_mapper.h"
#include "cpplinq/core/column.h"
#include "cpplinq/core/table.h"
#include <string>
#include <optional>
#include <vector>
#include <variant>

using namespace cpplinq;

namespace {

class MockDataReader : public IDataReader {
public:
    using CellValue = std::variant<std::monostate, int64_t, double, std::string, bool, std::vector<uint8_t>>;
    using Row = std::vector<CellValue>;

    explicit MockDataReader(std::vector<Row> rows)
        : rows_(std::move(rows)), current_row_(-1) {}

    bool next() override {
        if (current_row_ + 1 < static_cast<int>(rows_.size())) {
            ++current_row_;
            return true;
        }
        return false;
    }

    int column_count() const override {
        if (current_row_ >= 0 && current_row_ < static_cast<int>(rows_.size())) {
            return static_cast<int>(rows_[current_row_].size());
        }
        return 0;
    }

    bool is_null(int col) const override {
        return std::holds_alternative<std::monostate>(rows_[current_row_][col]);
    }

    int64_t get_int64(int col) const override {
        const auto& cell = rows_[current_row_][col];
        if (std::holds_alternative<int64_t>(cell)) return std::get<int64_t>(cell);
        return 0;
    }

    double get_double(int col) const override {
        const auto& cell = rows_[current_row_][col];
        if (std::holds_alternative<double>(cell)) return std::get<double>(cell);
        return 0.0;
    }

    std::string get_string(int col) const override {
        const auto& cell = rows_[current_row_][col];
        if (std::holds_alternative<std::string>(cell)) return std::get<std::string>(cell);
        return {};
    }

    bool get_bool(int col) const override {
        const auto& cell = rows_[current_row_][col];
        if (std::holds_alternative<bool>(cell)) return std::get<bool>(cell);
        return false;
    }

    std::vector<uint8_t> get_blob(int col) const override {
        const auto& cell = rows_[current_row_][col];
        if (std::holds_alternative<std::vector<uint8_t>>(cell)) return std::get<std::vector<uint8_t>>(cell);
        return {};
    }

private:
    std::vector<Row> rows_;
    int current_row_;
};

struct Person {
    int id = 0;
    std::string name;
    int age = 0;
    double score = 0.0;
    bool is_active = false;
};

struct NullableRecord {
    int id = 0;
    std::optional<std::string> nickname;
    std::optional<int> favorite_number;
    std::optional<double> rating;
    std::optional<bool> verified;
};

struct BlobRecord {
    int id = 0;
    std::vector<uint8_t> data;
};

template <typename Entity, typename... Cols>
auto create_mapper(const std::tuple<Cols...>& cols) {
    return RowMapper<Entity, Cols...>(cols);
}

} // namespace

// ============================================================================
// RowMapper Basic Mapping Tests
// ============================================================================

TEST(RowMapperTest, MapPrimitiveFields) {
    auto cols = std::make_tuple(
        column("id", &Person::id),
        column("name", &Person::name),
        column("age", &Person::age),
        column("score", &Person::score),
        column("is_active", &Person::is_active)
    );

    auto mapper = create_mapper<Person>(cols);

    std::vector<MockDataReader::Row> data = {
        {int64_t(1), std::string("Alice"), int64_t(30), double(95.5), true},
        {int64_t(2), std::string("Bob"), int64_t(25), double(82.0), false}
    };

    MockDataReader reader(data);

    ASSERT_TRUE(reader.next());
    Person p1 = mapper.map_row(reader);
    EXPECT_EQ(p1.id, 1);
    EXPECT_EQ(p1.name, "Alice");
    EXPECT_EQ(p1.age, 30);
    EXPECT_DOUBLE_EQ(p1.score, 95.5);
    EXPECT_TRUE(p1.is_active);

    ASSERT_TRUE(reader.next());
    Person p2 = mapper.map_row(reader);
    EXPECT_EQ(p2.id, 2);
    EXPECT_EQ(p2.name, "Bob");
    EXPECT_EQ(p2.age, 25);
    EXPECT_DOUBLE_EQ(p2.score, 82.0);
    EXPECT_FALSE(p2.is_active);

    EXPECT_FALSE(reader.next());
}

TEST(RowMapperTest, MapInPlaceExistingEntity) {
    auto cols = std::make_tuple(
        column("id", &Person::id),
        column("name", &Person::name),
        column("age", &Person::age),
        column("score", &Person::score),
        column("is_active", &Person::is_active)
    );

    auto mapper = create_mapper<Person>(cols);

    std::vector<MockDataReader::Row> data = {
        {int64_t(42), std::string("Charlie"), int64_t(33), double(88.5), true}
    };

    MockDataReader reader(data);
    ASSERT_TRUE(reader.next());
    Person p;
    mapper.map_row(reader, p);
    EXPECT_EQ(p.id, 42);
    EXPECT_EQ(p.name, "Charlie");
    EXPECT_EQ(p.age, 33);
    EXPECT_DOUBLE_EQ(p.score, 88.5);
    EXPECT_TRUE(p.is_active);
}

// ============================================================================
// RowMapper Nullable / Optional Tests
// ============================================================================

TEST(RowMapperTest, MapNullableFieldsWithValue) {
    auto cols = std::make_tuple(
        column("id", &NullableRecord::id),
        column("nickname", &NullableRecord::nickname),
        column("favorite_number", &NullableRecord::favorite_number),
        column("rating", &NullableRecord::rating),
        column("verified", &NullableRecord::verified)
    );

    auto mapper = create_mapper<NullableRecord>(cols);

    std::vector<MockDataReader::Row> data = {
        {int64_t(10), std::string("Ace"), int64_t(7), double(4.8), true}
    };

    MockDataReader reader(data);
    ASSERT_TRUE(reader.next());
    NullableRecord rec = mapper.map_row(reader);

    EXPECT_EQ(rec.id, 10);
    ASSERT_TRUE(rec.nickname.has_value());
    EXPECT_EQ(*rec.nickname, "Ace");
    ASSERT_TRUE(rec.favorite_number.has_value());
    EXPECT_EQ(*rec.favorite_number, 7);
    ASSERT_TRUE(rec.rating.has_value());
    EXPECT_DOUBLE_EQ(*rec.rating, 4.8);
    ASSERT_TRUE(rec.verified.has_value());
    EXPECT_TRUE(*rec.verified);
}

TEST(RowMapperTest, MapNullableFieldsWithNulls) {
    auto cols = std::make_tuple(
        column("id", &NullableRecord::id),
        column("nickname", &NullableRecord::nickname),
        column("favorite_number", &NullableRecord::favorite_number),
        column("rating", &NullableRecord::rating),
        column("verified", &NullableRecord::verified)
    );

    auto mapper = create_mapper<NullableRecord>(cols);

    std::vector<MockDataReader::Row> data = {
        {int64_t(20), std::monostate{}, std::monostate{}, std::monostate{}, std::monostate{}}
    };

    MockDataReader reader(data);
    ASSERT_TRUE(reader.next());
    NullableRecord rec = mapper.map_row(reader);

    EXPECT_EQ(rec.id, 20);
    EXPECT_FALSE(rec.nickname.has_value());
    EXPECT_FALSE(rec.favorite_number.has_value());
    EXPECT_FALSE(rec.rating.has_value());
    EXPECT_FALSE(rec.verified.has_value());
}

// ============================================================================
// RowMapper Blob Test
// ============================================================================

TEST(RowMapperTest, MapBlobField) {
    auto cols = std::make_tuple(
        column("id", &BlobRecord::id),
        column("data", &BlobRecord::data)
    );

    auto mapper = create_mapper<BlobRecord>(cols);

    std::vector<uint8_t> test_bytes = {0xDE, 0xAD, 0xBE, 0xEF, 0x01, 0x02};
    std::vector<MockDataReader::Row> data = {
        {int64_t(1), test_bytes}
    };

    MockDataReader reader(data);
    ASSERT_TRUE(reader.next());
    BlobRecord rec = mapper.map_row(reader);

    EXPECT_EQ(rec.id, 1);
    EXPECT_EQ(rec.data, test_bytes);
}

// ============================================================================
// field_to_bound_value Tests
// ============================================================================

TEST(RowMapperTest, FieldToBoundValuePrimitives) {
    Person p{1, "Alice", 30, 95.5, true};

    BoundValue bv_id = field_to_bound_value(p, &Person::id);
    EXPECT_EQ(std::get<int64_t>(bv_id), 1);

    BoundValue bv_name = field_to_bound_value(p, &Person::name);
    EXPECT_EQ(std::get<std::string>(bv_name), "Alice");

    BoundValue bv_age = field_to_bound_value(p, &Person::age);
    EXPECT_EQ(std::get<int64_t>(bv_age), 30);

    BoundValue bv_score = field_to_bound_value(p, &Person::score);
    EXPECT_DOUBLE_EQ(std::get<double>(bv_score), 95.5);

    BoundValue bv_active = field_to_bound_value(p, &Person::is_active);
    EXPECT_EQ(std::get<bool>(bv_active), true);
}

TEST(RowMapperTest, FieldToBoundValueOptionals) {
    NullableRecord rec_with_vals{1, "Nick", 42, 3.14, false};
    BoundValue bv_nick = field_to_bound_value(rec_with_vals, &NullableRecord::nickname);
    EXPECT_EQ(std::get<std::string>(bv_nick), "Nick");

    BoundValue bv_num = field_to_bound_value(rec_with_vals, &NullableRecord::favorite_number);
    EXPECT_EQ(std::get<int64_t>(bv_num), 42);

    NullableRecord rec_with_nulls{2, std::nullopt, std::nullopt, std::nullopt, std::nullopt};
    BoundValue bv_null_nick = field_to_bound_value(rec_with_nulls, &NullableRecord::nickname);
    EXPECT_TRUE(std::holds_alternative<std::monostate>(bv_null_nick));

    BoundValue bv_null_num = field_to_bound_value(rec_with_nulls, &NullableRecord::favorite_number);
    EXPECT_TRUE(std::holds_alternative<std::monostate>(bv_null_num));
}
