#include <gtest/gtest.h>
#include "cpplinq/mapping/row_mapper.h"
#include "cpplinq/core/column.h"
#include "cpplinq/core/table.h"
#include <string>
#include <optional>
#include <vector>
#include <variant>
#include <chrono>

using namespace cpplinq;

namespace {

class MockDataReader : public IDataReader {
public:
    using CellValue = std::variant<
        std::monostate,
        int64_t,
        uint64_t,
        double,
        std::string,
        std::wstring,
        bool,
        std::vector<uint8_t>,
        SqlNumeric,
        SqlDate,
        SqlTime,
        SqlTimestamp,
        SqlInterval,
        SqlGuid
    >;
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
        if (std::holds_alternative<uint64_t>(cell)) return static_cast<int64_t>(std::get<uint64_t>(cell));
        return 0;
    }

    uint64_t get_uint64(int col) const override {
        const auto& cell = rows_[current_row_][col];
        if (std::holds_alternative<uint64_t>(cell)) return std::get<uint64_t>(cell);
        if (std::holds_alternative<int64_t>(cell)) return static_cast<uint64_t>(std::get<int64_t>(cell));
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
        if (std::holds_alternative<std::wstring>(cell)) return wstring_to_utf8(std::get<std::wstring>(cell));
        return {};
    }

    std::string_view get_string_view(int col) const override {
        const auto& cell = rows_[current_row_][col];
        if (std::holds_alternative<std::string>(cell)) return std::get<std::string>(cell);
        return {};
    }

    std::wstring get_wstring(int col) const override {
        const auto& cell = rows_[current_row_][col];
        if (std::holds_alternative<std::wstring>(cell)) return std::get<std::wstring>(cell);
        if (std::holds_alternative<std::string>(cell)) return utf8_to_wstring(std::get<std::string>(cell));
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

    SqlNumeric get_numeric(int col) const override {
        const auto& cell = rows_[current_row_][col];
        if (std::holds_alternative<SqlNumeric>(cell)) return std::get<SqlNumeric>(cell);
        if (std::holds_alternative<std::string>(cell)) return SqlNumeric(std::get<std::string>(cell));
        if (std::holds_alternative<double>(cell)) return SqlNumeric(std::get<double>(cell));
        return SqlNumeric();
    }

    SqlDate get_date(int col) const override {
        const auto& cell = rows_[current_row_][col];
        if (std::holds_alternative<SqlDate>(cell)) return std::get<SqlDate>(cell);
        if (std::holds_alternative<std::string>(cell)) return SqlDate::from_string(std::get<std::string>(cell));
        return SqlDate();
    }

    SqlTime get_time(int col) const override {
        const auto& cell = rows_[current_row_][col];
        if (std::holds_alternative<SqlTime>(cell)) return std::get<SqlTime>(cell);
        if (std::holds_alternative<std::string>(cell)) return SqlTime::from_string(std::get<std::string>(cell));
        return SqlTime();
    }

    SqlTimestamp get_timestamp(int col) const override {
        const auto& cell = rows_[current_row_][col];
        if (std::holds_alternative<SqlTimestamp>(cell)) return std::get<SqlTimestamp>(cell);
        if (std::holds_alternative<std::string>(cell)) return SqlTimestamp::from_string(std::get<std::string>(cell));
        return SqlTimestamp();
    }

    SqlInterval get_interval(int col) const override {
        const auto& cell = rows_[current_row_][col];
        if (std::holds_alternative<SqlInterval>(cell)) return std::get<SqlInterval>(cell);
        if (std::holds_alternative<std::string>(cell)) return SqlInterval::from_string(std::get<std::string>(cell));
        return SqlInterval();
    }

    SqlGuid get_guid(int col) const override {
        const auto& cell = rows_[current_row_][col];
        if (std::holds_alternative<SqlGuid>(cell)) return std::get<SqlGuid>(cell);
        if (std::holds_alternative<std::string>(cell)) return SqlGuid::from_string(std::get<std::string>(cell));
        return SqlGuid();
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

struct AdvancedTypesRecord {
    uint64_t unsigned_id = 0;
    SqlNumeric decimal_amount;
    SqlDate birth_date;
    SqlTime start_time;
    SqlTimestamp created_at;
    SqlInterval session_duration;
    std::chrono::system_clock::time_point chrono_timestamp;
    std::chrono::seconds chrono_duration{0};
    SqlGuid guid_val;
    std::wstring wide_text;
};

template <typename Entity, typename... Cols>
auto create_mapper(const std::tuple<Cols...>& cols, int offset = 0) {
    return RowMapper<Entity, Cols...>(cols, offset);
}

} // namespace

// ============================================================================
// Basic Type Mapping Tests
// ============================================================================

TEST(RowMapperTest, MapSingleRowAllPrimitives) {
    auto cols = std::make_tuple(
        column("id", &Person::id),
        column("name", &Person::name),
        column("age", &Person::age),
        column("score", &Person::score),
        column("is_active", &Person::is_active)
    );

    auto mapper = create_mapper<Person>(cols);

    MockDataReader reader({
        { int64_t{42}, std::string{"Alice"}, int64_t{30}, double{98.5}, bool{true} }
    });

    ASSERT_TRUE(reader.next());
    Person p = mapper.map_row(reader);

    EXPECT_EQ(p.id, 42);
    EXPECT_EQ(p.name, "Alice");
    EXPECT_EQ(p.age, 30);
    EXPECT_DOUBLE_EQ(p.score, 98.5);
    EXPECT_TRUE(p.is_active);

    EXPECT_FALSE(mapper.is_all_null(reader));
    Person in_place_p;
    mapper.map_row(reader, in_place_p);
    EXPECT_EQ(in_place_p.id, 42);

    MockDataReader null_reader({{std::monostate{}, std::monostate{}, std::monostate{}, std::monostate{}, std::monostate{}}});
    ASSERT_TRUE(null_reader.next());
    EXPECT_TRUE(mapper.is_all_null(null_reader));
    Person null_p = mapper.map_row(null_reader);
    EXPECT_EQ(null_p.id, 0);
    EXPECT_EQ(null_p.name, "");
    mapper.map_row(null_reader, in_place_p);
    EXPECT_EQ(in_place_p.id, 0);
}

TEST(RowMapperTest, MapMultipleRows) {
    auto cols = std::make_tuple(
        column("id", &Person::id),
        column("name", &Person::name),
        column("age", &Person::age),
        column("score", &Person::score),
        column("is_active", &Person::is_active)
    );

    auto mapper = create_mapper<Person>(cols);

    MockDataReader reader({
        { int64_t{1}, std::string{"Alice"}, int64_t{25}, double{88.0}, bool{true} },
        { int64_t{2}, std::string{"Bob"}, int64_t{35}, double{72.5}, bool{false} },
        { int64_t{3}, std::string{"Charlie"}, int64_t{45}, double{95.0}, bool{true} }
    });

    std::vector<Person> people;
    while (reader.next()) {
        people.push_back(mapper.map_row(reader));
    }

    ASSERT_EQ(people.size(), 3);
    EXPECT_EQ(people[0].name, "Alice");
    EXPECT_EQ(people[1].name, "Bob");
    EXPECT_EQ(people[2].name, "Charlie");
    EXPECT_FALSE(people[1].is_active);
}

// ============================================================================
// Nullable / Optional Mapping Tests
// ============================================================================

TEST(RowMapperTest, MapNullableFieldsWithValues) {
    auto cols = std::make_tuple(
        column("id", &NullableRecord::id),
        column("nickname", &NullableRecord::nickname),
        column("favorite_number", &NullableRecord::favorite_number),
        column("rating", &NullableRecord::rating),
        column("verified", &NullableRecord::verified)
    );

    auto mapper = create_mapper<NullableRecord>(cols);

    MockDataReader reader({
        { int64_t{1}, std::string{"Ali"}, int64_t{7}, double{4.5}, bool{true} }
    });

    ASSERT_TRUE(reader.next());
    NullableRecord rec = mapper.map_row(reader);

    EXPECT_EQ(rec.id, 1);
    ASSERT_TRUE(rec.nickname.has_value());
    EXPECT_EQ(*rec.nickname, "Ali");
    ASSERT_TRUE(rec.favorite_number.has_value());
    EXPECT_EQ(*rec.favorite_number, 7);
    ASSERT_TRUE(rec.rating.has_value());
    EXPECT_DOUBLE_EQ(*rec.rating, 4.5);
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

    MockDataReader reader({
        { int64_t{2}, std::monostate{}, std::monostate{}, std::monostate{}, std::monostate{} }
    });

    ASSERT_TRUE(reader.next());
    NullableRecord rec = mapper.map_row(reader);

    EXPECT_EQ(rec.id, 2);
    EXPECT_FALSE(rec.nickname.has_value());
    EXPECT_FALSE(rec.favorite_number.has_value());
    EXPECT_FALSE(rec.rating.has_value());
    EXPECT_FALSE(rec.verified.has_value());
}

TEST(RowMapperTest, MapNullableMixed) {
    auto cols = std::make_tuple(
        column("id", &NullableRecord::id),
        column("nickname", &NullableRecord::nickname),
        column("favorite_number", &NullableRecord::favorite_number),
        column("rating", &NullableRecord::rating),
        column("verified", &NullableRecord::verified)
    );

    auto mapper = create_mapper<NullableRecord>(cols);

    MockDataReader reader({
        { int64_t{3}, std::string{"Bob"}, std::monostate{}, double{3.8}, std::monostate{} }
    });

    ASSERT_TRUE(reader.next());
    NullableRecord rec = mapper.map_row(reader);

    EXPECT_EQ(rec.id, 3);
    ASSERT_TRUE(rec.nickname.has_value());
    EXPECT_EQ(*rec.nickname, "Bob");
    EXPECT_FALSE(rec.favorite_number.has_value());
    ASSERT_TRUE(rec.rating.has_value());
    EXPECT_DOUBLE_EQ(*rec.rating, 3.8);
    EXPECT_FALSE(rec.verified.has_value());
}

// ============================================================================
// Offset Mapping Tests (for JOIN queries)
// ============================================================================

TEST(RowMapperTest, MapWithOffset) {
    auto cols_person = std::make_tuple(
        column("id", &Person::id),
        column("name", &Person::name),
        column("age", &Person::age),
        column("score", &Person::score),
        column("is_active", &Person::is_active)
    );

    // RowMapper starting at column index 2 (offset = 2)
    auto mapper = create_mapper<Person>(cols_person, 2);

    MockDataReader reader({
        { int64_t{999}, std::string{"dummy"}, int64_t{10}, std::string{"OffsetPerson"}, int64_t{28}, double{91.0}, bool{true} }
    });

    ASSERT_TRUE(reader.next());
    Person p = mapper.map_row(reader);

    EXPECT_EQ(p.id, 10);
    EXPECT_EQ(p.name, "OffsetPerson");
    EXPECT_EQ(p.age, 28);
    EXPECT_DOUBLE_EQ(p.score, 91.0);
    EXPECT_TRUE(p.is_active);

    EXPECT_FALSE(mapper.is_all_null(reader));
    Person in_place_p;
    mapper.map_row(reader, in_place_p);
    EXPECT_EQ(in_place_p.id, 10);

    MockDataReader null_offset_reader({
        { int64_t{999}, std::string{"dummy"}, std::monostate{}, std::monostate{}, std::monostate{}, std::monostate{}, std::monostate{} }
    });
    ASSERT_TRUE(null_offset_reader.next());
    EXPECT_TRUE(mapper.is_all_null(null_offset_reader));
    Person null_p = mapper.map_row(null_offset_reader);
    EXPECT_EQ(null_p.id, 0);
    mapper.map_row(null_offset_reader, in_place_p);
    EXPECT_EQ(in_place_p.id, 0);
}

// ============================================================================
// is_all_null Tests (for LEFT JOIN detection)
// ============================================================================

TEST(RowMapperTest, IsAllNullTrueWhenAllColumnsNull) {
    auto cols = std::make_tuple(
        column("id", &Person::id),
        column("name", &Person::name),
        column("age", &Person::age)
    );

    auto mapper = create_mapper<Person>(cols);

    MockDataReader reader({
        { std::monostate{}, std::monostate{}, std::monostate{} }
    });

    ASSERT_TRUE(reader.next());
    EXPECT_TRUE(mapper.is_all_null(reader));

    Person p = mapper.map_row(reader);
    EXPECT_EQ(p.id, 0);
    EXPECT_EQ(p.name, "");
    EXPECT_EQ(p.age, 0);

    Person in_place_p;
    mapper.map_row(reader, in_place_p);
    EXPECT_EQ(in_place_p.id, 0);
}

TEST(RowMapperTest, IsAllNullFalseWhenAnyColumnNotNull) {
    auto cols = std::make_tuple(
        column("id", &Person::id),
        column("name", &Person::name),
        column("age", &Person::age)
    );

    auto mapper = create_mapper<Person>(cols);

    MockDataReader reader({
        { std::monostate{}, std::string{"Alice"}, std::monostate{} }
    });

    ASSERT_TRUE(reader.next());
    EXPECT_FALSE(mapper.is_all_null(reader));

    Person p = mapper.map_row(reader);
    EXPECT_EQ(p.id, 0);
    EXPECT_EQ(p.name, "Alice");
    EXPECT_EQ(p.age, 0);

    Person in_place_p;
    mapper.map_row(reader, in_place_p);
    EXPECT_EQ(in_place_p.name, "Alice");
}

TEST(RowMapperTest, IsAllNullWithOffset) {
    auto cols = std::make_tuple(
        column("id", &Person::id),
        column("name", &Person::name)
    );

    // Offset = 2: checks columns 2 and 3
    auto mapper = create_mapper<Person>(cols, 2);

    MockDataReader reader({
        // Col 0, 1 are not null; Col 2, 3 are null
        { int64_t{1}, std::string{"LeftEntity"}, std::monostate{}, std::monostate{} }
    });

    ASSERT_TRUE(reader.next());
    EXPECT_TRUE(mapper.is_all_null(reader));

    Person p = mapper.map_row(reader);
    EXPECT_EQ(p.id, 0);
    EXPECT_EQ(p.name, "");

    Person in_place_p;
    mapper.map_row(reader, in_place_p);
    EXPECT_EQ(in_place_p.id, 0);
}

TEST(RowMapperTest, IsAllNullWithOffsetFalseWhenProjectedColumnsHaveValues) {
    auto cols = std::make_tuple(
        column("id", &Person::id),
        column("name", &Person::name)
    );

    auto mapper = create_mapper<Person>(cols, 2);

    MockDataReader reader({
        { int64_t{1}, std::string{"LeftEntity"}, int64_t{7}, std::string{"Joined"} }
    });

    ASSERT_TRUE(reader.next());
    EXPECT_FALSE(mapper.is_all_null(reader));

    Person p = mapper.map_row(reader);
    EXPECT_EQ(p.id, 7);
    EXPECT_EQ(p.name, "Joined");

    Person in_place_p;
    mapper.map_row(reader, in_place_p);
    EXPECT_EQ(in_place_p.id, 7);
    EXPECT_EQ(in_place_p.name, "Joined");
}

// ============================================================================
// Blob Type Mapping Tests
// ============================================================================

TEST(RowMapperTest, MapBlobField) {
    auto cols = std::make_tuple(
        column("id", &BlobRecord::id),
        column("data", &BlobRecord::data)
    );

    auto mapper = create_mapper<BlobRecord>(cols);

    std::vector<uint8_t> test_bytes = {0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x42};

    MockDataReader reader({
        { int64_t{1}, test_bytes }
    });

    ASSERT_TRUE(reader.next());
    BlobRecord rec = mapper.map_row(reader);

    EXPECT_EQ(rec.id, 1);
    EXPECT_EQ(rec.data, test_bytes);

    EXPECT_FALSE(mapper.is_all_null(reader));
    BlobRecord in_place_b;
    mapper.map_row(reader, in_place_b);
    EXPECT_EQ(in_place_b.id, 1);
    EXPECT_EQ(in_place_b.data, test_bytes);

    MockDataReader null_reader({{std::monostate{}, std::monostate{}}});
    ASSERT_TRUE(null_reader.next());
    EXPECT_TRUE(mapper.is_all_null(null_reader));
    BlobRecord null_rec = mapper.map_row(null_reader);
    EXPECT_EQ(null_rec.id, 0);
    EXPECT_TRUE(null_rec.data.empty());
    mapper.map_row(null_reader, in_place_b);
    EXPECT_EQ(in_place_b.id, 0);
    EXPECT_TRUE(in_place_b.data.empty());
}

// ============================================================================
// Advanced Types Mapping Tests (Unsigned, Decimal, Date, Time, Timestamp, Interval)
// ============================================================================

TEST(RowMapperTest, MapAdvancedTypes) {
    auto cols = std::make_tuple(
        column("unsigned_id", &AdvancedTypesRecord::unsigned_id),
        column("decimal_amount", &AdvancedTypesRecord::decimal_amount),
        column("birth_date", &AdvancedTypesRecord::birth_date),
        column("start_time", &AdvancedTypesRecord::start_time),
        column("created_at", &AdvancedTypesRecord::created_at),
        column("session_duration", &AdvancedTypesRecord::session_duration),
        column("chrono_timestamp", &AdvancedTypesRecord::chrono_timestamp),
        column("chrono_duration", &AdvancedTypesRecord::chrono_duration),
        column("guid_val", &AdvancedTypesRecord::guid_val),
        column("wide_text", &AdvancedTypesRecord::wide_text)
    );

    auto mapper = create_mapper<AdvancedTypesRecord>(cols);

    SqlTimestamp ref_ts(2026, 8, 17, 18, 30, 45, 0);
    SqlInterval ref_iv = SqlInterval::from_day_second(1, 2, 30, 15);
    SqlGuid ref_guid("6ba7b810-9dad-11d1-80b4-00c04fd430c8");

    MockDataReader reader({
        {
            uint64_t{18446744073709551615ULL},
            SqlNumeric("12345678901234567890.123456"),
            SqlDate(2026, 8, 17),
            SqlTime(18, 30, 45),
            ref_ts,
            ref_iv,
            ref_ts,
            ref_iv,
            ref_guid,
            std::wstring(L"Unicode \u30c6\u30b9\u30c8")
        }
    });

    ASSERT_TRUE(reader.next());
    AdvancedTypesRecord rec = mapper.map_row(reader);

    EXPECT_EQ(rec.unsigned_id, 18446744073709551615ULL);
    EXPECT_EQ(rec.decimal_amount.to_string(), "12345678901234567890.123456");
    EXPECT_EQ(rec.birth_date.to_string(), "2026-08-17");
    EXPECT_EQ(rec.start_time.to_string(), "18:30:45");
    EXPECT_EQ(rec.created_at.to_string(), "2026-08-17 18:30:45");
    EXPECT_EQ(rec.session_duration.to_string(), "1 02:30:15");
    EXPECT_EQ(SqlTimestamp::from_time_point(rec.chrono_timestamp).to_string(), "2026-08-17 18:30:45");
    EXPECT_EQ(rec.chrono_duration.count(), 86400 + 7200 + 1800 + 15);
    EXPECT_EQ(rec.guid_val.to_string(), "6ba7b810-9dad-11d1-80b4-00c04fd430c8");
    EXPECT_EQ(rec.wide_text, L"Unicode \u30c6\u30b9\u30c8");

    EXPECT_FALSE(mapper.is_all_null(reader));
    AdvancedTypesRecord in_place_rec;
    mapper.map_row(reader, in_place_rec);
    EXPECT_EQ(in_place_rec.unsigned_id, 18446744073709551615ULL);

    MockDataReader null_reader({
        {
            std::monostate{}, std::monostate{}, std::monostate{}, std::monostate{}, std::monostate{},
            std::monostate{}, std::monostate{}, std::monostate{}, std::monostate{}, std::monostate{}
        }
    });
    ASSERT_TRUE(null_reader.next());
    EXPECT_TRUE(mapper.is_all_null(null_reader));
    AdvancedTypesRecord null_rec = mapper.map_row(null_reader);
    EXPECT_EQ(null_rec.unsigned_id, 0u);
    EXPECT_EQ(null_rec.decimal_amount.to_string(), "0");
    EXPECT_EQ(null_rec.wide_text, L"");

    mapper.map_row(null_reader, in_place_rec);
    EXPECT_EQ(in_place_rec.unsigned_id, 0u);
    EXPECT_EQ(in_place_rec.wide_text, L"");
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

TEST(RowMapperTest, FieldToBoundValueAdvancedTypes) {
    AdvancedTypesRecord rec;
    rec.unsigned_id = 999999999999999ULL;
    rec.decimal_amount = SqlNumeric("99999.88");
    rec.birth_date = SqlDate(2026, 8, 17);
    rec.start_time = SqlTime(14, 0, 0);
    rec.created_at = SqlTimestamp(2026, 8, 17, 14, 0, 0);
    rec.session_duration = SqlInterval::from_day_second(0, 1, 30, 0);
    rec.guid_val = SqlGuid("12345678-1234-1234-1234-123456789abc");
    rec.wide_text = L"WideStringTest";

    BoundValue bv_u = field_to_bound_value(rec, &AdvancedTypesRecord::unsigned_id);
    EXPECT_EQ(std::get<uint64_t>(bv_u), 999999999999999ULL);

    BoundValue bv_dec = field_to_bound_value(rec, &AdvancedTypesRecord::decimal_amount);
    EXPECT_EQ(std::get<SqlNumeric>(bv_dec).to_string(), "99999.88");

    BoundValue bv_d = field_to_bound_value(rec, &AdvancedTypesRecord::birth_date);
    EXPECT_EQ(std::get<SqlDate>(bv_d).to_string(), "2026-08-17");

    BoundValue bv_t = field_to_bound_value(rec, &AdvancedTypesRecord::start_time);
    EXPECT_EQ(std::get<SqlTime>(bv_t).to_string(), "14:00:00");

    BoundValue bv_ts = field_to_bound_value(rec, &AdvancedTypesRecord::created_at);
    EXPECT_EQ(std::get<SqlTimestamp>(bv_ts).to_string(), "2026-08-17 14:00:00");

    BoundValue bv_iv = field_to_bound_value(rec, &AdvancedTypesRecord::session_duration);
    EXPECT_EQ(std::get<SqlInterval>(bv_iv).to_string(), "0 01:30:00");

    BoundValue bv_g = field_to_bound_value(rec, &AdvancedTypesRecord::guid_val);
    EXPECT_EQ(std::get<SqlGuid>(bv_g).to_string(), "12345678-1234-1234-1234-123456789abc");

    BoundValue bv_w = field_to_bound_value(rec, &AdvancedTypesRecord::wide_text);
    EXPECT_EQ(std::get<std::wstring>(bv_w), L"WideStringTest");
}

class MockStringOnlyReader : public IDataReader {
public:
    bool next() override { return true; }
    int column_count() const override { return 1; }
    bool is_null(int) const override { return false; }
    int64_t get_int64(int) const override { return 0; }
    uint64_t get_uint64(int) const override { return 0; }
    double get_double(int) const override { return 0; }
    std::string get_string(int) const override { return "driver_string_value"; }
    // Intentionally do NOT override get_string_view; uses default returning {}
    std::wstring get_wstring(int) const override { return {}; }
    bool get_bool(int) const override { return false; }
    std::vector<uint8_t> get_blob(int) const override { return {}; }
    SqlNumeric get_numeric(int) const override { return {}; }
    SqlDate get_date(int) const override { return {}; }
    SqlTime get_time(int) const override { return {}; }
    SqlTimestamp get_timestamp(int) const override { return {}; }
    SqlInterval get_interval(int) const override { return {}; }
    SqlGuid get_guid(int) const override { return {}; }
};

TEST(RowMapperTest, StringFallsBackToGetStringWhenViewEmpty) {
    MockStringOnlyReader reader;
    auto val = read_column_value<std::string>(reader, 0);
    EXPECT_EQ(val, "driver_string_value");

    std::string target;
    assign_column_value(target, reader, 0);
    EXPECT_EQ(target, "driver_string_value");
}

class MockEmptyStringViewReader : public IDataReader {
public:
    bool next() override { return true; }
    int column_count() const override { return 1; }
    bool is_null(int) const override { return false; }
    int64_t get_int64(int) const override { return 0; }
    uint64_t get_uint64(int) const override { return 0; }
    double get_double(int) const override { return 0; }
    std::string get_string(int) const override {
        throw std::runtime_error("get_string should not be called when string_view is valid");
    }
    std::string_view get_string_view(int) const override {
        static const char empty[] = "";
        return std::string_view(empty, 0);
    }
    std::wstring get_wstring(int) const override { return {}; }
    bool get_bool(int) const override { return false; }
    std::vector<uint8_t> get_blob(int) const override { return {}; }
    SqlNumeric get_numeric(int) const override { return {}; }
    SqlDate get_date(int) const override { return {}; }
    SqlTime get_time(int) const override { return {}; }
    SqlTimestamp get_timestamp(int) const override { return {}; }
    SqlInterval get_interval(int) const override { return {}; }
    SqlGuid get_guid(int) const override { return {}; }
};

TEST(RowMapperTest, ValidEmptyStringViewAvoidsGetString) {
    MockEmptyStringViewReader reader;
    EXPECT_NO_THROW({
        auto val = read_column_value<std::string>(reader, 0);
        EXPECT_EQ(val, "");
    });

    std::string target;
    EXPECT_NO_THROW({
        assign_column_value(target, reader, 0);
        EXPECT_EQ(target, "");
    });
}

struct SimpleEntity {
    int id = 0;
    std::string name;
};

TEST(RowMapperTest, InPlaceMapRowResetsNonOptionalOnNull) {
    const auto cols = std::make_tuple(
        column("id", &SimpleEntity::id),
        column("name", &SimpleEntity::name)
    );
    RowMapper<SimpleEntity, decltype(std::get<0>(cols)), decltype(std::get<1>(cols))> mapper(cols);

    // Row 1: has values
    MockDataReader reader1({
        {int64_t(42), std::string("initial_name")}
    });
    reader1.next();

    SimpleEntity entity;
    mapper.map_row(reader1, entity);
    EXPECT_EQ(entity.id, 42);
    EXPECT_EQ(entity.name, "initial_name");

    EXPECT_FALSE(mapper.is_all_null(reader1));

    SimpleEntity val_mapped = mapper.map_row(reader1);
    EXPECT_EQ(val_mapped.id, 42);
    EXPECT_EQ(val_mapped.name, "initial_name");

    // Row 2: columns are NULL
    MockDataReader reader2({
        {std::monostate{}, std::monostate{}}
    });
    reader2.next();

    EXPECT_TRUE(mapper.is_all_null(reader2));

    SimpleEntity null_mapped = mapper.map_row(reader2);
    EXPECT_EQ(null_mapped.id, 0);
    EXPECT_EQ(null_mapped.name, "");

    // Map in-place into the same entity: should NOT retain previous row's values
    mapper.map_row(reader2, entity);
    EXPECT_EQ(entity.id, 0);
    EXPECT_EQ(entity.name, "");
}

TEST(RowMapperTest, InPlaceMapRowResetsOptionalFieldsOnNull) {
    const auto cols = std::make_tuple(
        column("id", &NullableRecord::id),
        column("nickname", &NullableRecord::nickname),
        column("favorite_number", &NullableRecord::favorite_number),
        column("rating", &NullableRecord::rating),
        column("verified", &NullableRecord::verified)
    );
    auto mapper = create_mapper<NullableRecord>(cols);

    MockDataReader populated({
        { int64_t{9}, std::string{"before"}, int64_t{3}, double{4.25}, bool{true} }
    });
    populated.next();

    EXPECT_FALSE(mapper.is_all_null(populated));
    NullableRecord entity;
    mapper.map_row(populated, entity);
    ASSERT_TRUE(entity.nickname.has_value());
    ASSERT_TRUE(entity.favorite_number.has_value());
    ASSERT_TRUE(entity.rating.has_value());
    ASSERT_TRUE(entity.verified.has_value());

    NullableRecord val_rec = mapper.map_row(populated);
    EXPECT_EQ(val_rec.id, 9);
    EXPECT_EQ(*val_rec.nickname, "before");

    MockDataReader all_null({
        { std::monostate{}, std::monostate{}, std::monostate{}, std::monostate{}, std::monostate{} }
    });
    all_null.next();

    EXPECT_TRUE(mapper.is_all_null(all_null));
    NullableRecord null_rec = mapper.map_row(all_null);
    EXPECT_EQ(null_rec.id, 0);
    EXPECT_FALSE(null_rec.nickname.has_value());

    mapper.map_row(all_null, entity);

    EXPECT_EQ(entity.id, 0);
    EXPECT_FALSE(entity.nickname.has_value());
    EXPECT_FALSE(entity.favorite_number.has_value());
    EXPECT_FALSE(entity.rating.has_value());
    EXPECT_FALSE(entity.verified.has_value());
}

TEST(RowMapperTest, ChronoAndCharMappings) {
    // char mapping
    char ch = 'Z';
    BoundValue bv_char = convert_to_bound_value(ch);
    EXPECT_TRUE(std::holds_alternative<int64_t>(bv_char) || std::holds_alternative<uint64_t>(bv_char));

    // year_month_day mapping
    std::chrono::year_month_day ymd{std::chrono::year{2026}, std::chrono::month{10}, std::chrono::day{16}};
    BoundValue bv_ymd = convert_to_bound_value(ymd);
    ASSERT_TRUE(std::holds_alternative<SqlDate>(bv_ymd));
    EXPECT_EQ(std::get<SqlDate>(bv_ymd).to_string(), "2026-10-16");

    // hh_mm_ss mapping
    std::chrono::hh_mm_ss hms{std::chrono::hours(14) + std::chrono::minutes(30) + std::chrono::seconds(45)};
    BoundValue bv_hms = convert_to_bound_value(hms);
    ASSERT_TRUE(std::holds_alternative<SqlTime>(bv_hms));
    EXPECT_EQ(std::get<SqlTime>(bv_hms).to_string(), "14:30:45");

    // Read back via reader
    MockDataReader reader({
        {SqlDate(2026, 10, 16), SqlTime(14, 30, 45)}
    });
    reader.next();

    auto read_ymd = read_column_value<std::chrono::year_month_day>(reader, 0);
    EXPECT_EQ(static_cast<int>(read_ymd.year()), 2026);
    EXPECT_EQ(static_cast<unsigned>(read_ymd.month()), 10);
    EXPECT_EQ(static_cast<unsigned>(read_ymd.day()), 16);

    auto read_hms = read_column_value<std::chrono::hh_mm_ss<std::chrono::seconds>>(reader, 1);
    EXPECT_EQ(read_hms.hours().count(), 14);
    EXPECT_EQ(read_hms.minutes().count(), 30);
    EXPECT_EQ(read_hms.seconds().count(), 45);
}

TEST(DataTypesTest, SqlNumericExactComparisonsAndOverflowSafety) {
    // Safe INT64_MIN constructor (no UB)
    int64_t min_val = std::numeric_limits<int64_t>::min();
    SqlNumeric num_min(min_val);
    EXPECT_EQ(num_min.to_string(), "-9223372036854775808");
    EXPECT_TRUE(num_min.is_negative);

    // Consistent and exact comparisons
    EXPECT_TRUE(SqlNumeric("1.50") == SqlNumeric("1.5"));
    EXPECT_TRUE((SqlNumeric("1.50") <=> SqlNumeric("1.5")) == 0);
    EXPECT_TRUE(SqlNumeric("-0") == SqlNumeric("0"));
    EXPECT_TRUE((SqlNumeric("-0") <=> SqlNumeric("0")) == 0);
    EXPECT_TRUE(SqlNumeric("0.00") == SqlNumeric("0"));

    // Arbitrary precision decimals exceeding 53 bits (> 17 digits)
    SqlNumeric big1("123456789012345678901234567890");
    SqlNumeric big2("123456789012345678901234567891");
    EXPECT_TRUE(big1 < big2);
    EXPECT_FALSE(big1 == big2);
    EXPECT_TRUE((big1 <=> big2) < 0);

    // Negative comparisons
    EXPECT_TRUE(SqlNumeric("-2.5") < SqlNumeric("-1.5"));
    EXPECT_TRUE(SqlNumeric("-1.5") > SqlNumeric("-2.5"));
    EXPECT_TRUE(SqlNumeric("-1.50") == SqlNumeric("-1.5"));

    // Underflow edge cases
    SqlNumeric underflow1("-");
    EXPECT_EQ(underflow1.to_string(), "0");
    SqlNumeric underflow2("-.");
    EXPECT_EQ(underflow2.to_string(), "0");
    SqlNumeric underflow3(".");
    EXPECT_EQ(underflow3.to_string(), "0");

    // Infinity rejection
    SqlNumeric inf_pos(std::numeric_limits<double>::infinity());
    EXPECT_EQ(inf_pos.to_string(), "0");
    SqlNumeric inf_neg(-std::numeric_limits<double>::infinity());
    EXPECT_EQ(inf_neg.to_string(), "0");
}

TEST(DataTypesTest, SqlTimeAndTimestampSubsecondsAndTimezone) {
    // Fractional seconds with ISO-8601 timezone suffix
    SqlTimestamp ts_z = SqlTimestamp::from_string("2026-10-16T14:15:00.5Z");
    EXPECT_EQ(ts_z.fraction, 500'000'000U);
    EXPECT_EQ(ts_z.tz_offset_minutes, 0);

    SqlTimestamp ts_offset = SqlTimestamp::from_string("2026-10-16T14:15:00.123+02:00");
    EXPECT_EQ(ts_offset.fraction, 123'000'000U);
    EXPECT_EQ(ts_offset.tz_offset_minutes, 120);

    SqlTime t_frac = SqlTime::from_string("14:15:00.5Z");
    EXPECT_EQ(t_frac.fraction, 500'000'000U);

    // Pre-1970 subsecond floor test
    // 1969-12-31 23:59:59.500 UTC = -500ms from epoch
    auto pre1970_tp = std::chrono::system_clock::time_point(std::chrono::milliseconds(-500));
    SqlTimestamp ts_pre = SqlTimestamp::from_time_point(pre1970_tp);
    EXPECT_EQ(ts_pre.year, 1969);
    EXPECT_EQ(ts_pre.month, 12);
    EXPECT_EQ(ts_pre.day, 31);
    EXPECT_EQ(ts_pre.hour, 23);
    EXPECT_EQ(ts_pre.minute, 59);
    EXPECT_EQ(ts_pre.second, 59);
    EXPECT_EQ(ts_pre.fraction, 500'000'000U);

    // Timestamp comparison normalizing tz_offset_minutes
    // 14:15:00 +02:00 == 12:15:00 +00:00
    SqlTimestamp ts1(2026, 10, 16, 14, 15, 0, 0, 120);
    SqlTimestamp ts2(2026, 10, 16, 12, 15, 0, 0, 0);
    EXPECT_TRUE(ts1 == ts2);
    EXPECT_TRUE((ts1 <=> ts2) == 0);
}

TEST(DataTypesTest, SqlIntervalSubsecondsAndYearToMonth) {
    // INT64_MIN safe negation in from_duration and to_duration
    auto min_duration = std::chrono::nanoseconds(std::numeric_limits<int64_t>::min());
    SqlInterval iv_min = SqlInterval::from_duration(min_duration);
    EXPECT_TRUE(iv_min.is_negative);
    auto roundtrip_min = iv_min.to_duration<std::chrono::nanoseconds>();
    EXPECT_EQ(roundtrip_min.count(), std::numeric_limits<int64_t>::min());

    // Parsing subseconds in interval
    SqlInterval iv_sub = SqlInterval::from_string("1 02:03:04.500000000");
    EXPECT_EQ(iv_sub.days, 1);
    EXPECT_EQ(iv_sub.hours, 2);
    EXPECT_EQ(iv_sub.minutes, 3);
    EXPECT_EQ(iv_sub.seconds, 4);
    EXPECT_EQ(iv_sub.fraction, 500'000'000U);

    // YearToMonth to_duration
    SqlInterval iv_ym = SqlInterval::from_year_month(2, 6); // 2 years 6 months = 30 months
    auto months_dur = iv_ym.to_duration<std::chrono::months>();
    EXPECT_EQ(months_dur.count(), 30);
}

TEST(RowMapperTest, ConvertToBoundValueAllTypes) {
    // std::string_view
    std::string_view sv = "sv_value";
    BoundValue bv_sv = convert_to_bound_value(sv);
    ASSERT_TRUE(std::holds_alternative<std::string>(bv_sv));
    EXPECT_EQ(std::get<std::string>(bv_sv), "sv_value");

    // const char* non-null and null
    const char* cstr = "cstr_value";
    BoundValue bv_cstr = convert_to_bound_value(cstr);
    ASSERT_TRUE(std::holds_alternative<std::string>(bv_cstr));
    EXPECT_EQ(std::get<std::string>(bv_cstr), "cstr_value");

    const char* cstr_null = nullptr;
    BoundValue bv_cstr_null = convert_to_bound_value(cstr_null);
    ASSERT_TRUE(std::holds_alternative<std::string>(bv_cstr_null));
    EXPECT_EQ(std::get<std::string>(bv_cstr_null), "");

    // char* non-null and null
    char mutable_str[] = "mutable_str_val";
    char* p_mut = mutable_str;
    BoundValue bv_mut = convert_to_bound_value(p_mut);
    ASSERT_TRUE(std::holds_alternative<std::string>(bv_mut));
    EXPECT_EQ(std::get<std::string>(bv_mut), "mutable_str_val");

    char* p_mut_null = nullptr;
    BoundValue bv_mut_null = convert_to_bound_value(p_mut_null);
    ASSERT_TRUE(std::holds_alternative<std::string>(bv_mut_null));
    EXPECT_EQ(std::get<std::string>(bv_mut_null), "");

    // std::wstring_view
    std::wstring_view wsv = L"wsv_value";
    BoundValue bv_wsv = convert_to_bound_value(wsv);
    ASSERT_TRUE(std::holds_alternative<std::wstring>(bv_wsv));
    EXPECT_EQ(std::get<std::wstring>(bv_wsv), L"wsv_value");

    // const wchar_t* non-null and null
    const wchar_t* wcstr = L"wcstr_value";
    BoundValue bv_wcstr = convert_to_bound_value(wcstr);
    ASSERT_TRUE(std::holds_alternative<std::wstring>(bv_wcstr));
    EXPECT_EQ(std::get<std::wstring>(bv_wcstr), L"wcstr_value");

    const wchar_t* wcstr_null = nullptr;
    BoundValue bv_wcstr_null = convert_to_bound_value(wcstr_null);
    ASSERT_TRUE(std::holds_alternative<std::wstring>(bv_wcstr_null));
    EXPECT_EQ(std::get<std::wstring>(bv_wcstr_null), L"");

    // wchar_t* non-null and null
    wchar_t mutable_wstr[] = L"mutable_wstr_val";
    wchar_t* p_wmut = mutable_wstr;
    BoundValue bv_wmut = convert_to_bound_value(p_wmut);
    ASSERT_TRUE(std::holds_alternative<std::wstring>(bv_wmut));
    EXPECT_EQ(std::get<std::wstring>(bv_wmut), L"mutable_wstr_val");

    wchar_t* p_wmut_null = nullptr;
    BoundValue bv_wmut_null = convert_to_bound_value(p_wmut_null);
    ASSERT_TRUE(std::holds_alternative<std::wstring>(bv_wmut_null));
    EXPECT_EQ(std::get<std::wstring>(bv_wmut_null), L"");

    // std::chrono::year_month_day
    std::chrono::year_month_day ymd{std::chrono::year(2026), std::chrono::month(12), std::chrono::day(25)};
    BoundValue bv_ymd = convert_to_bound_value(ymd);
    ASSERT_TRUE(std::holds_alternative<SqlDate>(bv_ymd));
    EXPECT_EQ(std::get<SqlDate>(bv_ymd).to_string(), "2026-12-25");

    // std::chrono::system_clock::time_point
    auto tp = std::chrono::system_clock::from_time_t(1700000000);
    BoundValue bv_tp = convert_to_bound_value(tp);
    ASSERT_TRUE(std::holds_alternative<SqlTimestamp>(bv_tp));

    // std::chrono::hh_mm_ss
    std::chrono::hh_mm_ss hms{std::chrono::hours(11) + std::chrono::minutes(22) + std::chrono::seconds(33)};
    BoundValue bv_hms = convert_to_bound_value(hms);
    ASSERT_TRUE(std::holds_alternative<SqlTime>(bv_hms));
    EXPECT_EQ(std::get<SqlTime>(bv_hms).to_string(), "11:22:33");

    // SqlTime
    SqlTime st(11, 22, 33);
    BoundValue bv_st = convert_to_bound_value(st);
    ASSERT_TRUE(std::holds_alternative<SqlTime>(bv_st));
    EXPECT_EQ(std::get<SqlTime>(bv_st).to_string(), "11:22:33");

    // std::chrono::duration
    std::chrono::seconds dur(3600);
    BoundValue bv_dur = convert_to_bound_value(dur);
    ASSERT_TRUE(std::holds_alternative<SqlInterval>(bv_dur));

    // SqlInterval
    SqlInterval iv = SqlInterval::from_day_second(1, 2, 3, 4);
    BoundValue bv_iv = convert_to_bound_value(iv);
    ASSERT_TRUE(std::holds_alternative<SqlInterval>(bv_iv));
    EXPECT_EQ(std::get<SqlInterval>(bv_iv).to_string(), "1 02:03:04");

    // SqlGuid
    SqlGuid g("6ba7b810-9dad-11d1-80b4-00c04fd430c8");
    BoundValue bv_g = convert_to_bound_value(g);
    ASSERT_TRUE(std::holds_alternative<SqlGuid>(bv_g));
    EXPECT_EQ(std::get<SqlGuid>(bv_g).to_string(), "6ba7b810-9dad-11d1-80b4-00c04fd430c8");

    // std::monostate
    BoundValue bv_mono = convert_to_bound_value(std::monostate{});
    EXPECT_TRUE(std::holds_alternative<std::monostate>(bv_mono));

    // BoundValue directly
    BoundValue orig_bv = int64_t{999};
    BoundValue bv_direct = convert_to_bound_value(orig_bv);
    ASSERT_TRUE(std::holds_alternative<int64_t>(bv_direct));
    EXPECT_EQ(std::get<int64_t>(bv_direct), 999);

    // Unsigned integral types
    uint8_t u8 = 8;
    BoundValue bv_u8 = convert_to_bound_value(u8);
    ASSERT_TRUE(std::holds_alternative<uint64_t>(bv_u8));
    EXPECT_EQ(std::get<uint64_t>(bv_u8), 8u);

    uint16_t u16 = 16;
    BoundValue bv_u16 = convert_to_bound_value(u16);
    ASSERT_TRUE(std::holds_alternative<uint64_t>(bv_u16));
    EXPECT_EQ(std::get<uint64_t>(bv_u16), 16u);

    uint32_t u32 = 32;
    BoundValue bv_u32 = convert_to_bound_value(u32);
    ASSERT_TRUE(std::holds_alternative<uint64_t>(bv_u32));
    EXPECT_EQ(std::get<uint64_t>(bv_u32), 32u);

    uint64_t u64 = 64;
    BoundValue bv_u64 = convert_to_bound_value(u64);
    ASSERT_TRUE(std::holds_alternative<uint64_t>(bv_u64));
    EXPECT_EQ(std::get<uint64_t>(bv_u64), 64u);

    // Signed integral types
    int8_t i8 = -8;
    BoundValue bv_i8 = convert_to_bound_value(i8);
    ASSERT_TRUE(std::holds_alternative<int64_t>(bv_i8));
    EXPECT_EQ(std::get<int64_t>(bv_i8), -8);

    int16_t i16 = -16;
    BoundValue bv_i16 = convert_to_bound_value(i16);
    ASSERT_TRUE(std::holds_alternative<int64_t>(bv_i16));
    EXPECT_EQ(std::get<int64_t>(bv_i16), -16);

    int32_t i32 = -32;
    BoundValue bv_i32 = convert_to_bound_value(i32);
    ASSERT_TRUE(std::holds_alternative<int64_t>(bv_i32));
    EXPECT_EQ(std::get<int64_t>(bv_i32), -32);

    int64_t i64 = -64;
    BoundValue bv_i64 = convert_to_bound_value(i64);
    ASSERT_TRUE(std::holds_alternative<int64_t>(bv_i64));
    EXPECT_EQ(std::get<int64_t>(bv_i64), -64);

    // Floating point types
    float f = 3.14f;
    BoundValue bv_f = convert_to_bound_value(f);
    ASSERT_TRUE(std::holds_alternative<double>(bv_f));
    EXPECT_NEAR(std::get<double>(bv_f), 3.14, 0.001);

    double d = 2.71828;
    BoundValue bv_d = convert_to_bound_value(d);
    ASSERT_TRUE(std::holds_alternative<double>(bv_d));
    EXPECT_DOUBLE_EQ(std::get<double>(bv_d), 2.71828);

    // bool
    BoundValue bv_b = convert_to_bound_value(true);
    ASSERT_TRUE(std::holds_alternative<bool>(bv_b));
    EXPECT_TRUE(std::get<bool>(bv_b));

    // std::string
    BoundValue bv_str = convert_to_bound_value(std::string("str_val"));
    ASSERT_TRUE(std::holds_alternative<std::string>(bv_str));
    EXPECT_EQ(std::get<std::string>(bv_str), "str_val");

    // std::wstring
    BoundValue bv_wstr = convert_to_bound_value(std::wstring(L"wstr_val"));
    ASSERT_TRUE(std::holds_alternative<std::wstring>(bv_wstr));
    EXPECT_EQ(std::get<std::wstring>(bv_wstr), L"wstr_val");

    // std::vector<uint8_t>
    std::vector<uint8_t> blob{1, 2, 3};
    BoundValue bv_blob = convert_to_bound_value(blob);
    ASSERT_TRUE(std::holds_alternative<std::vector<uint8_t>>(bv_blob));
    EXPECT_EQ(std::get<std::vector<uint8_t>>(bv_blob), blob);

    // SqlNumeric
    BoundValue bv_num = convert_to_bound_value(SqlNumeric("123.45"));
    ASSERT_TRUE(std::holds_alternative<SqlNumeric>(bv_num));
    EXPECT_EQ(std::get<SqlNumeric>(bv_num).to_string(), "123.45");

    // SqlDate
    BoundValue bv_date = convert_to_bound_value(SqlDate(2026, 1, 1));
    ASSERT_TRUE(std::holds_alternative<SqlDate>(bv_date));
    EXPECT_EQ(std::get<SqlDate>(bv_date).to_string(), "2026-01-01");

    // SqlTimestamp
    BoundValue bv_ts = convert_to_bound_value(SqlTimestamp(2026, 1, 1, 12, 0, 0));
    ASSERT_TRUE(std::holds_alternative<SqlTimestamp>(bv_ts));
    EXPECT_EQ(std::get<SqlTimestamp>(bv_ts).to_string(), "2026-01-01 12:00:00");
}

struct FieldTestEntity {
    int id = 0;
    std::string name;
    std::optional<int> opt_int;
    std::optional<std::string> opt_str;
    std::optional<double> opt_double;
};

TEST(RowMapperTest, FieldToBoundValueOptionalAndNonOptional) {
    FieldTestEntity entity_nulls{
        .id = 101,
        .name = "TestEntity",
        .opt_int = std::nullopt,
        .opt_str = std::nullopt,
        .opt_double = std::nullopt
    };

    // Non-optional fields
    BoundValue bv_id = field_to_bound_value(entity_nulls, &FieldTestEntity::id);
    ASSERT_TRUE(std::holds_alternative<int64_t>(bv_id));
    EXPECT_EQ(std::get<int64_t>(bv_id), 101);

    BoundValue bv_name = field_to_bound_value(entity_nulls, &FieldTestEntity::name);
    ASSERT_TRUE(std::holds_alternative<std::string>(bv_name));
    EXPECT_EQ(std::get<std::string>(bv_name), "TestEntity");

    // Optional fields with nullopt -> std::monostate
    BoundValue bv_null_int = field_to_bound_value(entity_nulls, &FieldTestEntity::opt_int);
    EXPECT_TRUE(std::holds_alternative<std::monostate>(bv_null_int));

    BoundValue bv_null_str = field_to_bound_value(entity_nulls, &FieldTestEntity::opt_str);
    EXPECT_TRUE(std::holds_alternative<std::monostate>(bv_null_str));

    BoundValue bv_null_double = field_to_bound_value(entity_nulls, &FieldTestEntity::opt_double);
    EXPECT_TRUE(std::holds_alternative<std::monostate>(bv_null_double));

    // Optional fields with values
    FieldTestEntity entity_vals{
        .id = 102,
        .name = "TestEntityVals",
        .opt_int = 42,
        .opt_str = "hello",
        .opt_double = 3.14159
    };

    BoundValue bv_val_int = field_to_bound_value(entity_vals, &FieldTestEntity::opt_int);
    ASSERT_TRUE(std::holds_alternative<int64_t>(bv_val_int));
    EXPECT_EQ(std::get<int64_t>(bv_val_int), 42);

    BoundValue bv_val_str = field_to_bound_value(entity_vals, &FieldTestEntity::opt_str);
    ASSERT_TRUE(std::holds_alternative<std::string>(bv_val_str));
    EXPECT_EQ(std::get<std::string>(bv_val_str), "hello");

    BoundValue bv_val_double = field_to_bound_value(entity_vals, &FieldTestEntity::opt_double);
    ASSERT_TRUE(std::holds_alternative<double>(bv_val_double));
    EXPECT_DOUBLE_EQ(std::get<double>(bv_val_double), 3.14159);
}

class MinimalDataReader : public IDataReader {
public:
    bool next() override { return true; }
    int column_count() const override { return 2; }
    bool is_null(int col) const override { return col == 0; }
    int64_t get_int64(int) const override { return 0; }
    uint64_t get_uint64(int) const override { return 0; }
    double get_double(int) const override { return 0.0; }
    std::string get_string(int col) const override { return col == 1 ? "minimal_str" : ""; }
    std::wstring get_wstring(int) const override { return {}; }
    bool get_bool(int) const override { return false; }
    std::vector<uint8_t> get_blob(int) const override { return {}; }
    SqlNumeric get_numeric(int) const override { return {}; }
    SqlDate get_date(int) const override { return {}; }
    SqlTime get_time(int) const override { return {}; }
    SqlTimestamp get_timestamp(int) const override { return {}; }
    SqlInterval get_interval(int) const override { return {}; }
    SqlGuid get_guid(int) const override { return {}; }
};

TEST(RowMapperTest, MinimalDataReaderDefaults) {
    MinimalDataReader reader;

    // get_string_view default implementation returns {}
    EXPECT_TRUE(reader.get_string_view(0).empty());
    EXPECT_EQ(reader.get_string_view(0).data(), nullptr);
    EXPECT_TRUE(reader.get_string_view(1).empty());
    EXPECT_EQ(reader.get_string_view(1).data(), nullptr);

    // get_value default implementation: monostate when is_null, get_string when not null
    BoundValue val_null = reader.get_value(0);
    EXPECT_TRUE(std::holds_alternative<std::monostate>(val_null));

    BoundValue val_non_null = reader.get_value(1);
    ASSERT_TRUE(std::holds_alternative<std::string>(val_non_null));
    EXPECT_EQ(std::get<std::string>(val_non_null), "minimal_str");

    // read_column_value and assign_column_value fallback to get_string when get_string_view returns nullptr
    auto read_str = read_column_value<std::string>(reader, 1);
    EXPECT_EQ(read_str, "minimal_str");

    std::string assigned_str;
    assign_column_value(assigned_str, reader, 1);
    EXPECT_EQ(assigned_str, "minimal_str");
}

struct ComprehensiveEntity {
    int id = 0;
    std::string name;
    double score = 0.0;
    std::optional<std::string> opt_str;
    std::optional<int> opt_int;
};

TEST(RowMapperTest, IsAllNullAndInPlaceMappingWithComprehensiveTypes) {
    const auto cols = std::make_tuple(
        column("id", &ComprehensiveEntity::id),
        column("name", &ComprehensiveEntity::name),
        column("score", &ComprehensiveEntity::score),
        column("opt_str", &ComprehensiveEntity::opt_str),
        column("opt_int", &ComprehensiveEntity::opt_int)
    );
    auto mapper = create_mapper<ComprehensiveEntity>(cols);

    // 1. All columns null -> is_all_null returns true
    MockDataReader null_reader({
        { std::monostate{}, std::monostate{}, std::monostate{}, std::monostate{}, std::monostate{} }
    });
    ASSERT_TRUE(null_reader.next());
    EXPECT_TRUE(mapper.is_all_null(null_reader));

    // In-place mapping from non-default entity into null row
    ComprehensiveEntity entity{10, "initial", 5.5, "opt_init", 99};
    mapper.map_row(null_reader, entity);
    EXPECT_EQ(entity.id, 0);
    EXPECT_EQ(entity.name, "");
    EXPECT_DOUBLE_EQ(entity.score, 0.0);
    EXPECT_FALSE(entity.opt_str.has_value());
    EXPECT_FALSE(entity.opt_int.has_value());

    // 2. Only first column non-null -> is_all_null returns false
    MockDataReader col0_reader({
        { int64_t{1}, std::monostate{}, std::monostate{}, std::monostate{}, std::monostate{} }
    });
    col0_reader.next();
    EXPECT_FALSE(mapper.is_all_null(col0_reader));

    // 3. Only middle column non-null -> is_all_null returns false
    MockDataReader col2_reader({
        { std::monostate{}, std::monostate{}, double{2.5}, std::monostate{}, std::monostate{} }
    });
    col2_reader.next();
    EXPECT_FALSE(mapper.is_all_null(col2_reader));

    // 4. Only last column non-null -> is_all_null returns false
    MockDataReader col4_reader({
        { std::monostate{}, std::monostate{}, std::monostate{}, std::monostate{}, int64_t{77} }
    });
    col4_reader.next();
    EXPECT_FALSE(mapper.is_all_null(col4_reader));

    // 5. All columns non-null -> is_all_null returns false and in-place map succeeds
    MockDataReader full_reader({
        { int64_t{42}, std::string{"Alice"}, double{98.5}, std::string{"nick"}, int64_t{7} }
    });
    full_reader.next();
    EXPECT_FALSE(mapper.is_all_null(full_reader));
    mapper.map_row(full_reader, entity);
    EXPECT_EQ(entity.id, 42);
    EXPECT_EQ(entity.name, "Alice");
    EXPECT_DOUBLE_EQ(entity.score, 98.5);
    ASSERT_TRUE(entity.opt_str.has_value());
    EXPECT_EQ(*entity.opt_str, "nick");
    ASSERT_TRUE(entity.opt_int.has_value());
    EXPECT_EQ(*entity.opt_int, 7);

    ComprehensiveEntity val_p = mapper.map_row(full_reader);
    EXPECT_EQ(val_p.id, 42);
    EXPECT_EQ(val_p.name, "Alice");
    EXPECT_DOUBLE_EQ(val_p.score, 98.5);

    ComprehensiveEntity null_p = mapper.map_row(null_reader);
    EXPECT_EQ(null_p.id, 0);
    EXPECT_EQ(null_p.name, "");
    EXPECT_DOUBLE_EQ(null_p.score, 0.0);
    EXPECT_FALSE(null_p.opt_str.has_value());
    EXPECT_FALSE(null_p.opt_int.has_value());
}

TEST(RowMapperTest, ReadColumnValueAndAssignColumnValueAllTypes) {
    SqlGuid guid("6ba7b810-9dad-11d1-80b4-00c04fd430c8");
    SqlInterval iv = SqlInterval::from_day_second(1, 2, 3, 4);
    SqlTimestamp ts(2026, 10, 18, 12, 30, 45);
    SqlTime tm(12, 30, 45);
    SqlDate dt(2026, 10, 18);
    SqlNumeric num("12345.67");
    std::vector<uint8_t> blob{10, 20, 30};

    MockDataReader reader({
        {
            bool{true},
            int64_t{-12345},
            uint64_t{54321},
            double{3.14159},
            std::string{"string_val"},
            std::wstring{L"wstring_val"},
            blob,
            num,
            dt,
            tm,
            ts,
            iv,
            guid
        }
    });
    ASSERT_TRUE(reader.next());

    // bool
    EXPECT_EQ(read_column_value<bool>(reader, 0), true);

    // signed integrals
    EXPECT_EQ(read_column_value<int8_t>(reader, 1), static_cast<int8_t>(-12345));
    EXPECT_EQ(read_column_value<int16_t>(reader, 1), static_cast<int16_t>(-12345));
    EXPECT_EQ(read_column_value<int32_t>(reader, 1), -12345);
    EXPECT_EQ(read_column_value<int64_t>(reader, 1), -12345);

    // unsigned integrals
    EXPECT_EQ(read_column_value<uint8_t>(reader, 2), static_cast<uint8_t>(54321));
    EXPECT_EQ(read_column_value<uint16_t>(reader, 2), static_cast<uint16_t>(54321));
    EXPECT_EQ(read_column_value<uint32_t>(reader, 2), 54321u);
    EXPECT_EQ(read_column_value<uint64_t>(reader, 2), 54321u);

    // floating point
    EXPECT_NEAR(read_column_value<float>(reader, 3), 3.14159f, 0.0001f);
    EXPECT_DOUBLE_EQ(read_column_value<double>(reader, 3), 3.14159);

    // string & string_view
    EXPECT_EQ(read_column_value<std::string>(reader, 4), "string_val");
    EXPECT_EQ(read_column_value<std::string_view>(reader, 4), "string_val");

    // wstring
    EXPECT_EQ(read_column_value<std::wstring>(reader, 5), L"wstring_val");

    // blob
    EXPECT_EQ(read_column_value<std::vector<uint8_t>>(reader, 6), blob);

    // SqlNumeric, SqlDate, SqlTime, SqlTimestamp, SqlInterval, SqlGuid
    EXPECT_EQ(read_column_value<SqlNumeric>(reader, 7).to_string(), "12345.67");
    EXPECT_EQ(read_column_value<SqlDate>(reader, 8).to_string(), "2026-10-18");
    EXPECT_EQ(read_column_value<SqlTime>(reader, 9).to_string(), "12:30:45");
    EXPECT_EQ(read_column_value<SqlTimestamp>(reader, 10).to_string(), "2026-10-18 12:30:45");
    EXPECT_EQ(read_column_value<SqlInterval>(reader, 11).to_string(), "1 02:03:04");
    EXPECT_EQ(read_column_value<SqlGuid>(reader, 12).to_string(), "6ba7b810-9dad-11d1-80b4-00c04fd430c8");

    // chrono mappings
    auto ymd = read_column_value<std::chrono::year_month_day>(reader, 8);
    EXPECT_EQ(static_cast<int>(ymd.year()), 2026);
    EXPECT_EQ(static_cast<unsigned>(ymd.month()), 10);
    EXPECT_EQ(static_cast<unsigned>(ymd.day()), 18);

    auto hms = read_column_value<std::chrono::hh_mm_ss<std::chrono::seconds>>(reader, 9);
    EXPECT_EQ(hms.hours().count(), 12);
    EXPECT_EQ(hms.minutes().count(), 30);
    EXPECT_EQ(hms.seconds().count(), 45);

    auto tp = read_column_value<std::chrono::system_clock::time_point>(reader, 10);
    EXPECT_EQ(SqlTimestamp::from_time_point(tp).to_string(), "2026-10-18 12:30:45");

    auto dur = read_column_value<std::chrono::seconds>(reader, 11);
    EXPECT_EQ(dur.count(), 86400 + 7200 + 180 + 4);

    // assign_column_value
    std::vector<uint8_t> assigned_blob;
    assign_column_value(assigned_blob, reader, 6);
    EXPECT_EQ(assigned_blob, blob);

    std::string assigned_str;
    assign_column_value(assigned_str, reader, 4);
    EXPECT_EQ(assigned_str, "string_val");

    int assigned_int = 0;
    assign_column_value(assigned_int, reader, 1);
    EXPECT_EQ(assigned_int, -12345);
}

TEST(DataTypesTest, AdditionalCoverageForDataTypes) {
    // utf8_to_wstring invalid byte sequence fallback
    std::string invalid_utf8 = "\xFF\xFE\x80";
    std::wstring converted_invalid = utf8_to_wstring(invalid_utf8);
    EXPECT_FALSE(converted_invalid.empty());

    // SqlGuid methods
    SqlGuid nil_g;
    EXPECT_TRUE(nil_g.is_nil());
    EXPECT_EQ(nil_g.to_string(false, false), "00000000000000000000000000000000");
    EXPECT_EQ(nil_g.to_string(false, true), "00000000000000000000000000000000");
    EXPECT_EQ(nil_g.to_string(true, true), "00000000-0000-0000-0000-000000000000");

    SqlGuid g_upper = SqlGuid::from_string("6BA7B8109DAD11D180B400C04FD430C8");
    EXPECT_FALSE(g_upper.is_nil());
    EXPECT_EQ(g_upper.to_string(false, true), "6BA7B8109DAD11D180B400C04FD430C8");

    SqlGuid g_empty = SqlGuid::from_string("");
    EXPECT_TRUE(g_empty.is_nil());

    SqlGuid g_short = SqlGuid::from_string("invalid_short");
    EXPECT_TRUE(g_short.is_nil());

    uint8_t d4[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    SqlGuid g_parts(0x12345678, 0x1234, 0x5678, d4);
    EXPECT_FALSE(g_parts.is_nil());

    SqlGuid g_parts_null(0x12345678, 0x1234, 0x5678, nullptr);
    EXPECT_FALSE(g_parts_null.is_nil());

    // SqlNumeric conversions exceptions and edge cases
    SqlNumeric num_overflow("1e9999");
    EXPECT_EQ(num_overflow.to_double(), 0.0);

    SqlNumeric num_big("999999999999999999999999999999999999");
    EXPECT_EQ(num_big.to_int64(), 0);
    EXPECT_EQ(num_big.to_uint64(), 0);

    SqlNumeric num_neg("-123.45");
    EXPECT_EQ(num_neg.to_uint64(), 0);
    EXPECT_EQ(num_neg.to_int64(), -123);
    EXPECT_DOUBLE_EQ(num_neg.to_double(), -123.45);

    // SqlNumeric comparisons with zero and opposing signs
    EXPECT_TRUE((SqlNumeric("0") <=> SqlNumeric("5")) < 0);
    EXPECT_TRUE((SqlNumeric("0") <=> SqlNumeric("-5")) > 0);
    EXPECT_TRUE((SqlNumeric("5") <=> SqlNumeric("0")) > 0);
    EXPECT_TRUE((SqlNumeric("-5") <=> SqlNumeric("0")) < 0);
    EXPECT_TRUE((SqlNumeric("-5") <=> SqlNumeric("5")) < 0);
    EXPECT_TRUE((SqlNumeric("5") <=> SqlNumeric("-5")) > 0);

    EXPECT_TRUE((SqlNumeric("100") <=> SqlNumeric("50")) > 0);
    EXPECT_TRUE((SqlNumeric("50") <=> SqlNumeric("100")) < 0);
    EXPECT_TRUE((SqlNumeric("1.23") <=> SqlNumeric("1.25")) < 0);
    EXPECT_TRUE((SqlNumeric("1.25") <=> SqlNumeric("1.23")) > 0);
    EXPECT_TRUE((SqlNumeric("1.234") <=> SqlNumeric("1.23")) > 0);

    // SqlTimestamp string formatting with tz offset
    SqlTimestamp ts_tz_pos(2026, 1, 1, 12, 0, 0, 0, 120);
    EXPECT_EQ(ts_tz_pos.to_string(), "2026-01-01 12:00:00+02:00");

    SqlTimestamp ts_tz_neg(2026, 1, 1, 12, 0, 0, 0, -300);
    EXPECT_EQ(ts_tz_neg.to_string(), "2026-01-01 12:00:00-05:00");

    SqlTimestamp ts_long_frac = SqlTimestamp::from_string("2026-01-01 12:00:00.123456789999Z");
    EXPECT_EQ(ts_long_frac.fraction, 123456789U);

    SqlTimestamp ts_ws_tz = SqlTimestamp::from_string("2026-01-01 12:00:00   Z");
    EXPECT_EQ(ts_ws_tz.tz_offset_minutes, 0);

    SqlTimestamp ts_compact_tz = SqlTimestamp::from_string("2026-01-01 12:00:00+0200");
    EXPECT_EQ(ts_compact_tz.tz_offset_minutes, 120);

    SqlTimestamp ts_compact_tz_neg = SqlTimestamp::from_string("2026-01-01 12:00:00-0530");
    EXPECT_EQ(ts_compact_tz_neg.tz_offset_minutes, -330);

    SqlTimestamp ts_empty = SqlTimestamp::from_string("");
    EXPECT_EQ(ts_empty.year, 1970);

    // SqlInterval
    SqlInterval iv_ym_neg = SqlInterval::from_year_month(2, 6, true);
    auto months_neg = iv_ym_neg.to_duration<std::chrono::months>();
    EXPECT_EQ(months_neg.count(), -30);

    SqlInterval iv_comp_pos;
    iv_comp_pos.years = 1;
    iv_comp_pos.months = 2;
    iv_comp_pos.days = 5;
    auto comp_dur = iv_comp_pos.to_duration<std::chrono::seconds>();
    EXPECT_GT(comp_dur.count(), 0);

    SqlInterval iv_comp_neg;
    iv_comp_neg.years = 1;
    iv_comp_neg.months = 2;
    iv_comp_neg.days = 5;
    iv_comp_neg.is_negative = true;
    auto comp_dur_neg = iv_comp_neg.to_duration<std::chrono::seconds>();
    EXPECT_LT(comp_dur_neg.count(), 0);

    SqlInterval iv_neg_day = SqlInterval::from_day_second(1, 2, 3, 4, 0, true);
    auto sec_neg = iv_neg_day.to_duration<std::chrono::seconds>();
    EXPECT_LT(sec_neg.count(), 0);

    SqlInterval iv_plus = SqlInterval::from_string("+1 02:03:04");
    EXPECT_FALSE(iv_plus.is_negative);
    EXPECT_EQ(iv_plus.days, 1);

    SqlInterval iv_frac_long = SqlInterval::from_string("1 02:03:04.123456789999");
    EXPECT_EQ(iv_frac_long.fraction, 123456789U);

    SqlInterval iv_empty = SqlInterval::from_string("");
    EXPECT_EQ(iv_empty.days, 0);
}

