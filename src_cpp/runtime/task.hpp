#pragma once

#include "json.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace kos {

enum class TaskType {
    PARSE,
    STYLOMETRY,
    EMBEDDING,
    CALIBRATE,
    BUILD_GRAPH,
    GRAPHSAGE,
    AGGREGATE
};

enum class TaskStatus { PENDING, READY, RUNNING, COMPLETE, CACHED, FAILED };

enum class WorkflowStatus { PENDING, RUNNING, COMPLETE, FAILED };

inline const char* task_type_str(TaskType t) {
    switch (t) {
        case TaskType::PARSE: return "PARSE";
        case TaskType::STYLOMETRY: return "STYLOMETRY";
        case TaskType::EMBEDDING: return "EMBEDDING";
        case TaskType::CALIBRATE: return "CALIBRATE";
        case TaskType::BUILD_GRAPH: return "BUILD_GRAPH";
        case TaskType::GRAPHSAGE: return "GRAPHSAGE";
        case TaskType::AGGREGATE: return "AGGREGATE";
    }
    return "UNKNOWN";
}

inline const char* task_status_str(TaskStatus s) {
    switch (s) {
        case TaskStatus::PENDING: return "pending";
        case TaskStatus::READY: return "ready";
        case TaskStatus::RUNNING: return "running";
        case TaskStatus::COMPLETE: return "complete";
        case TaskStatus::CACHED: return "cached";
        case TaskStatus::FAILED: return "failed";
    }
    return "unknown";
}

inline const char* workflow_status_str(WorkflowStatus s) {
    switch (s) {
        case WorkflowStatus::PENDING: return "pending";
        case WorkflowStatus::RUNNING: return "running";
        case WorkflowStatus::COMPLETE: return "complete";
        case WorkflowStatus::FAILED: return "failed";
    }
    return "unknown";
}

TaskType task_type_from_str(const std::string& s);
TaskStatus task_status_from_str(const std::string& s);
WorkflowStatus workflow_status_from_str(const std::string& s);

struct Task {
    int64_t id = 0;
    int64_t workflow_id = 0;
    int64_t dataset_id = 0;
    TaskType type = TaskType::PARSE;
    TaskStatus status = TaskStatus::PENDING;
    int chunk_start = 0;
    int chunk_end = 0;
    std::vector<int64_t> dependencies;
    std::string fingerprint;
    std::string error;
    std::string created_at;
    std::string started_at;
    std::string finished_at;
    int worker_id = -1;
    int64_t bytes_moved = 0;
    double duration_ms = 0;
};

struct Workflow {
    int64_t id = 0;
    int64_t dataset_id = 0;
    WorkflowStatus status = WorkflowStatus::PENDING;
    int worker_count = 0;
    int chunk_size = 0;
    int document_count = 0;
    std::string created_at;
    std::string started_at;
    std::string finished_at;
    std::string error;
};

struct WorkflowProgress {
    int64_t workflow_id = 0;
    int64_t dataset_id = 0;
    std::string status;
    int total = 0;
    int completed = 0;
    int cached = 0;
    int running = 0;
    int pending = 0;
    int failed = 0;
    double progress = 0;
    int worker_count = 0;
    int chunk_size = 0;
    int document_count = 0;
    double total_ms = 0;
    int64_t bytes_moved = 0;
    nlohmann::json by_type = nlohmann::json::object();
};

}  // namespace kos
