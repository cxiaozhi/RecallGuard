#pragma once

#include <sqlite3.h>

#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>

namespace recall_memory::sqlite {

class Error : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class Statement {
public:
    Statement(sqlite3* database, std::string_view sql);
    ~Statement();

    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;
    Statement(Statement&& other) noexcept;
    Statement& operator=(Statement&& other) noexcept;

    void bind(int index, std::string_view value);
    void bind(int index, std::int64_t value);
    void bind(int index, double value);
    void bind_null(int index);
    bool step();
    void execute();
    void reset();

    std::string text(int column) const;
    std::int64_t integer(int column) const;
    double real(int column) const;
    bool is_null(int column) const;

private:
    sqlite3_stmt* statement_{nullptr};
};

class Database {
public:
    explicit Database(const std::filesystem::path& path);
    ~Database();

    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    void execute(std::string_view sql);
    Statement prepare(std::string_view sql);
    sqlite3* handle() const { return database_; }

private:
    sqlite3* database_{nullptr};
};

class Transaction {
public:
    explicit Transaction(Database& database);
    ~Transaction();

    Transaction(const Transaction&) = delete;
    Transaction& operator=(const Transaction&) = delete;

    void commit();

private:
    Database& database_;
    bool committed_{false};
};

}  // namespace recall_memory::sqlite
