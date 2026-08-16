#include <iostream>
#include <string>
#include <vector>
#include <optional>
#include "cpplinq/cpplinq.hpp"

using namespace cpplinq;

// 1. Define Entity Struct
struct User {
    int id = 0;
    std::string name;
    std::optional<std::string> email;
    int age = 0;
};

// 2. Define Table Mapping
inline const auto users_table = table<User>(
    "users",
    column("id",    &User::id,    primary_key, auto_increment),
    column("name",  &User::name,  not_null),
    column("email", &User::email),
    column("age",   &User::age,   not_null)
);

int main() {
    std::cout << "==================================================" << std::endl;
    std::cout << "   cpplinq: LINQ-to-SQL C++ Database Library Demo " << std::endl;
    std::cout << "==================================================" << std::endl << std::endl;

    // 3. Connect to Database (in-memory SQLite)
    auto db = cpplinq::connect<cpplinq::sqlite>(":memory:");

    // 4. Ensure Table Exists
    std::cout << "--> Creating table 'users'..." << std::endl;
    db.ensure_table(users_table);

    // 5. Insert Entities
    std::cout << "--> Inserting sample users..." << std::endl;
    int64_t id_alice   = db.insert(users_table, User{0, "Alice",   "alice@example.com", 30});
    int64_t id_bob     = db.insert(users_table, User{0, "Bob",     std::nullopt,        25});
    int64_t id_charlie = db.insert(users_table, User{0, "Charlie", "charlie@corp.com",  17});
    int64_t id_diana   = db.insert(users_table, User{0, "Diana",   "diana@example.com", 35});
    int64_t id_evan    = db.insert(users_table, User{0, "Evan",    std::nullopt,        42});

    std::cout << "    Inserted 5 users with IDs: "
              << id_alice << ", " << id_bob << ", " << id_charlie << ", "
              << id_diana << ", " << id_evan << std::endl << std::endl;

    // 6. Query with Fluent Filtering & Sorting
    std::cout << "--> Query: Users with age >= 25, ordered by age desc:" << std::endl;
    auto adults = db.from(users_table)
                    .where(users_table["age"] >= 25)
                    .order_by_desc(users_table["age"])
                    .to_vector();

    for (const auto& u : adults) {
        std::cout << "    [" << u.id << "] " << u.name
                  << " (Age: " << u.age << ", Email: "
                  << (u.email ? *u.email : "<NULL>") << ")" << std::endl;
    }
    std::cout << std::endl;

    // 7. Query with Combined Logic & Pagination
    std::cout << "--> Query: Limit 2, Offset 1, ordered by name:" << std::endl;
    auto paged = db.from(users_table)
                   .order_by(users_table["name"])
                   .limit(2)
                   .offset(1)
                   .to_vector();

    for (const auto& u : paged) {
        std::cout << "    [" << u.id << "] " << u.name << " (Age: " << u.age << ")" << std::endl;
    }
    std::cout << std::endl;

    // 8. Aggregates (Count, Average, Min, Max, Sum)
    std::cout << "--> Aggregates on 'users':" << std::endl;
    size_t total_count = db.from(users_table).count();
    auto avg_age       = db.from(users_table).avg(users_table["age"]);
    auto min_age       = db.from(users_table).min_val(users_table["age"]);
    auto max_age       = db.from(users_table).max_val(users_table["age"]);
    auto sum_age       = db.from(users_table).sum(users_table["age"]);

    std::cout << "    Total Count : " << total_count << std::endl;
    std::cout << "    Average Age : " << (avg_age ? *avg_age : 0.0) << std::endl;
    std::cout << "    Min Age     : " << (min_age ? *min_age : 0.0) << std::endl;
    std::cout << "    Max Age     : " << (max_age ? *max_age : 0.0) << std::endl;
    std::cout << "    Sum of Ages : " << (sum_age ? *sum_age : 0.0) << std::endl << std::endl;

    // 9. Update Records
    std::cout << "--> Updating Bob's email and age..." << std::endl;
    size_t updated = db.from(users_table)
                       .where(users_table["name"] == "Bob")
                       .update({
                           users_table["email"] = "bob.new@example.com",
                           users_table["age"]   = 26
                       });
    std::cout << "    Rows updated: " << updated << std::endl;

    auto bob = db.from(users_table).where(users_table["name"] == "Bob").first();
    if (bob) {
        std::cout << "    Bob's updated record: [" << bob->id << "] " << bob->name
                  << " -> " << (bob->email ? *bob->email : "<NULL>")
                  << " (Age: " << bob->age << ")" << std::endl;
    }
    std::cout << std::endl;

    // 10. Delete Records
    std::cout << "--> Deleting underage users (age < 18)..." << std::endl;
    size_t deleted = db.from(users_table)
                       .where(users_table["age"] < 18)
                       .remove();
    std::cout << "    Rows deleted: " << deleted << std::endl;
    std::cout << "    Remaining count: " << db.from(users_table).count() << std::endl << std::endl;

    // 11. Transactions (Rollback & Commit)
    std::cout << "--> Transaction test: Rollback demonstration..." << std::endl;
    {
        auto txn = db.begin_transaction();
        db.insert(users_table, User{0, "GhostUser", "ghost@example.com", 99});
        std::cout << "    Count inside transaction: " << db.from(users_table).count() << std::endl;
        // Letting txn go out of scope without calling txn.commit() triggers automatic rollback
    }
    std::cout << "    Count after rollback: " << db.from(users_table).count() << " (Unchanged!)" << std::endl;

    std::cout << "--> Transaction test: Commit demonstration..." << std::endl;
    {
        auto txn = db.begin_transaction();
        db.insert(users_table, User{0, "PermanentUser", "perm@example.com", 29});
        txn.commit();
    }
    std::cout << "    Count after commit: " << db.from(users_table).count() << std::endl << std::endl;

    std::cout << "==================================================" << std::endl;
    std::cout << "   cpplinq Demo Completed Successfully!           " << std::endl;
    std::cout << "==================================================" << std::endl;

    return 0;
}

