#include <gtest/gtest.h>
#include "cpplinq/cpplinq.hpp"
#include <stop_token>
#include <thread>
#include <chrono>

using namespace cpplinq;

struct StreamItem {
    int id = 0;
    std::string name;
    int value = 0;
};

inline const auto stream_items = table<StreamItem>(
    "stream_items",
    column("id",    &StreamItem::id,    primary_key, auto_increment),
    column("name",  &StreamItem::name,  not_null),
    column("value", &StreamItem::value, not_null)
);

class StreamingTest : public ::testing::Test {
protected:
    void SetUp() override {
        conn_ = make_connection<sqlite>(":memory:");
        conn_->open();
        db_ = std::make_unique<DbContext<sqlite>>(*conn_);
        db_->ensure_table(stream_items);

        // Insert 100 items
        std::vector<StreamItem> items;
        items.reserve(100);
        for (int i = 1; i <= 100; ++i) {
            items.push_back(StreamItem{0, "Item_" + std::to_string(i), i * 10});
        }
        db_->insert_many(stream_items, items);
    }

    std::unique_ptr<IConnection> conn_;
    std::unique_ptr<DbContext<sqlite>> db_;
};

// 1. Test Raw IConnection::stream
TEST_F(StreamingTest, RawConnectionStream) {
    int count = 0;
    for (const auto& row : conn_->stream("SELECT id, name, value FROM stream_items ORDER BY id")) {
        EXPECT_FALSE(row.is_null(0));
        EXPECT_GT(row.get_int64(0), 0);
        EXPECT_FALSE(row.get_string(1).empty());
        EXPECT_EQ(row.get_int64(2), row.get_int64(0) * 10);
        count++;
    }
    EXPECT_EQ(count, 100);
}

// 2. Test Raw Stream with Bound Parameters and ExecutionOptions
TEST_F(StreamingTest, RawConnectionStreamWithParamsAndOptions) {
    execution_options options;
    options.query_timeout_seconds = 30;

    int count = 0;
    for (const auto& row : conn_->stream(
             "SELECT id, name, value FROM stream_items WHERE value >= ? ORDER BY id",
             {BoundValue{int64_t(500)}},
             options)) {
        EXPECT_GE(row.get_int64(2), 500);
        count++;
    }
    EXPECT_EQ(count, 51); // items 50..100 (50*10 = 500 up to 1000)
}

// 3. Test QueryBuilder::stream with Typed Entities
TEST_F(StreamingTest, QueryBuilderTypedStream) {
    int count = 0;
    int expected_id = 1;
    for (const auto& item : db_->from(stream_items).order_by(stream_items["id"]).stream()) {
        EXPECT_EQ(item.id, expected_id);
        EXPECT_EQ(item.name, "Item_" + std::to_string(expected_id));
        EXPECT_EQ(item.value, expected_id * 10);
        expected_id++;
        count++;
    }
    EXPECT_EQ(count, 100);
}

// 4. Test Early Break from Stream (does not load remaining items)
TEST_F(StreamingTest, StreamEarlyTermination) {
    int count = 0;
    for (const auto& item : db_->from(stream_items).order_by(stream_items["id"]).stream()) {
        count++;
        if (count == 5) {
            break; // early exit
        }
    }
    EXPECT_EQ(count, 5);
}

// 5. Test Cooperative Cancellation via std::stop_token
TEST_F(StreamingTest, CooperativeCancellationWithStopToken) {
    std::stop_source stop_source;
    execution_options options;
    options.stop_token = stop_source.get_token();

    int count = 0;
    EXPECT_THROW({
        for (const auto& item : db_->from(stream_items).order_by(stream_items["id"]).stream(options)) {
            (void)item;
            count++;
            if (count == 10) {
                stop_source.request_stop(); // Request cancellation
            }
        }
    }, operation_cancelled);

    EXPECT_EQ(count, 10);
}

// 6. Test Driver Capabilities & Driver Info Introspection
TEST_F(StreamingTest, DriverCapabilitiesAndInfo) {
    auto info = conn_->info();
    EXPECT_EQ(info.driver_name, "SQLite");
    EXPECT_FALSE(info.driver_version.empty());
    EXPECT_EQ(info.dbms_name, "SQLite");

    auto caps = conn_->capabilities();
    EXPECT_TRUE(caps.cancel);
    EXPECT_TRUE(caps.streaming);
    EXPECT_TRUE(caps.query_timeout);
    EXPECT_TRUE(caps.transactions);
    EXPECT_TRUE(caps.returning_clause);
    EXPECT_TRUE(caps.upsert);
    EXPECT_TRUE(caps.window_functions);
    EXPECT_TRUE(caps.ctes);

    if (!caps.cancel) {
        FAIL() << "SQLite driver reported cancel as false";
    }
}

// 7. Test Statement Cancel
TEST_F(StreamingTest, StatementCancelExplicit) {
    auto stmt = conn_->prepare("SELECT id, name FROM stream_items");
    EXPECT_NO_THROW(stmt->cancel());
}

// 8. Test RowRecord Conversion Helpers
TEST(StreamingExtraTest, RowRecordConversions) {
    RowRecord row;
    row.values.push_back(BoundValue{int64_t(100)});
    row.values.push_back(BoundValue{uint64_t(200)});
    row.values.push_back(BoundValue{double(300.5)});
    row.values.push_back(BoundValue{bool(true)});
    row.values.push_back(BoundValue{std::string("400")});
    row.values.push_back(BoundValue{std::wstring(L"WideText")});
    row.values.push_back(BoundValue{SqlNumeric("500.25")});
    row.values.push_back(BoundValue{SqlDate(2026, 8, 21)});
    row.values.push_back(BoundValue{SqlTime(12, 0, 0, 0)});
    row.values.push_back(BoundValue{SqlTimestamp(2026, 8, 21, 12, 0, 0, 0)});
    SqlInterval iv;
    iv.type = IntervalType::Day;
    iv.days = 5;
    row.values.push_back(BoundValue{iv});
    row.values.push_back(BoundValue{SqlGuid("a1b2c3d4-e5f6-7a8b-9c0d-1e2f3a4b5c6d")});
    row.values.push_back(BoundValue{std::monostate{}});

    // get_int64
    EXPECT_EQ(row.get_int64(0), 100);
    EXPECT_EQ(row.get_int64(1), 200);
    EXPECT_EQ(row.get_int64(2), 300);
    EXPECT_EQ(row.get_int64(3), 1);
    EXPECT_EQ(row.get_int64(4), 400);
    EXPECT_EQ(row.get_int64(6), 500);
    EXPECT_EQ(row.get_int64(999), 0);

    // get_double
    EXPECT_DOUBLE_EQ(row.get_double(0), 100.0);
    EXPECT_DOUBLE_EQ(row.get_double(1), 200.0);
    EXPECT_DOUBLE_EQ(row.get_double(2), 300.5);
    EXPECT_DOUBLE_EQ(row.get_double(4), 400.0);
    EXPECT_NEAR(row.get_double(6), 500.25, 1e-4);
    EXPECT_DOUBLE_EQ(row.get_double(999), 0.0);

    // get_string
    EXPECT_EQ(row.get_string(0), "100");
    EXPECT_EQ(row.get_string(1), "200");
    EXPECT_EQ(row.get_string(3), "true");
    EXPECT_EQ(row.get_string(4), "400");
    EXPECT_EQ(row.get_string(5), "WideText");
    EXPECT_EQ(row.get_string(6), "500.25");
    EXPECT_EQ(row.get_string(7), "2026-08-21");
    EXPECT_EQ(row.get_string(8), "12:00:00");
    EXPECT_EQ(row.get_string(9), "2026-08-21 12:00:00");
    EXPECT_EQ(row.get_string(10), "5 00:00:00");
    EXPECT_EQ(row.get_string(11), "a1b2c3d4-e5f6-7a8b-9c0d-1e2f3a4b5c6d");
    EXPECT_EQ(row.get_string(999), "");

    // get_bool
    EXPECT_TRUE(row.get_bool(0));
    EXPECT_TRUE(row.get_bool(3));
    EXPECT_FALSE(row.get_bool(999));
}

// 9. Test Raw RowStream Cancellation with Stop Token
TEST_F(StreamingTest, RawStreamCancellation) {
    std::stop_source stop_source;
    execution_options options;
    options.stop_token = stop_source.get_token();

    int count = 0;
    EXPECT_THROW({
        for (const auto& row : conn_->stream("SELECT id, name, value FROM stream_items ORDER BY id", {}, options)) {
            (void)row;
            count++;
            if (count == 5) {
                stop_source.request_stop();
            }
        }
    }, operation_cancelled);

    EXPECT_EQ(count, 5);
}

