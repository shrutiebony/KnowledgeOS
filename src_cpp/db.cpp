#include "db.hpp"

#include "util.hpp"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <string>

#include "sqlite3.h"

namespace kos {

static std::vector<uint8_t> floats_to_blob(const std::vector<float>& v) {
    std::vector<uint8_t> b(v.size() * sizeof(float));
    if (!v.empty()) std::memcpy(b.data(), v.data(), b.size());
    return b;
}

static std::vector<float> blob_to_floats(const void* p, int n) {
    std::vector<float> v;
    if (!p || n <= 0 || n % static_cast<int>(sizeof(float)) != 0) return v;
    v.resize(static_cast<size_t>(n) / sizeof(float));
    std::memcpy(v.data(), p, static_cast<size_t>(n));
    return v;
}

static std::optional<double> opt_double(sqlite3_stmt* st, int col) {
    if (sqlite3_column_type(st, col) == SQLITE_NULL) return std::nullopt;
    return sqlite3_column_double(st, col);
}

static std::string col_text(sqlite3_stmt* st, int col) {
    const unsigned char* p = sqlite3_column_text(st, col);
    return p ? reinterpret_cast<const char*>(p) : "";
}

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
)");
    char* alter_err = nullptr;
    sqlite3_exec(db_, "ALTER TABLE documents ADD COLUMN created_at TEXT;", nullptr, nullptr, &alter_err);
    sqlite3_free(alter_err);
}

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

Document Store::read_document(void* stmt_v, bool include_text) {
    auto* st = static_cast<sqlite3_stmt*>(stmt_v);
    Document d;
    d.id = sqlite3_column_int64(st, 0);
    d.dataset_id = sqlite3_column_int64(st, 1);
    d.title = col_text(st, 2);
    d.url = col_text(st, 3);
    d.source = col_text(st, 4);
    d.topic = col_text(st, 5);
    d.published_at = col_text(st, 6);
    d.created_at = col_text(st, 28);
    d.text = include_text ? col_text(st, 7) : "";
    d.word_count = sqlite3_column_int(st, 8);
    d.embedding = blob_to_floats(sqlite3_column_blob(st, 9), sqlite3_column_bytes(st, 9));
    d.gnn_embedding = blob_to_floats(sqlite3_column_blob(st, 10), sqlite3_column_bytes(st, 10));
    d.type_token_ratio = opt_double(st, 11);
    d.avg_sentence_length = opt_double(st, 12);
    d.sentence_length_std = opt_double(st, 13);
    d.burstiness = opt_double(st, 14);
    d.punctuation_ratio = opt_double(st, 15);
    d.char_entropy = opt_double(st, 16);
    d.repetition_score = opt_double(st, 17);
    d.detector_stylometry = opt_double(st, 18);
    d.detector_repetition = opt_double(st, 19);
    d.detector_uniformity = opt_double(st, 20);
    d.embedding_anomaly = opt_double(st, 21);
    d.stylometry_deviation = opt_double(st, 22);
    d.p_ai = opt_double(st, 23);
    d.ci_low = opt_double(st, 24);
    d.ci_high = opt_double(st, 25);
    d.band = col_text(st, 26);
    d.explanation_json = col_text(st, 27);
    if (d.band.empty()) d.band = BAND_PENDING;
    if (d.title.empty()) d.title = "Untitled";
    return d;
}

static const char* DOC_COLS =
    "id, dataset_id, title, url, source, topic, published_at, text, word_count, embedding, gnn_embedding, "
    "type_token_ratio, avg_sentence_length, sentence_length_std, burstiness, punctuation_ratio, char_entropy, "
    "repetition_score, detector_stylometry, detector_repetition, detector_uniformity, embedding_anomaly, "
    "stylometry_deviation, p_ai, ci_low, ci_high, band, explanation_json, created_at";

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

void Store::delete_dataset(int64_t id) {
    std::lock_guard<std::mutex> lock(mu_);
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(db_, "DELETE FROM datasets WHERE id=?", -1, &st, nullptr);
    sqlite3_bind_int64(st, 1, id);
    sqlite3_step(st);
    sqlite3_finalize(st);
}

static void bind_opt(sqlite3_stmt* st, int i, const std::optional<double>& v) {
    if (v) sqlite3_bind_double(st, i, *v);
    else sqlite3_bind_null(st, i);
}

static void bind_blob_floats(sqlite3_stmt* st, int i, const std::vector<float>& v) {
    if (v.empty()) {
        sqlite3_bind_null(st, i);
        return;
    }
    sqlite3_bind_blob(st, i, v.data(), static_cast<int>(v.size() * sizeof(float)), SQLITE_TRANSIENT);
}

Document Store::insert_document(Document d) {
    std::lock_guard<std::mutex> lock(mu_);
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(db_,
        "INSERT INTO documents(dataset_id,title,url,source,topic,published_at,created_at,text,word_count) "
        "VALUES(?,?,?,?,?,?,?,?,?)",
        -1, &st, nullptr);
    sqlite3_bind_int64(st, 1, d.dataset_id);
    sqlite3_bind_text(st, 2, d.title.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 3, d.url.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 4, d.source.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 5, d.topic.c_str(), -1, SQLITE_TRANSIENT);
    if (d.published_at.empty()) sqlite3_bind_null(st, 6);
    else sqlite3_bind_text(st, 6, d.published_at.c_str(), -1, SQLITE_TRANSIENT);
    if (d.created_at.empty()) sqlite3_bind_null(st, 7);
    else sqlite3_bind_text(st, 7, d.created_at.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 8, d.text.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(st, 9, d.word_count);
    if (sqlite3_step(st) != SQLITE_DONE) {
        sqlite3_finalize(st);
        throw std::runtime_error("insert document failed");
    }
    d.id = sqlite3_last_insert_rowid(db_);
    sqlite3_finalize(st);
    return d;
}

std::optional<Document> Store::get_document(int64_t id, bool include_text) {
    std::lock_guard<std::mutex> lock(mu_);
    sqlite3_stmt* st = nullptr;
    std::string sql = std::string("SELECT ") + DOC_COLS + " FROM documents WHERE id=?";
    sqlite3_prepare_v2(db_, sql.c_str(), -1, &st, nullptr);
    sqlite3_bind_int64(st, 1, id);
    std::optional<Document> out;
    if (sqlite3_step(st) == SQLITE_ROW) out = read_document(st, include_text);
    sqlite3_finalize(st);
    return out;
}

std::vector<Document> Store::docs_by_dataset(int64_t dataset_id, bool include_text) {
    std::lock_guard<std::mutex> lock(mu_);
    sqlite3_stmt* st = nullptr;
    std::string sql = std::string("SELECT ") + DOC_COLS + " FROM documents WHERE dataset_id=? ORDER BY id";
    sqlite3_prepare_v2(db_, sql.c_str(), -1, &st, nullptr);
    sqlite3_bind_int64(st, 1, dataset_id);
    std::vector<Document> out;
    while (sqlite3_step(st) == SQLITE_ROW) out.push_back(read_document(st, include_text));
    sqlite3_finalize(st);
    return out;
}

std::vector<Document> Store::all_docs(bool include_text) {
    std::lock_guard<std::mutex> lock(mu_);
    sqlite3_stmt* st = nullptr;
    std::string sql = std::string("SELECT ") + DOC_COLS +
        " FROM documents WHERE dataset_id IN ("
        "SELECT id FROM datasets WHERE kind != 'WIKIPEDIA_SUBSET') ORDER BY id";
    sqlite3_prepare_v2(db_, sql.c_str(), -1, &st, nullptr);
    std::vector<Document> out;
    while (sqlite3_step(st) == SQLITE_ROW) out.push_back(read_document(st, include_text));
    sqlite3_finalize(st);
    return out;
}

std::vector<Document> Store::docs_by_ids(const std::vector<int64_t>& ids, bool include_text) {
    std::vector<Document> out;
    if (ids.empty()) return out;
    std::lock_guard<std::mutex> lock(mu_);
    const size_t chunk = 200;
    for (size_t off = 0; off < ids.size(); off += chunk) {
        size_t n = std::min(chunk, ids.size() - off);
        std::string sql = std::string("SELECT ") + DOC_COLS + " FROM documents WHERE id IN (";
        for (size_t i = 0; i < n; ++i) {
            if (i) sql += ",";
            sql += "?";
        }
        sql += ")";
        sqlite3_stmt* st = nullptr;
        sqlite3_prepare_v2(db_, sql.c_str(), -1, &st, nullptr);
        for (size_t i = 0; i < n; ++i) sqlite3_bind_int64(st, static_cast<int>(i + 1), ids[off + i]);
        while (sqlite3_step(st) == SQLITE_ROW) out.push_back(read_document(st, include_text));
        sqlite3_finalize(st);
    }
    return out;
}

void Store::save_document(const Document& d) {
    std::lock_guard<std::mutex> lock(mu_);
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(db_,
        "UPDATE documents SET title=?,url=?,source=?,topic=?,published_at=?,created_at=?,text=?,word_count=?,"
        "embedding=?,gnn_embedding=?,type_token_ratio=?,avg_sentence_length=?,sentence_length_std=?,"
        "burstiness=?,punctuation_ratio=?,char_entropy=?,repetition_score=?,detector_stylometry=?,"
        "detector_repetition=?,detector_uniformity=?,embedding_anomaly=?,stylometry_deviation=?,"
        "p_ai=?,ci_low=?,ci_high=?,band=?,explanation_json=? WHERE id=?",
        -1, &st, nullptr);
    sqlite3_bind_text(st, 1, d.title.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 2, d.url.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 3, d.source.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 4, d.topic.c_str(), -1, SQLITE_TRANSIENT);
    if (d.published_at.empty()) sqlite3_bind_null(st, 5);
    else sqlite3_bind_text(st, 5, d.published_at.c_str(), -1, SQLITE_TRANSIENT);
    if (d.created_at.empty()) sqlite3_bind_null(st, 6);
    else sqlite3_bind_text(st, 6, d.created_at.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 7, d.text.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(st, 8, d.word_count);
    bind_blob_floats(st, 9, d.embedding);
    bind_blob_floats(st, 10, d.gnn_embedding);
    bind_opt(st, 11, d.type_token_ratio);
    bind_opt(st, 12, d.avg_sentence_length);
    bind_opt(st, 13, d.sentence_length_std);
    bind_opt(st, 14, d.burstiness);
    bind_opt(st, 15, d.punctuation_ratio);
    bind_opt(st, 16, d.char_entropy);
    bind_opt(st, 17, d.repetition_score);
    bind_opt(st, 18, d.detector_stylometry);
    bind_opt(st, 19, d.detector_repetition);
    bind_opt(st, 20, d.detector_uniformity);
    bind_opt(st, 21, d.embedding_anomaly);
    bind_opt(st, 22, d.stylometry_deviation);
    bind_opt(st, 23, d.p_ai);
    bind_opt(st, 24, d.ci_low);
    bind_opt(st, 25, d.ci_high);
    sqlite3_bind_text(st, 26, d.band.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(st, 27, d.explanation_json.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 28, d.id);
    sqlite3_step(st);
    sqlite3_finalize(st);
}

void Store::save_documents(const std::vector<Document>& docs) {
    for (const auto& d : docs) save_document(d);
}

void Store::delete_documents(int64_t dataset_id) {
    std::lock_guard<std::mutex> lock(mu_);
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(db_, "DELETE FROM documents WHERE dataset_id=?", -1, &st, nullptr);
    sqlite3_bind_int64(st, 1, dataset_id);
    sqlite3_step(st);
    sqlite3_finalize(st);
}

int Store::delete_docs_below_word_count(int64_t dataset_id, int min_words) {
    std::lock_guard<std::mutex> lock(mu_);
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(db_,
        "DELETE FROM edges WHERE dataset_id=? AND ("
        "source_document_id IN (SELECT id FROM documents WHERE dataset_id=? AND word_count < ?) OR "
        "target_document_id IN (SELECT id FROM documents WHERE dataset_id=? AND word_count < ?))",
        -1, &st, nullptr);
    sqlite3_bind_int64(st, 1, dataset_id);
    sqlite3_bind_int64(st, 2, dataset_id);
    sqlite3_bind_int(st, 3, min_words);
    sqlite3_bind_int64(st, 4, dataset_id);
    sqlite3_bind_int(st, 5, min_words);
    sqlite3_step(st);
    sqlite3_finalize(st);
    sqlite3_prepare_v2(db_, "DELETE FROM documents WHERE dataset_id=? AND word_count < ?", -1, &st, nullptr);
    sqlite3_bind_int64(st, 1, dataset_id);
    sqlite3_bind_int(st, 2, min_words);
    sqlite3_step(st);
    int n = sqlite3_changes(db_);
    sqlite3_finalize(st);
    return n;
}

int64_t Store::count_docs(int64_t dataset_id) {
    std::lock_guard<std::mutex> lock(mu_);
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(db_, "SELECT COUNT(*) FROM documents WHERE dataset_id=?", -1, &st, nullptr);
    sqlite3_bind_int64(st, 1, dataset_id);
    int64_t n = 0;
    if (sqlite3_step(st) == SQLITE_ROW) n = sqlite3_column_int64(st, 0);
    sqlite3_finalize(st);
    return n;
}

int64_t Store::count_all_docs() {
    std::lock_guard<std::mutex> lock(mu_);
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(db_,
        "SELECT COUNT(*) FROM documents WHERE dataset_id IN ("
        "SELECT id FROM datasets WHERE kind != 'WIKIPEDIA_SUBSET')",
        -1, &st, nullptr);
    int64_t n = 0;
    if (sqlite3_step(st) == SQLITE_ROW) n = sqlite3_column_int64(st, 0);
    sqlite3_finalize(st);
    return n;
}

int64_t Store::count_unscored(int64_t dataset_id) {
    std::lock_guard<std::mutex> lock(mu_);
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(db_, "SELECT COUNT(*) FROM documents WHERE dataset_id=? AND p_ai IS NULL", -1, &st, nullptr);
    sqlite3_bind_int64(st, 1, dataset_id);
    int64_t n = 0;
    if (sqlite3_step(st) == SQLITE_ROW) n = sqlite3_column_int64(st, 0);
    sqlite3_finalize(st);
    return n;
}

std::vector<std::string> Store::distinct_topics(int64_t dataset_id) {
    std::lock_guard<std::mutex> lock(mu_);
    sqlite3_stmt* st = nullptr;
    const std::string sql =
        "SELECT topic FROM documents WHERE dataset_id=? AND topic IS NOT NULL AND topic!='' "
        "GROUP BY topic HAVING COUNT(*) >= " +
        std::to_string(MIN_TOPIC_DOCUMENTS) + " ORDER BY topic";
    sqlite3_prepare_v2(db_, sql.c_str(), -1, &st, nullptr);
    sqlite3_bind_int64(st, 1, dataset_id);
    std::vector<std::string> out;
    while (sqlite3_step(st) == SQLITE_ROW) out.push_back(col_text(st, 0));
    sqlite3_finalize(st);
    return out;
}

int Store::update_published_at_if_null(int64_t doc_id, const std::string& date) {
    std::lock_guard<std::mutex> lock(mu_);
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(db_, "UPDATE documents SET published_at=? WHERE id=? AND published_at IS NULL", -1, &st, nullptr);
    sqlite3_bind_text(st, 1, date.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 2, doc_id);
    sqlite3_step(st);
    int n = sqlite3_changes(db_);
    sqlite3_finalize(st);
    return n;
}

int Store::update_created_at_if_null(int64_t doc_id, const std::string& date) {
    std::lock_guard<std::mutex> lock(mu_);
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(db_, "UPDATE documents SET created_at=? WHERE id=? AND (created_at IS NULL OR created_at='')",
                       -1, &st, nullptr);
    sqlite3_bind_text(st, 1, date.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 2, doc_id);
    sqlite3_step(st);
    int n = sqlite3_changes(db_);
    sqlite3_finalize(st);
    return n;
}

int Store::update_created_at(int64_t doc_id, const std::string& date) {
    std::lock_guard<std::mutex> lock(mu_);
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(db_, "UPDATE documents SET created_at=? WHERE id=?", -1, &st, nullptr);
    sqlite3_bind_text(st, 1, date.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 2, doc_id);
    sqlite3_step(st);
    int n = sqlite3_changes(db_);
    sqlite3_finalize(st);
    return n;
}

int Store::update_topic(int64_t doc_id, const std::string& topic) {
    std::lock_guard<std::mutex> lock(mu_);
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(db_, "UPDATE documents SET topic=? WHERE id=?", -1, &st, nullptr);
    sqlite3_bind_text(st, 1, topic.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(st, 2, doc_id);
    sqlite3_step(st);
    int n = sqlite3_changes(db_);
    sqlite3_finalize(st);
    return n;
}

void Store::delete_edges(int64_t dataset_id) {
    std::lock_guard<std::mutex> lock(mu_);
    sqlite3_stmt* st = nullptr;
    sqlite3_prepare_v2(db_, "DELETE FROM edges WHERE dataset_id=?", -1, &st, nullptr);
    sqlite3_bind_int64(st, 1, dataset_id);
    sqlite3_step(st);
    sqlite3_finalize(st);
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
    delete_edges(dataset_id);
    delete_documents(dataset_id);
}

}  // namespace kos
