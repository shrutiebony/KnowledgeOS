#include "runtime/scheduler.hpp"

#include "analysis.hpp"
#include "util.hpp"

#include <algorithm>
#include <chrono>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace kos {

static Scheduler* g_runtime = nullptr;

static int resolve_workers(const Config& cfg) {
    int n = cfg.worker_threads;
    if (n <= 0) n = static_cast<int>(std::thread::hardware_concurrency());
    if (n <= 0) n = 2;
    if (n > 16) n = 16;
    if (n < 2) n = 2;
    return n;
}

static std::string fingerprint_of(TaskType type, int64_t dataset_id, int start, int end, int embed_dim) {
    std::ostringstream os;
    os << task_type_str(type) << '|' << dataset_id << '|' << start << '-' << end << '|' << embed_dim;
    return os.str();
}

Scheduler::Scheduler(Store& store, Config cfg) : store_(store), cfg_(std::move(cfg)) {
    workers_ = resolve_workers(cfg_);
    start_pool();
}

Scheduler::~Scheduler() {
    stop_ = true;
    cv_.notify_all();
    for (auto& t : pool_) {
        if (t.joinable()) t.join();
    }
}

void Scheduler::start_pool() {
    pool_.reserve(static_cast<size_t>(workers_));
    for (int i = 0; i < workers_; ++i) {
        pool_.emplace_back([this, i] { worker_loop(i); });
    }
}

int64_t Scheduler::submit_analysis(int64_t dataset_id, bool force) {
    auto docs = store_.docs_by_dataset(dataset_id, false);
    const int n = static_cast<int>(docs.size());
    int chunk = cfg_.task_chunk_size > 0 ? cfg_.task_chunk_size : 32;
    if (chunk < 8) chunk = 8;

    Workflow wf;
    wf.dataset_id = dataset_id;
    wf.status = WorkflowStatus::RUNNING;
    wf.worker_count = workers_;
    wf.chunk_size = chunk;
    wf.document_count = n;
    wf.created_at = now_iso();
    wf.started_at = wf.created_at;
    wf.id = store_.insert_workflow(wf);

    auto add = [&](TaskType type, int a, int b, const std::vector<int64_t>& deps) {
        Task t;
        t.workflow_id = wf.id;
        t.dataset_id = dataset_id;
        t.type = type;
        t.status = TaskStatus::PENDING;
        t.chunk_start = a;
        t.chunk_end = b;
        t.dependencies = deps;
        t.fingerprint = fingerprint_of(type, dataset_id, a, b, cfg_.embed_dim);
        t.created_at = now_iso();
        t.id = store_.insert_task(t);
        for (int64_t d : deps) store_.insert_task_dependency(t.id, d);
        return t;
    };

    Task parse = add(TaskType::PARSE, 0, n, {});
    std::vector<int64_t> feature_ids;
    if (n == 0) {
        add(TaskType::AGGREGATE, 0, 0, {parse.id});
    } else {
        std::vector<int64_t> feature_ids;
        for (int i = 0; i < n; i += chunk) {
            int j = std::min(n, i + chunk);
            int64_t sty = add(TaskType::STYLOMETRY, i, j, {parse.id}).id;
            feature_ids.push_back(add(TaskType::EMBEDDING, i, j, {sty}).id);
        }
        int64_t cal = add(TaskType::CALIBRATE, 0, n, feature_ids).id;
        int64_t graph = add(TaskType::BUILD_GRAPH, 0, n, {cal}).id;
        int64_t gs = add(TaskType::GRAPHSAGE, 0, n, {graph}).id;
        add(TaskType::AGGREGATE, 0, n, {gs});
        (void)force;
    }

    auto persisted = store_.tasks_by_workflow(wf.id);
    {
        std::lock_guard<std::mutex> lock(mu_);
        workflow_remaining_[wf.id] = static_cast<int>(persisted.size());
        workflow_status_[wf.id] = WorkflowStatus::RUNNING;
        workflow_force_[wf.id] = force;
        for (auto& t : persisted) {
            Node node;
            node.task = t;
            node.remaining = static_cast<int>(t.dependencies.size());
            nodes_[t.id] = node;
        }
        for (auto& t : persisted) {
            for (int64_t d : t.dependencies) {
                auto it = nodes_.find(d);
                if (it != nodes_.end()) it->second.dependents.push_back(t.id);
            }
        }
        for (auto& [id, node] : nodes_) {
            if (node.task.workflow_id != wf.id) continue;
            if (node.remaining == 0) {
                node.task.status = TaskStatus::READY;
                ready_.push(id);
            }
        }
    }
    cv_.notify_all();
    return wf.id;
}

void Scheduler::wait(int64_t workflow_id) {
    std::unique_lock<std::mutex> lock(mu_);
    done_cv_.wait(lock, [&] {
        auto it = workflow_status_.find(workflow_id);
        return it != workflow_status_.end() &&
               (it->second == WorkflowStatus::COMPLETE || it->second == WorkflowStatus::FAILED);
    });
}

WorkflowProgress Scheduler::progress(int64_t workflow_id) {
    return snapshot(workflow_id);
}

nlohmann::json Scheduler::progress_json(int64_t workflow_id) {
    auto p = snapshot(workflow_id);
    return {
        {"workflow_id", p.workflow_id},
        {"dataset_id", p.dataset_id},
        {"status", p.status},
        {"total_tasks", p.total},
        {"completed", p.completed},
        {"cached", p.cached},
        {"running", p.running},
        {"pending", p.pending},
        {"failed", p.failed},
        {"progress", p.progress},
        {"parallel_workers", p.worker_count},
        {"chunk_size", p.chunk_size},
        {"documents", p.document_count},
        {"total_runtime_ms", p.total_ms},
        {"bytes_moved", p.bytes_moved},
        {"by_type", p.by_type}
    };
}

WorkflowProgress Scheduler::snapshot(int64_t workflow_id) {
    WorkflowProgress p;
    p.workflow_id = workflow_id;
    auto wf = store_.get_workflow(workflow_id);
    auto tasks = store_.tasks_by_workflow(workflow_id);
    if (wf) {
        p.dataset_id = wf->dataset_id;
        p.status = workflow_status_str(wf->status);
        p.worker_count = wf->worker_count;
        p.chunk_size = wf->chunk_size;
        p.document_count = wf->document_count;
    }
    p.total = static_cast<int>(tasks.size());
    std::unordered_map<std::string, double> type_ms;
    std::unordered_map<std::string, int> type_n;
    for (const auto& t : tasks) {
        if (t.status == TaskStatus::COMPLETE) p.completed++;
        else if (t.status == TaskStatus::CACHED) { p.completed++; p.cached++; }
        else if (t.status == TaskStatus::RUNNING) p.running++;
        else if (t.status == TaskStatus::FAILED) p.failed++;
        else p.pending++;
        p.total_ms += t.duration_ms;
        p.bytes_moved += t.bytes_moved;
        std::string k = task_type_str(t.type);
        type_ms[k] += t.duration_ms;
        type_n[k] += 1;
    }
    if (p.total) p.progress = 100.0 * static_cast<double>(p.completed) / static_cast<double>(p.total);
    for (const auto& [k, ms] : type_ms) {
        p.by_type[k] = {{"ms", ms}, {"tasks", type_n[k]}};
    }
    return p;
}

void Scheduler::worker_loop(int worker_id) {
    while (!stop_) {
        int64_t id = 0;
        {
            std::unique_lock<std::mutex> lock(mu_);
            cv_.wait(lock, [&] { return stop_ || !ready_.empty(); });
            if (stop_) return;
            id = ready_.front();
            ready_.pop();
            auto it = nodes_.find(id);
            if (it == nodes_.end()) continue;
            it->second.task.status = TaskStatus::RUNNING;
            it->second.task.worker_id = worker_id;
            it->second.task.started_at = now_iso();
        }
        Task task;
        {
            std::lock_guard<std::mutex> lock(mu_);
            task = nodes_[id].task;
        }
        store_.update_task(task);
        bool ok = true;
        try {
            execute(task, worker_id);
        } catch (const std::exception& e) {
            ok = false;
            task.error = e.what();
        }
        task.finished_at = now_iso();
        if (ok && task.status != TaskStatus::CACHED) task.status = TaskStatus::COMPLETE;
        if (!ok) task.status = TaskStatus::FAILED;
        store_.update_task(task);
        {
            std::lock_guard<std::mutex> lock(mu_);
            nodes_[id].task = task;
        }
        complete_node(id, ok);
    }
}

void Scheduler::complete_node(int64_t task_id, bool ok) {
    int64_t wf = 0;
    bool wf_done = false;
    WorkflowStatus wf_status = WorkflowStatus::COMPLETE;
    {
        std::lock_guard<std::mutex> lock(mu_);
        auto it = nodes_.find(task_id);
        if (it == nodes_.end()) return;
        wf = it->second.task.workflow_id;
        if (!ok) {
            workflow_status_[wf] = WorkflowStatus::FAILED;
            wf_done = true;
            wf_status = WorkflowStatus::FAILED;
        } else {
            for (int64_t dep : it->second.dependents) {
                auto dit = nodes_.find(dep);
                if (dit == nodes_.end()) continue;
                dit->second.remaining--;
                if (dit->second.remaining <= 0 &&
                    dit->second.task.status != TaskStatus::COMPLETE &&
                    dit->second.task.status != TaskStatus::CACHED &&
                    dit->second.task.status != TaskStatus::FAILED) {
                    dit->second.task.status = TaskStatus::READY;
                    ready_.push(dep);
                }
            }
            auto rem = workflow_remaining_.find(wf);
            if (rem != workflow_remaining_.end()) {
                rem->second--;
                if (rem->second <= 0) {
                    workflow_status_[wf] = WorkflowStatus::COMPLETE;
                    wf_done = true;
                    wf_status = WorkflowStatus::COMPLETE;
                }
            }
        }
        cv_.notify_all();
        if (wf_done) done_cv_.notify_all();
    }
    if (wf_done) {
        auto wfrow = store_.get_workflow(wf);
        if (wfrow) {
            wfrow->status = wf_status;
            wfrow->finished_at = now_iso();
            if (wf_status == WorkflowStatus::FAILED) wfrow->error = "task failed";
            store_.update_workflow(*wfrow);
        }
    }
}

void Scheduler::execute(Task& task, int worker_id) {
    using clock = std::chrono::steady_clock;
    auto t0 = clock::now();
    bool force = false;
    {
        std::lock_guard<std::mutex> lock(mu_);
        auto it = workflow_force_.find(task.workflow_id);
        force = it != workflow_force_.end() && it->second;
    }

    auto slice = [&](std::vector<Document>& docs) {
        int a = std::max(0, task.chunk_start);
        int b = std::min(static_cast<int>(docs.size()), task.chunk_end);
        if (b < a) b = a;
        return std::pair<int, int>{a, b};
    };

    switch (task.type) {
        case TaskType::PARSE: {
            auto n = store_.count_docs(task.dataset_id);
            if (n <= 0 && task.chunk_end > 0) throw std::runtime_error("no documents to parse");
            break;
        }
        case TaskType::STYLOMETRY: {
            auto docs = store_.docs_by_dataset(task.dataset_id, true);
            auto [a, b] = slice(docs);
            bool all_cached = !docs.empty();
            int64_t bytes = 0;
            for (int i = a; i < b; ++i) {
                bytes += static_cast<int64_t>(docs[i].text.size());
                if (!docs[i].type_token_ratio) all_cached = false;
            }
            if (!force && all_cached && a < b) {
                task.status = TaskStatus::CACHED;
                task.bytes_moved = 0;
            } else {
                std::vector<Document> changed;
                for (int i = a; i < b; ++i) {
                    fill_stylometry_only(docs[i]);
                    changed.push_back(docs[i]);
                }
                if (!changed.empty()) store_.save_documents(changed);
                task.bytes_moved = bytes;
            }
            break;
        }
        case TaskType::EMBEDDING: {
            auto docs = store_.docs_by_dataset(task.dataset_id, true);
            auto [a, b] = slice(docs);
            bool all_cached = a < b;
            int64_t bytes = 0;
            for (int i = a; i < b; ++i) {
                bytes += static_cast<int64_t>(docs[i].text.size());
                if (docs[i].embedding.empty()) all_cached = false;
            }
            if (!force && all_cached) {
                task.status = TaskStatus::CACHED;
                task.bytes_moved = 0;
            } else {
                std::vector<Document> changed;
                for (int i = a; i < b; ++i) {
                    fill_embedding_only(docs[i], cfg_);
                    bytes += static_cast<int64_t>(docs[i].embedding.size() * sizeof(float));
                    changed.push_back(docs[i]);
                }
                if (!changed.empty()) store_.save_documents(changed);
                task.bytes_moved = bytes;
            }
            break;
        }
        case TaskType::CALIBRATE: {
            auto docs = store_.docs_by_dataset(task.dataset_id, true);
            calibrate_collection(docs, cfg_);
            store_.save_documents(docs);
            task.bytes_moved = static_cast<int64_t>(docs.size() * 64);
            break;
        }
        case TaskType::BUILD_GRAPH: {
            auto docs = store_.docs_by_dataset(task.dataset_id, false);
            rebuild_knn_graph(store_, task.dataset_id, docs, cfg_);
            task.bytes_moved = static_cast<int64_t>(docs.size() * cfg_.embed_dim * sizeof(float));
            break;
        }
        case TaskType::GRAPHSAGE: {
            auto ds = store_.get_dataset(task.dataset_id);
            if (!ds) throw std::runtime_error("dataset not found");
            mean_aggregate_graphsage(store_, *ds);
            break;
        }
        case TaskType::AGGREGATE: {
            auto ds = store_.get_dataset(task.dataset_id);
            if (!ds) throw std::runtime_error("dataset not found");
            if (store_.count_docs(task.dataset_id) == 0) ds->analysis_state = "empty";
            else {
                ds->analysis_state = "ready";
                ds->last_analyzed_at = now_iso();
            }
            store_.save_dataset(*ds);
            break;
        }
    }
    auto t1 = clock::now();
    task.duration_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
}

void start_analysis_runtime(Store& store, const Config& cfg) {
    if (!g_runtime) g_runtime = new Scheduler(store, cfg);
}

Scheduler& analysis_runtime() {
    if (!g_runtime) throw std::runtime_error("analysis runtime not started");
    return *g_runtime;
}

}  // namespace kos
