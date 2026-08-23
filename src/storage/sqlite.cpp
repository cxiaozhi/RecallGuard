#include "storage/sqlite.hpp"

#include <utility>

namespace recall_memory::sqlite {
namespace {

void check(int result, sqlite3* database, std::string_view operation) {
    if (result == SQLITE_OK || result == SQLITE_ROW || result == SQLITE_DONE) return;
    throw Error(std::string(operation) + ": " + sqlite3_errmsg(database));
}

}  // namespace

Statement::Statement(sqlite3* database, std::string_view sql) {
    const auto result = sqlite3_prepare_v2(database, sql.data(), static_cast<int>(sql.size()), &statement_, nullptr);
    check(result, database, "prepare statement");
}

Statement::~Statement() {
    if (statement_) sqlite3_finalize(statement_);
}

Statement::Statement(Statement&& other) noexcept : statement_(std::exchange(other.statement_, nullptr)) {}

Statement& Statement::operator=(Statement&& other) noexcept {
    if (this == &other) return *this;
    if (statement_) sqlite3_finalize(statement_);
    statement_ = std::exchange(other.statement_, nullptr);
    return *this;
}

void Statement::bind(int index, std::string_view value) {
    check(sqlite3_bind_text(statement_, index, value.data(), static_cast<int>(value.size()), SQLITE_TRANSIENT),
          sqlite3_db_handle(statement_), "bind text");
}

void Statement::bind(int index, std::int64_t value) {
    check(sqlite3_bind_int64(statement_, index, value), sqlite3_db_handle(statement_), "bind integer");
}

void Statement::bind(int index, double value) {
    check(sqlite3_bind_double(statement_, index, value), sqlite3_db_handle(statement_), "bind real");
}

void Statement::bind_null(int index) {
    check(sqlite3_bind_null(statement_, index), sqlite3_db_handle(statement_), "bind null");
}

bool Statement::step() {
    const auto result = sqlite3_step(statement_);
    check(result, sqlite3_db_handle(statement_), "step statement");
    return result == SQLITE_ROW;
}

void Statement::execute() {
    if (step()) throw Error("Statement unexpectedly returned a row");
}

void Statement::reset() {
    check(sqlite3_reset(statement_), sqlite3_db_handle(statement_), "reset statement");
    check(sqlite3_clear_bindings(statement_), sqlite3_db_handle(statement_), "clear statement bindings");
}

std::string Statement::text(int column) const {
    const auto* value = sqlite3_column_text(statement_, column);
    if (!value) return {};
    const auto size = sqlite3_column_bytes(statement_, column);
    return {reinterpret_cast<const char*>(value), static_cast<std::size_t>(size)};
}

std::int64_t Statement::integer(int column) const { return sqlite3_column_int64(statement_, column); }
double Statement::real(int column) const { return sqlite3_column_double(statement_, column); }
bool Statement::is_null(int column) const { return sqlite3_column_type(statement_, column) == SQLITE_NULL; }

Database::Database(const std::filesystem::path& path) {
    const auto utf8 = path.generic_string();
    const auto result = sqlite3_open_v2(
        utf8.c_str(), &database_, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX, nullptr);
    if (result != SQLITE_OK) {
        const std::string message = database_ ? sqlite3_errmsg(database_) : "unknown error";
        if (database_) sqlite3_close(database_);
        database_ = nullptr;
        throw Error("open database: " + message);
    }
    sqlite3_busy_timeout(database_, 5000);
}

Database::~Database() {
    if (database_) sqlite3_close(database_);
}

void Database::execute(std::string_view sql) {
    char* error = nullptr;
    const auto result = sqlite3_exec(database_, std::string(sql).c_str(), nullptr, nullptr, &error);
    if (result != SQLITE_OK) {
        const std::string message = error ? error : sqlite3_errmsg(database_);
        sqlite3_free(error);
        throw Error("execute SQL: " + message);
    }
}

Statement Database::prepare(std::string_view sql) { return Statement(database_, sql); }

Transaction::Transaction(Database& database) : database_(database) { database_.execute("BEGIN IMMEDIATE"); }

Transaction::~Transaction() {
    if (!committed_) {
        try {
            database_.execute("ROLLBACK");
        } catch (...) {
        }
    }
}

void Transaction::commit() {
    database_.execute("COMMIT");
    committed_ = true;
}

}  // namespace recall_memory::sqlite
