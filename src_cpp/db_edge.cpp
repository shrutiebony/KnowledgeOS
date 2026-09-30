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

void Store::delete_edges(int64_t dataset_id) {
    std::lock_guard<std::mutex> lock(mu_);
    run_bound_sql(db_,
        "DELETE FROM edges WHERE dataset_id=? OR "
        "source_document_id IN (SELECT id FROM documents WHERE dataset_id=?) OR "
        "target_document_id IN (SELECT id FROM documents WHERE dataset_id=?)",
        {dataset_id, dataset_id, dataset_id});
}

void Store::insert_edges(const std::vector<DocumentEdge>& edges) {
    std::lock_guard<std::mutex> lock(mu_);
    exec("BEGIN");
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(db_,
        "INSERT INTO edges(dataset_id,source_document_id,target_document_id,cosine,reason) VALUES(?,?,?,?,?)",
        -1, &st, nullptr);
    for (const auto& e : edges) {
        sqlite3_reset(st);
        sqlite3_bind_int64(st, 1, e.dataset_id);
        sqlite3_bind_int64(st, 2, e.source_document_id);
        sqlite3_bind_int64(st, 3, e.target_document_id);
        sqlite3_bind_double(st, 4, e.cosine);
        sqlite3_bind_text(st, 5, e.reason.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(st);
    }
    sqlite3_finalize(st);
    exec("COMMIT");
}

std::vector<DocumentEdge> Store::edges_by_dataset(int64_t dataset_id) {
    std::lock_guard<std::mutex> lock(mu_);
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(db_,
        "SELECT id,dataset_id,source_document_id,target_document_id,cosine,reason FROM edges WHERE dataset_id=?",
        -1, &st, nullptr);
    sqlite3_bind_int64(st, 1, dataset_id);
    std::vector<DocumentEdge> out;
    while (sqlite3_step(st) == SQLITE_ROW) {
        DocumentEdge e;
        e.id = sqlite3_column_int64(st, 0);
        e.dataset_id = sqlite3_column_int64(st, 1);
        e.source_document_id = sqlite3_column_int64(st, 2);
        e.target_document_id = sqlite3_column_int64(st, 3);
        e.cosine = sqlite3_column_double(st, 4);
        e.reason = col_text(st, 5);
        out.push_back(e);
    }
    sqlite3_finalize(st);
    return out;
}

std::vector<DocumentEdge> Store::edges_from(int64_t dataset_id, int64_t source_id) {
    std::lock_guard<std::mutex> lock(mu_);
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(db_,
        "SELECT id,dataset_id,source_document_id,target_document_id,cosine,reason FROM edges "
        "WHERE dataset_id=? AND source_document_id=?",
        -1, &st, nullptr);
    sqlite3_bind_int64(st, 1, dataset_id);
    sqlite3_bind_int64(st, 2, source_id);
    std::vector<DocumentEdge> out;
    while (sqlite3_step(st) == SQLITE_ROW) {
        DocumentEdge e;
        e.id = sqlite3_column_int64(st, 0);
        e.dataset_id = sqlite3_column_int64(st, 1);
        e.source_document_id = sqlite3_column_int64(st, 2);
        e.target_document_id = sqlite3_column_int64(st, 3);
        e.cosine = sqlite3_column_double(st, 4);
        e.reason = col_text(st, 5);
        out.push_back(e);
    }
    sqlite3_finalize(st);
    return out;
}

void Store::clear_dataset_contents(int64_t dataset_id) {
    std::lock_guard<std::mutex> lock(mu_);
    run_bound_sql(db_,
        "DELETE FROM edges WHERE dataset_id=? "
        "OR source_document_id IN (SELECT id FROM documents WHERE dataset_id=?) "
        "OR target_document_id IN (SELECT id FROM documents WHERE dataset_id=?)",
        {dataset_id, dataset_id, dataset_id});
    run_bound_sql(db_, "DELETE FROM documents WHERE dataset_id=?", {dataset_id});
}

}  // namespace kos
