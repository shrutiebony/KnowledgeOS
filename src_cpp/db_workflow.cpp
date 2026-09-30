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

void Store::fail_interrupted_workflows() {
    std::lock_guard<std::mutex> lock(mu_);
    exec("UPDATE tasks SET status='failed', error='interrupted' WHERE status='running' OR status='ready';");
    exec("UPDATE workflows SET status='failed', error='interrupted' WHERE status='running' OR status='pending';");
}

static Workflow read_workflow(sqlite3_stmt* st) {
    Workflow w;
    w.id = sqlite3_column_int64(st, 0);
    w.dataset_id = sqlite3_column_int64(st, 1);
    w.status = workflow_status_from_str(col_text(st, 2));
    w.worker_count = sqlite3_column_int(st, 3);
    w.chunk_size = sqlite3_column_int(st, 4);
    w.document_count = sqlite3_column_int(st, 5);
    w.created_at = col_text(st, 6);
    w.started_at = col_text(st, 7);
    w.finished_at = col_text(st, 8);
    w.error = col_text(st, 9);
    return w;
}

int64_t Store::insert_workflow(Workflow w) {
    std::lock_guard<std::mutex> lock(mu_);
    if (w.created_at.empty()) w.created_at = now_iso();
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(db_,
        "INSERT INTO workflows(dataset_id,status,worker_count,chunk_size,document_count,created_at,started_at,finished_at,error) "
        "VALUES(?,?,?,?,?,?,?,?,?)",
        -1, &st, nullptr);
    sqlite3_bind_int64(st, 1, w.dataset_id);
    sqlite3_bind_text(st, 2, workflow_status_str(w.status), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(st, 3, w.worker_count);
    sqlite3_bind_int(st, 4, w.chunk_size);
    sqlite3_bind_int(st, 5, w.document_count);
    sqlite3_bind_text(st, 6, w.created_at.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 7, w.started_at.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 8, w.finished_at.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 9, w.error.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(st);
    int64_t id = sqlite3_last_insert_rowid(db_);
    sqlite3_finalize(st);
    return id;
}

void Store::update_workflow(const Workflow& w) {
    std::lock_guard<std::mutex> lock(mu_);
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(db_,
        "UPDATE workflows SET status=?,started_at=?,finished_at=?,error=?,worker_count=?,chunk_size=?,document_count=? WHERE id=?",
        -1, &st, nullptr);
    sqlite3_bind_text(st, 1, workflow_status_str(w.status), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, w.started_at.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 3, w.finished_at.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 4, w.error.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(st, 5, w.worker_count);
    sqlite3_bind_int(st, 6, w.chunk_size);
    sqlite3_bind_int(st, 7, w.document_count);
    sqlite3_bind_int64(st, 8, w.id);
    sqlite3_step(st);
    sqlite3_finalize(st);
}

std::optional<Workflow> Store::get_workflow(int64_t id) {
    std::lock_guard<std::mutex> lock(mu_);
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(db_,
        "SELECT id,dataset_id,status,worker_count,chunk_size,document_count,created_at,started_at,finished_at,error "
        "FROM workflows WHERE id=?",
        -1, &st, nullptr);
    sqlite3_bind_int64(st, 1, id);
    std::optional<Workflow> out;
    if (sqlite3_step(st) == SQLITE_ROW) out = read_workflow(st);
    sqlite3_finalize(st);
    return out;
}

std::optional<Workflow> Store::latest_workflow(int64_t dataset_id) {
    std::lock_guard<std::mutex> lock(mu_);
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(db_,
        "SELECT id,dataset_id,status,worker_count,chunk_size,document_count,created_at,started_at,finished_at,error "
        "FROM workflows WHERE dataset_id=? ORDER BY id DESC LIMIT 1",
        -1, &st, nullptr);
    sqlite3_bind_int64(st, 1, dataset_id);
    std::optional<Workflow> out;
    if (sqlite3_step(st) == SQLITE_ROW) out = read_workflow(st);
    sqlite3_finalize(st);
    return out;
}

int64_t Store::insert_task(Task t) {
    std::lock_guard<std::mutex> lock(mu_);
    if (t.created_at.empty()) t.created_at = now_iso();
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(db_,
        "INSERT INTO tasks(workflow_id,dataset_id,task_type,status,chunk_start,chunk_end,fingerprint,error,"
        "created_at,started_at,finished_at,worker_id,bytes_moved,duration_ms) "
        "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?)",
        -1, &st, nullptr);
    sqlite3_bind_int64(st, 1, t.workflow_id);
    sqlite3_bind_int64(st, 2, t.dataset_id);
    sqlite3_bind_text(st, 3, task_type_str(t.type), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 4, task_status_str(t.status), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(st, 5, t.chunk_start);
    sqlite3_bind_int(st, 6, t.chunk_end);
    sqlite3_bind_text(st, 7, t.fingerprint.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 8, t.error.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 9, t.created_at.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 10, t.started_at.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 11, t.finished_at.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(st, 12, t.worker_id);
    sqlite3_bind_int64(st, 13, t.bytes_moved);
    sqlite3_bind_double(st, 14, t.duration_ms);
    sqlite3_step(st);
    int64_t id = sqlite3_last_insert_rowid(db_);
    sqlite3_finalize(st);
    return id;
}

void Store::update_task(const Task& t) {
    std::lock_guard<std::mutex> lock(mu_);
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(db_,
        "UPDATE tasks SET status=?,started_at=?,finished_at=?,worker_id=?,error=?,bytes_moved=?,duration_ms=? WHERE id=?",
        -1, &st, nullptr);
    sqlite3_bind_text(st, 1, task_status_str(t.status), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, t.started_at.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 3, t.finished_at.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(st, 4, t.worker_id);
    sqlite3_bind_text(st, 5, t.error.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 6, t.bytes_moved);
    sqlite3_bind_double(st, 7, t.duration_ms);
    sqlite3_bind_int64(st, 8, t.id);
    sqlite3_step(st);
    sqlite3_finalize(st);
}

void Store::insert_task_dependency(int64_t task_id, int64_t depends_on) {
    std::lock_guard<std::mutex> lock(mu_);
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(db_, "INSERT INTO task_dependencies(task_id,depends_on_task_id) VALUES(?,?)", -1, &st, nullptr);
    sqlite3_bind_int64(st, 1, task_id);
    sqlite3_bind_int64(st, 2, depends_on);
    sqlite3_step(st);
    sqlite3_finalize(st);
}

std::vector<Task> Store::tasks_by_workflow(int64_t workflow_id) {
    std::lock_guard<std::mutex> lock(mu_);
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(db_,
        "SELECT id,workflow_id,dataset_id,task_type,status,chunk_start,chunk_end,fingerprint,error,"
        "created_at,started_at,finished_at,worker_id,bytes_moved,duration_ms FROM tasks WHERE workflow_id=? ORDER BY id",
        -1, &st, nullptr);
    sqlite3_bind_int64(st, 1, workflow_id);
    std::vector<Task> out;
    while (sqlite3_step(st) == SQLITE_ROW) {
        Task t;
        t.id = sqlite3_column_int64(st, 0);
        t.workflow_id = sqlite3_column_int64(st, 1);
        t.dataset_id = sqlite3_column_int64(st, 2);
        t.type = task_type_from_str(col_text(st, 3));
        t.status = task_status_from_str(col_text(st, 4));
        t.chunk_start = sqlite3_column_int(st, 5);
        t.chunk_end = sqlite3_column_int(st, 6);
        t.fingerprint = col_text(st, 7);
        t.error = col_text(st, 8);
        t.created_at = col_text(st, 9);
        t.started_at = col_text(st, 10);
        t.finished_at = col_text(st, 11);
        t.worker_id = sqlite3_column_int(st, 12);
        t.bytes_moved = sqlite3_column_int64(st, 13);
        t.duration_ms = sqlite3_column_double(st, 14);
        out.push_back(t);
    }
    sqlite3_finalize(st);

    sqlite3_stmt* dep = nullptr;
    sqlite3_prepare_v2(db_,
        "SELECT task_id,depends_on_task_id FROM task_dependencies WHERE task_id IN (SELECT id FROM tasks WHERE workflow_id=?)",
        -1, &dep, nullptr);
    sqlite3_bind_int64(dep, 1, workflow_id);
    std::unordered_map<int64_t, std::vector<int64_t>> deps;
    while (sqlite3_step(dep) == SQLITE_ROW) {
        deps[sqlite3_column_int64(dep, 0)].push_back(sqlite3_column_int64(dep, 1));
    }
    sqlite3_finalize(dep);
    for (auto& t : out) t.dependencies = deps[t.id];
    return out;
}

}  // namespace kos
