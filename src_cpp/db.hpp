#pragma once

#include "models.hpp"

#include <mutex>
#include <optional>
#include <string>
#include <vector>

struct sqlite3;

namespace kos {

class Store {
public:
    Store() = default;
    ~Store();
    Store(const Store&) = delete;
    Store& operator=(const Store&) = delete;

    void open(const std::string& path);

    Dataset insert_dataset(Dataset d);
    std::optional<Dataset> get_dataset(int64_t id);
    std::optional<Dataset> find_by_name_ignore_case(const std::string& name);
    std::optional<Dataset> find_first_by_kind(const std::string& kind);
    std::optional<Dataset> find_by_kind_and_name(const std::string& kind, const std::string& name);
    std::vector<Dataset> all_datasets();
    std::vector<Dataset> datasets_by_kind_and_name(const std::string& kind, const std::string& name);
    void save_dataset(const Dataset& d);
    void delete_dataset(int64_t id);

    Document insert_document(Document d);
    std::optional<Document> get_document(int64_t id, bool include_text = true);
    std::vector<Document> docs_by_dataset(int64_t dataset_id, bool include_text);
    std::vector<Document> all_docs(bool include_text);
    std::vector<Document> docs_by_ids(const std::vector<int64_t>& ids, bool include_text);
    void save_document(const Document& d);
    void save_documents(const std::vector<Document>& docs);
    void delete_documents(int64_t dataset_id);
    int delete_docs_below_word_count(int64_t dataset_id, int min_words);
    int64_t count_docs(int64_t dataset_id);
    int64_t count_all_docs();
    int64_t count_unscored(int64_t dataset_id);
    std::vector<std::string> distinct_topics(int64_t dataset_id);
    int update_published_at_if_null(int64_t doc_id, const std::string& date);
    int update_created_at_if_null(int64_t doc_id, const std::string& date);
    int update_topic(int64_t doc_id, const std::string& topic);

    void delete_edges(int64_t dataset_id);
    void insert_edges(const std::vector<DocumentEdge>& edges);
    std::vector<DocumentEdge> edges_by_dataset(int64_t dataset_id);
    std::vector<DocumentEdge> edges_from(int64_t dataset_id, int64_t source_id);

    void clear_dataset_contents(int64_t dataset_id);

    std::mutex& mutex() { return mu_; }

private:
    sqlite3* db_ = nullptr;
    std::mutex mu_;
    void exec(const char* sql);
    Dataset read_dataset(void* stmt);
    Document read_document(void* stmt, bool include_text);
};

}  // namespace kos
