#pragma once

#include "config.hpp"
#include "db.hpp"
#include "runtime/task.hpp"

#include "json.hpp"

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <thread>
#include <unordered_map>
#include <vector>

namespace kos {

class Scheduler {
public:
    Scheduler(Store& store, Config cfg);
    ~Scheduler();

    Scheduler(const Scheduler&) = delete;
    Scheduler& operator=(const Scheduler&) = delete;

    int64_t submit_analysis(int64_t dataset_id, bool force);
    void wait(int64_t workflow_id);
    WorkflowProgress progress(int64_t workflow_id);
    nlohmann::json progress_json(int64_t workflow_id);
    int worker_count() const { return workers_; }

private:
    struct Node {
        Task task;
        int remaining = 0;
        std::vector<int64_t> dependents;
    };

    Store& store_;
    Config cfg_;
    int workers_ = 2;
    std::atomic<bool> stop_{false};
    std::mutex mu_;
    std::condition_variable cv_;
    std::condition_variable done_cv_;
    std::queue<int64_t> ready_;
    std::unordered_map<int64_t, Node> nodes_;
    std::unordered_map<int64_t, int> workflow_remaining_;
    std::unordered_map<int64_t, WorkflowStatus> workflow_status_;
    std::unordered_map<int64_t, bool> workflow_force_;
    std::vector<std::thread> pool_;

    void start_pool();
    void worker_loop(int worker_id);
    void execute(Task& task, int worker_id);
    void complete_node(int64_t task_id, bool ok);
    WorkflowProgress snapshot(int64_t workflow_id);
};

void start_analysis_runtime(Store& store, const Config& cfg);
Scheduler& analysis_runtime();

}  // namespace kos
