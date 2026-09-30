#pragma once

#include "db.hpp"

#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

#include "sqlite3.h"

namespace kos {

inline std::string col_text(sqlite3_stmt* st, int col) {
    const unsigned char* p = sqlite3_column_text(st, col);
    return p ? reinterpret_cast<const char*>(p) : "";
}

inline void run_bound_sql(sqlite3* db, const char* sql, const std::vector<int64_t>& ids) {
    sqlite3_stmt* st = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &st, nullptr) != SQLITE_OK) {
        throw std::runtime_error(std::string("prepare failed: ") + sqlite3_errmsg(db));
    }
    for (size_t i = 0; i < ids.size(); ++i) {
        sqlite3_bind_int64(st, static_cast<int>(i + 1), ids[i]);
    }
    int rc = sqlite3_step(st);
    sqlite3_finalize(st);
    if (rc != SQLITE_DONE) {
        throw std::runtime_error(std::string("sql failed: ") + sqlite3_errmsg(db));
    }
}

}  // namespace kos
