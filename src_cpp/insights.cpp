#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "analysis.hpp"
#include "config.hpp"
#include "util.hpp"

#include "json.hpp"

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iomanip>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;
using nlohmann::json;

#include "insights_internal.hpp"

namespace kos {

using insights_detail::attach_calculations;
using insights_detail::fallback_insights;
using insights_detail::find_agent_script;
using insights_detail::parse_agent_stdout;
using insights_detail::run_agent;
using insights_detail::slim_graph;

nlohmann::json explain_insights(Store& store, int64_t dataset_id, const std::string& topic,
                                const std::vector<int64_t>& ids) {
    const bool union_view = dataset_id == DATASET_ALL;
    std::optional<Dataset> ds;
    if (!union_view) {
        ds = store.get_dataset(dataset_id);
        if (!ds) throw std::runtime_error("dataset not found");
    }
    json summary = summary_json(store, dataset_id, "documents", topic, ids);
    json graph = slim_graph(graph_json(store, dataset_id, topic, ids));
    auto time_rows = breakdown_rows(store, dataset_id, "time", topic, ids);
    json time;
    time["by"] = "time";
    time["available"] = !time_rows.empty();
    time["rows"] = time_rows;

    json payload;
    if (union_view) {
        payload["datasetId"] = "all";
        payload["name"] = "All sources";
        payload["kind"] = KIND_ALL;
        payload["union"] = true;
    } else {
        payload["datasetId"] = ds->id;
        payload["name"] = ds->name;
        payload["kind"] = ds->kind;
        payload["union"] = false;
    }
    std::string view_topic = trim(topic);
    if (view_topic.empty()) payload["topic"] = nullptr;
    else payload["topic"] = view_topic;
    payload["topicView"] = !view_topic.empty();
    payload["idView"] = !ids.empty();
    payload["ids"] = ids;
    payload["summary"] = summary;
    payload["graph"] = graph;
    payload["time"] = time;
    payload["disclaimer"] =
        "Estimates are not proof of authorship. GraphSAGE is not in the headline share. "
        "No medical judgment. Wikipedia topic is a view of the same collection.";

    json result;
    if (union_view) {
        result["datasetId"] = "all";
        result["name"] = "All sources";
    } else {
        result["datasetId"] = ds->id;
        result["name"] = ds->name;
    }
    if (view_topic.empty()) result["topic"] = nullptr;
    else result["topic"] = view_topic;
    result["topicView"] = !view_topic.empty();
    result["idView"] = !ids.empty();
    result["llmRequired"] = false;

    auto safe_fallback = [&]() {
        try {
            return fallback_insights(payload);
        } catch (...) {
            return std::string(
                "These figures are an ESTIMATE, not proof of authorship. "
                "GraphSAGE is not in this share. No medical judgment is made.");
        }
    };

    fs::path script = find_agent_script();
    std::string agent_out;
    if (!script.empty() && run_agent(script, payload.dump(), agent_out)) {
        json parsed = parse_agent_stdout(agent_out, payload);
        result["text"] = parsed.value("text", safe_fallback());
        result["source"] = parsed.value("source", "agent");
        result["llmRequired"] = false;
        attach_calculations(result, payload);
        return result;
    }
    result["text"] = safe_fallback();
    result["source"] = "fallback";
    attach_calculations(result, payload);
    return result;
}

}  // namespace kos
