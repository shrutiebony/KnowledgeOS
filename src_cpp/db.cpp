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

Store::~Store() {
    if (db_) sqlite3_close(db_);
}

void Store::exec(const char* sql) {
    char* err = nullptr;
    if (sqlite3_exec(db_, sql, nullptr, nullptr, &err) != SQLITE_OK) {
        std::string m = err ? err : "sqlite error";
        sqlite3_free(err);
        throw std::runtime_error(m);
    }
}

void Store::open(const std::string& path) {
    std::filesystem::path p(path);
    if (p.has_parent_path()) {
        std::filesystem::create_directories(p.parent_path());
    }
    if (sqlite3_open(path.c_str(), &db_) != SQLITE_OK) {
        throw std::runtime_error(std::string("sqlite open failed: ") + sqlite3_errmsg(db_));
    }
    exec("PRAGMA journal_mode=WAL;");
    exec("PRAGMA foreign_keys=ON;");
    exec("PRAGMA busy_timeout=5000;");
    exec(R"(
CREATE TABLE IF NOT EXISTS datasets (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  name TEXT NOT NULL,
  kind TEXT NOT NULL,
  parent_topic TEXT,
  parent_dataset_id INTEGER,
  graph_sage_status TEXT DEFAULT 'NOT_TRAINED',
  graph_sage_message TEXT,
  created_at TEXT,
  last_analyzed_at TEXT,
  analysis_state TEXT DEFAULT 'idle'
);
CREATE TABLE IF NOT EXISTS documents (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  dataset_id INTEGER NOT NULL,
  title TEXT NOT NULL,
  url TEXT,
  source TEXT,
  topic TEXT,
  published_at TEXT,
  created_at TEXT,
  text TEXT NOT NULL,
  word_count INTEGER DEFAULT 0,
  embedding BLOB,
  gnn_embedding BLOB,
  type_token_ratio REAL,
  avg_sentence_length REAL,
  sentence_length_std REAL,
  burstiness REAL,
  punctuation_ratio REAL,
  char_entropy REAL,
  repetition_score REAL,
  detector_stylometry REAL,
  detector_repetition REAL,
  detector_uniformity REAL,
  embedding_anomaly REAL,
  stylometry_deviation REAL,
  p_ai REAL,
  ci_low REAL,
  ci_high REAL,
  band TEXT DEFAULT 'PENDING',
  explanation_json TEXT
);
CREATE TABLE IF NOT EXISTS edges (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  dataset_id INTEGER NOT NULL,
  source_document_id INTEGER NOT NULL,
  target_document_id INTEGER NOT NULL,
  cosine REAL,
  reason TEXT
);
CREATE INDEX IF NOT EXISTS idx_docs_dataset ON documents(dataset_id);
CREATE INDEX IF NOT EXISTS idx_edges_dataset ON edges(dataset_id);
CREATE INDEX IF NOT EXISTS idx_edges_src ON edges(dataset_id, source_document_id);
CREATE TABLE IF NOT EXISTS workflows (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  dataset_id INTEGER NOT NULL,
  status TEXT NOT NULL DEFAULT 'pending',
  worker_count INTEGER DEFAULT 0,
  chunk_size INTEGER DEFAULT 32,
  document_count INTEGER DEFAULT 0,
  created_at TEXT,
  started_at TEXT,
  finished_at TEXT,
  error TEXT
);
CREATE TABLE IF NOT EXISTS tasks (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  workflow_id INTEGER NOT NULL,
  dataset_id INTEGER NOT NULL,
  task_type TEXT NOT NULL,
  status TEXT NOT NULL DEFAULT 'pending',
  chunk_start INTEGER DEFAULT 0,
  chunk_end INTEGER DEFAULT 0,
  fingerprint TEXT,
  error TEXT,
  created_at TEXT,
  started_at TEXT,
  finished_at TEXT,
  worker_id INTEGER DEFAULT -1,
  bytes_moved INTEGER DEFAULT 0,
  duration_ms REAL DEFAULT 0
);
CREATE TABLE IF NOT EXISTS task_dependencies (
  task_id INTEGER NOT NULL,
  depends_on_task_id INTEGER NOT NULL
);
CREATE INDEX IF NOT EXISTS idx_tasks_workflow ON tasks(workflow_id);
CREATE INDEX IF NOT EXISTS idx_task_deps ON task_dependencies(task_id);
)");
    char* alter_err = nullptr;
    sqlite3_exec(db_, "ALTER TABLE documents ADD COLUMN created_at TEXT;", nullptr, nullptr, &alter_err);
    sqlite3_free(alter_err);
    fail_interrupted_workflows();
}

}  // namespace kos
