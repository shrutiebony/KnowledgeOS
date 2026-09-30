#include "db.hpp"

#include "util.hpp"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include "sqlite3.h"

#include "db_sql.hpp"

namespace kos {

Dataset Store::read_dataset(void* stmt_v) {
    auto* st = static_cast<sqlite3_stmt*>(stmt_v);
    Dataset d;
    d.id = sqlite3_column_int64(st, 0);
    d.name = col_text(st, 1);
    d.kind = col_text(st, 2);
    d.parent_topic = col_text(st, 3);
    if (sqlite3_column_type(st, 4) != SQLITE_NULL) d.parent_dataset_id = sqlite3_column_int64(st, 4);
    d.graph_sage_status = col_text(st, 5);
    d.graph_sage_message = col_text(st, 6);
    d.created_at = col_text(st, 7);
    d.last_analyzed_at = col_text(st, 8);
    d.analysis_state = col_text(st, 9);
    if (d.graph_sage_status.empty()) d.graph_sage_status = GS_NOT_TRAINED;
    if (d.graph_sage_message.empty()) d.graph_sage_message = GS_DEFAULT_MSG;
    return d;
}

Dataset Store::insert_dataset(Dataset d) {
    std::lock_guard<std::mutex> lock(mu_);
    if (d.created_at.empty()) d.created_at = now_iso();
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(db_,
        "INSERT INTO datasets(name,kind,parent_topic,parent_dataset_id,graph_sage_status,graph_sage_message,"
        "created_at,last_analyzed_at,analysis_state) VALUES(?,?,?,?,?,?,?,?,?)",
        -1, &st, nullptr);
    sqlite3_bind_text(st, 1, d.name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, d.kind.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 3, d.parent_topic.c_str(), -1, SQLITE_TRANSIENT);
    if (d.parent_dataset_id) sqlite3_bind_int64(st, 4, *d.parent_dataset_id);
    else sqlite3_bind_null(st, 4);
    sqlite3_bind_text(st, 5, d.graph_sage_status.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 6, d.graph_sage_message.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 7, d.created_at.c_str(), -1, SQLITE_TRANSIENT);
    if (d.last_analyzed_at.empty()) sqlite3_bind_null(st, 8);
    else sqlite3_bind_text(st, 8, d.last_analyzed_at.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 9, d.analysis_state.c_str(), -1, SQLITE_TRANSIENT);
    if (sqlite3_step(st) != SQLITE_DONE) {
        sqlite3_finalize(st);
        throw std::runtime_error("insert dataset failed");
    }
    d.id = sqlite3_last_insert_rowid(db_);
    sqlite3_finalize(st);
    return d;
}

std::optional<Dataset> Store::get_dataset(int64_t id) {
    std::lock_guard<std::mutex> lock(mu_);
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(db_, "SELECT id,name,kind,parent_topic,parent_dataset_id,graph_sage_status,"
                            "graph_sage_message,created_at,last_analyzed_at,analysis_state FROM datasets WHERE id=?",
                       -1, &st, nullptr);
    sqlite3_bind_int64(st, 1, id);
    std::optional<Dataset> out;
    if (sqlite3_step(st) == SQLITE_ROW) out = read_dataset(st);
    sqlite3_finalize(st);
    return out;
}

std::optional<Dataset> Store::find_by_name_ignore_case(const std::string& name) {
    std::lock_guard<std::mutex> lock(mu_);
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(db_, "SELECT id,name,kind,parent_topic,parent_dataset_id,graph_sage_status,"
                            "graph_sage_message,created_at,last_analyzed_at,analysis_state FROM datasets "
                            "WHERE lower(name)=lower(?) LIMIT 1",
                       -1, &st, nullptr);
    sqlite3_bind_text(st, 1, name.c_str(), -1, SQLITE_TRANSIENT);
    std::optional<Dataset> out;
    if (sqlite3_step(st) == SQLITE_ROW) out = read_dataset(st);
    sqlite3_finalize(st);
    return out;
}

std::optional<Dataset> Store::find_first_by_kind(const std::string& kind) {
    std::lock_guard<std::mutex> lock(mu_);
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(db_, "SELECT id,name,kind,parent_topic,parent_dataset_id,graph_sage_status,"
                            "graph_sage_message,created_at,last_analyzed_at,analysis_state FROM datasets "
                            "WHERE kind=? ORDER BY created_at DESC LIMIT 1",
                       -1, &st, nullptr);
    sqlite3_bind_text(st, 1, kind.c_str(), -1, SQLITE_TRANSIENT);
    std::optional<Dataset> out;
    if (sqlite3_step(st) == SQLITE_ROW) out = read_dataset(st);
    sqlite3_finalize(st);
    return out;
}

std::optional<Dataset> Store::find_by_kind_and_name(const std::string& kind, const std::string& name) {
    std::lock_guard<std::mutex> lock(mu_);
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(db_, "SELECT id,name,kind,parent_topic,parent_dataset_id,graph_sage_status,"
                            "graph_sage_message,created_at,last_analyzed_at,analysis_state FROM datasets "
                            "WHERE kind=? AND lower(name)=lower(?) LIMIT 1",
                       -1, &st, nullptr);
    sqlite3_bind_text(st, 1, kind.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, name.c_str(), -1, SQLITE_TRANSIENT);
    std::optional<Dataset> out;
    if (sqlite3_step(st) == SQLITE_ROW) out = read_dataset(st);
    sqlite3_finalize(st);
    return out;
}

std::vector<Dataset> Store::all_datasets() {
    std::lock_guard<std::mutex> lock(mu_);
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(db_, "SELECT id,name,kind,parent_topic,parent_dataset_id,graph_sage_status,"
                            "graph_sage_message,created_at,last_analyzed_at,analysis_state FROM datasets "
                            "ORDER BY created_at DESC",
                       -1, &st, nullptr);
    std::vector<Dataset> out;
    while (sqlite3_step(st) == SQLITE_ROW) out.push_back(read_dataset(st));
    sqlite3_finalize(st);
    return out;
}

std::vector<Dataset> Store::datasets_by_kind_and_name(const std::string& kind, const std::string& name) {
    std::lock_guard<std::mutex> lock(mu_);
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(db_, "SELECT id,name,kind,parent_topic,parent_dataset_id,graph_sage_status,"
                            "graph_sage_message,created_at,last_analyzed_at,analysis_state FROM datasets "
                            "WHERE kind=? AND lower(name)=lower(?) ORDER BY created_at DESC",
                       -1, &st, nullptr);
    sqlite3_bind_text(st, 1, kind.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, name.c_str(), -1, SQLITE_TRANSIENT);
    std::vector<Dataset> out;
    while (sqlite3_step(st) == SQLITE_ROW) out.push_back(read_dataset(st));
    sqlite3_finalize(st);
    return out;
}

void Store::save_dataset(const Dataset& d) {
    std::lock_guard<std::mutex> lock(mu_);
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(db_,
        "UPDATE datasets SET name=?,kind=?,parent_topic=?,parent_dataset_id=?,graph_sage_status=?,"
        "graph_sage_message=?,created_at=?,last_analyzed_at=?,analysis_state=? WHERE id=?",
        -1, &st, nullptr);
    sqlite3_bind_text(st, 1, d.name.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, d.kind.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 3, d.parent_topic.c_str(), -1, SQLITE_TRANSIENT);
    if (d.parent_dataset_id) sqlite3_bind_int64(st, 4, *d.parent_dataset_id);
    else sqlite3_bind_null(st, 4);
    sqlite3_bind_text(st, 5, d.graph_sage_status.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 6, d.graph_sage_message.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 7, d.created_at.c_str(), -1, SQLITE_TRANSIENT);
    if (d.last_analyzed_at.empty()) sqlite3_bind_null(st, 8);
    else sqlite3_bind_text(st, 8, d.last_analyzed_at.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 9, d.analysis_state.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 10, d.id);
    sqlite3_step(st);
    sqlite3_finalize(st);
}

static void delete_dataset_rows_locked(sqlite3* db, int64_t id) {
    // Drop edges owned by this collection, plus any edge that still points at its documents.
    run_bound_sql(db,
        "DELETE FROM edges WHERE dataset_id=? OR "
        "source_document_id IN (SELECT id FROM documents WHERE dataset_id=?) OR "
        "target_document_id IN (SELECT id FROM documents WHERE dataset_id=?)",
        {id, id, id});
    run_bound_sql(db,
        "DELETE FROM edges WHERE dataset_id IN (SELECT id FROM datasets WHERE parent_dataset_id=?) OR "
        "source_document_id IN (SELECT id FROM documents WHERE dataset_id IN "
        "(SELECT id FROM datasets WHERE parent_dataset_id=?)) OR "
        "target_document_id IN (SELECT id FROM documents WHERE dataset_id IN "
        "(SELECT id FROM datasets WHERE parent_dataset_id=?))",
        {id, id, id});
    run_bound_sql(db,
        "DELETE FROM documents WHERE dataset_id=? OR dataset_id IN "
        "(SELECT id FROM datasets WHERE parent_dataset_id=?)",
        {id, id});
    run_bound_sql(db, "DELETE FROM datasets WHERE parent_dataset_id=?", {id});
    run_bound_sql(db, "DELETE FROM datasets WHERE id=?", {id});
    // Leftovers from older deletes that only dropped the datasets row.
    sqlite3_exec(db, "DELETE FROM edges WHERE dataset_id NOT IN (SELECT id FROM datasets)",
                 nullptr, nullptr, nullptr);
    sqlite3_exec(db, "DELETE FROM documents WHERE dataset_id NOT IN (SELECT id FROM datasets)",
                 nullptr, nullptr, nullptr);
    sqlite3_exec(db,
        "DELETE FROM edges WHERE source_document_id NOT IN (SELECT id FROM documents) OR "
        "target_document_id NOT IN (SELECT id FROM documents)",
        nullptr, nullptr, nullptr);
}

void Store::delete_dataset(int64_t id) {
    std::lock_guard<std::mutex> lock(mu_);
    exec("BEGIN IMMEDIATE");
    try {
        delete_dataset_rows_locked(db_, id);
        exec("COMMIT");
    } catch (...) {
        sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
        throw;
    }
}

}  // namespace kos
