#include "runtime/task.hpp"

namespace kos {

TaskType task_type_from_str(const std::string& s) {
    if (s == "STYLOMETRY") return TaskType::STYLOMETRY;
    if (s == "EMBEDDING") return TaskType::EMBEDDING;
    if (s == "CALIBRATE") return TaskType::CALIBRATE;
    if (s == "BUILD_GRAPH") return TaskType::BUILD_GRAPH;
    if (s == "GRAPHSAGE") return TaskType::GRAPHSAGE;
    if (s == "AGGREGATE") return TaskType::AGGREGATE;
    return TaskType::PARSE;
}

TaskStatus task_status_from_str(const std::string& s) {
    if (s == "ready") return TaskStatus::READY;
    if (s == "running") return TaskStatus::RUNNING;
    if (s == "complete") return TaskStatus::COMPLETE;
    if (s == "cached") return TaskStatus::CACHED;
    if (s == "failed") return TaskStatus::FAILED;
    return TaskStatus::PENDING;
}

WorkflowStatus workflow_status_from_str(const std::string& s) {
    if (s == "running") return WorkflowStatus::RUNNING;
    if (s == "complete") return WorkflowStatus::COMPLETE;
    if (s == "failed") return WorkflowStatus::FAILED;
    return WorkflowStatus::PENDING;
}

}  // namespace kos
