#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "analysis.hpp"
#include "util.hpp"

#include "json.hpp"

#include <cstdlib>
#include <filesystem>
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

namespace kos {
namespace {

#ifdef _WIN32
static std::string exe_dir() {
    char buf[MAX_PATH];
    DWORD n = GetModuleFileNameA(nullptr, buf, MAX_PATH);
    if (n == 0) return ".";
    return fs::path(std::string(buf, n)).parent_path().string();
}
#else
static std::string exe_dir() {
    std::error_code ec;
    auto p = fs::read_symlink("/proc/self/exe", ec);
    if (ec) return ".";
    return p.parent_path().string();
}
#endif

static std::string quote_win(const std::string& s) {
    std::string out = "\"";
    for (char c : s) {
        if (c == '"') out += "\\\"";
        else out += c;
    }
    out += '"';
    return out;
}

static fs::path find_agent_script() {
    std::vector<fs::path> cands = {
        fs::path(exe_dir()) / "tools" / "insights" / "agent.py",
        fs::path(exe_dir()) / ".." / "tools" / "insights" / "agent.py",
        fs::current_path() / "tools" / "insights" / "agent.py",
        fs::current_path() / ".." / "tools" / "insights" / "agent.py",
    };
    for (const auto& p : cands) {
        std::error_code ec;
        if (fs::exists(p, ec)) return fs::weakly_canonical(p);
    }
    return {};
}

static std::string fallback_insights(const json& payload) {
    const json& summary = payload.contains("summary") && payload["summary"].is_object()
                              ? payload["summary"]
                              : payload;
    std::ostringstream out;
    std::string name = payload.value("name", summary.value("name", std::string("This collection")));
    std::string topic;
    if (payload.contains("topic") && payload["topic"].is_string()) topic = payload["topic"].get<std::string>();
    else if (summary.contains("topic") && summary["topic"].is_string()) topic = summary["topic"].get<std::string>();
    bool topic_view = payload.value("topicView", summary.value("topicView", false));
    bool id_view = payload.value("idView", summary.value("idView", false));
    int n = summary.value("documentCount", 0);
    int corpus = summary.value("corpusDocumentCount", n);

    out << name;
    if (id_view) {
        out << " / selected graph cluster";
        if (n && corpus && n != corpus) out << " / " << n << " of " << corpus << " documents";
        else if (n) out << " / " << n << " documents";
        out << ". This is a view of the same collection, not a second dataset.";
    } else if (topic_view && !topic.empty()) {
        out << " / topic \"" << topic << "\"";
        if (n && corpus && n != corpus) out << " / " << n << " of " << corpus << " documents in the same collection";
        else if (n) out << " / " << n << " documents";
        out << ". This is a view of the same collection, not a second dataset.";
    } else {
        out << " / " << n << " visible document" << (n == 1 ? "" : "s") << ".";
    }

    out << "\n\n";
    if (summary.contains("shareOfDocuments") || summary.contains("shareOfAnalyzedWords")) {
        out << "Estimated AI-generated share: ";
        bool first = true;
        if (summary.contains("shareOfDocuments") && !summary["shareOfDocuments"].is_null()) {
            out << summary["shareOfDocuments"].get<int>() << "% of documents";
            if (summary.contains("shareOfDocumentsRange") && summary["shareOfDocumentsRange"].is_array() &&
                summary["shareOfDocumentsRange"].size() >= 2) {
                out << " (range " << summary["shareOfDocumentsRange"][0].get<int>() << "-"
                    << summary["shareOfDocumentsRange"][1].get<int>() << "%)";
            }
            first = false;
        }
        if (summary.contains("shareOfAnalyzedWords") && !summary["shareOfAnalyzedWords"].is_null()) {
            if (!first) out << " and ";
            out << summary["shareOfAnalyzedWords"].get<int>() << "% of analyzed words";
            if (summary.contains("shareOfAnalyzedWordsRange") && summary["shareOfAnalyzedWordsRange"].is_array() &&
                summary["shareOfAnalyzedWordsRange"].size() >= 2) {
                out << " (range " << summary["shareOfAnalyzedWordsRange"][0].get<int>() << "-"
                    << summary["shareOfAnalyzedWordsRange"][1].get<int>() << "%)";
            }
        }
        out << ".";
    } else {
        out << summary.value("headline", "This visible set is not yet analyzed.");
    }

    json bands = summary.value("bands", json::object());
    int ai = bands.value(BAND_AI, 0);
    int human = bands.value(BAND_HUMAN, 0);
    int unc = bands.value(BAND_UNC, 0);
    out << "\n\nBands: " << ai << " likely AI-generated, " << human << " likely human-written, " << unc
        << " uncertain.";

    const char* caveat =
        "The time chart bins each document by first-revision year when that date is stored, otherwise the document date. "
        "Combined All sources unions years from every collection. The series runs from 2015 through the latest dated year in the visible set.";
    json time = payload.value("time", json::object());
    json rows = time.value("rows", json::array());
    if (!time.value("available", false) || rows.empty()) {
        out << "\n\nNo dated pages are in this visible set, so a yearly series cannot be drawn. " << caveat;
    } else {
        out << "\n\nThe yearly series covers " << rows.size() << " year-bin" << (rows.size() == 1 ? "" : "s") << ". "
            << caveat;
    }

    json graph = payload.value("graph", json::object());
    std::string group_by = graph.value("groupBy", std::string("topic"));
    std::string group_label = "heading";
    if (group_by == "topic") group_label = "country";
    else if (group_by == "dataset") group_label = "collection";
    else if (group_by == "source") group_label = "collection";
    else if (group_by == "band") group_label = "country";
    int nodes = graph.value("nodeCount", 0);
    int edges = graph.value("edgeCount", 0);
    out << "\n\nThe graph groups " << nodes << " document" << (nodes == 1 ? "" : "s") << " by " << group_label;
    if (edges) out << ", with " << edges << " similarity edge" << (edges == 1 ? "" : "s");
    out << ". A heading labels a cluster of similar pages; clicking it only filters the same collection.";

    out << "\n\nThe collection percent is the mean of document p_ai scores (stylometry + stock-phrase / "
           "uniformity / n-gram detectors + embedding vs the pre-2019 writing centroid in this visible set, "
           "logistic blend). Pages before 2019 stay near zero. GraphSAGE is not in this share.";
    return out.str();
}

#ifdef _WIN32
static bool search_on_path(const char* name, std::string& out) {
    char buf[MAX_PATH];
    DWORD n = SearchPathA(nullptr, name, nullptr, MAX_PATH, buf, nullptr);
    if (!n || n >= MAX_PATH) return false;
    out.assign(buf, n);
    return true;
}

static bool run_process(const std::string& app, const std::string& cmdline, const std::string& input,
                        std::string& output, std::string& err, DWORD timeout_ms) {
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;
    HANDLE in_r = nullptr, in_w = nullptr, out_r = nullptr, out_w = nullptr, err_r = nullptr, err_w = nullptr;
    if (!CreatePipe(&in_r, &in_w, &sa, 0) || !CreatePipe(&out_r, &out_w, &sa, 0) ||
        !CreatePipe(&err_r, &err_w, &sa, 0)) {
        return false;
    }
    SetHandleInformation(in_w, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(out_r, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(err_r, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdInput = in_r;
    si.hStdOutput = out_w;
    si.hStdError = err_w;

    PROCESS_INFORMATION pi{};
    std::string mutable_cmd = cmdline;
    BOOL ok = CreateProcessA(app.empty() ? nullptr : app.c_str(), mutable_cmd.data(), nullptr, nullptr, TRUE,
                             CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    CloseHandle(in_r);
    CloseHandle(out_w);
    CloseHandle(err_w);
    if (!ok) {
        CloseHandle(in_w);
        CloseHandle(out_r);
        CloseHandle(err_r);
        return false;
    }

    DWORD written = 0;
    if (!input.empty()) {
        WriteFile(in_w, input.data(), static_cast<DWORD>(input.size()), &written, nullptr);
    }
    CloseHandle(in_w);

    auto read_all = [](HANDLE h, std::string& dest) {
        char buf[4096];
        DWORD n = 0;
        while (ReadFile(h, buf, sizeof(buf), &n, nullptr) && n > 0) dest.append(buf, n);
    };
    output.clear();
    err.clear();
    read_all(out_r, output);
    read_all(err_r, err);
    CloseHandle(out_r);
    CloseHandle(err_r);

    WaitForSingleObject(pi.hProcess, timeout_ms);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return code == 0;
}
#else
static bool run_process(const std::string& app, const std::vector<std::string>& args, const std::string& input,
                        std::string& output, std::string& err, int timeout_ms) {
    int in_pipe[2], out_pipe[2], err_pipe[2];
    if (pipe(in_pipe) != 0 || pipe(out_pipe) != 0 || pipe(err_pipe) != 0) return false;
    pid_t pid = fork();
    if (pid < 0) return false;
    if (pid == 0) {
        dup2(in_pipe[0], STDIN_FILENO);
        dup2(out_pipe[1], STDOUT_FILENO);
        dup2(err_pipe[1], STDERR_FILENO);
        close(in_pipe[0]);
        close(in_pipe[1]);
        close(out_pipe[0]);
        close(out_pipe[1]);
        close(err_pipe[0]);
        close(err_pipe[1]);
        std::vector<char*> argv;
        argv.push_back(const_cast<char*>(app.c_str()));
        for (const auto& a : args) argv.push_back(const_cast<char*>(a.c_str()));
        argv.push_back(nullptr);
        execvp(app.c_str(), argv.data());
        _exit(127);
    }
    close(in_pipe[0]);
    close(out_pipe[1]);
    close(err_pipe[1]);
    if (!input.empty()) {
        const char* p = input.data();
        size_t left = input.size();
        while (left) {
            ssize_t n = write(in_pipe[1], p, left);
            if (n <= 0) break;
            p += n;
            left -= static_cast<size_t>(n);
        }
    }
    close(in_pipe[1]);
    auto read_all = [](int fd, std::string& dest) {
        char buf[4096];
        ssize_t n;
        while ((n = read(fd, buf, sizeof(buf))) > 0) dest.append(buf, static_cast<size_t>(n));
    };
    output.clear();
    err.clear();
    read_all(out_pipe[0], output);
    read_all(err_pipe[0], err);
    close(out_pipe[0]);
    close(err_pipe[0]);
    int status = 0;
    (void)timeout_ms;
    waitpid(pid, &status, 0);
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}
#endif

static bool run_agent(const fs::path& script, const std::string& input, std::string& output) {
    std::string err;
#ifdef _WIN32
    std::vector<std::pair<std::string, std::string>> cmds;
    std::string py;
    if (search_on_path("python.exe", py)) {
        cmds.push_back({py, quote_win(py) + " " + quote_win(script.string())});
    }
    if (search_on_path("py.exe", py)) {
        cmds.push_back({py, quote_win(py) + " -3 " + quote_win(script.string())});
    }
    if (search_on_path("python3.exe", py)) {
        cmds.push_back({py, quote_win(py) + " " + quote_win(script.string())});
    }
    cmds.push_back({"", "python " + quote_win(script.string())});
    cmds.push_back({"", "py -3 " + quote_win(script.string())});
    for (const auto& [app, cmd] : cmds) {
        std::string out;
        if (run_process(app, cmd, input, out, err, 20000) && !trim(out).empty()) {
            output = out;
            return true;
        }
    }
#else
    std::vector<std::string> apps = {"python3", "python", "py"};
    for (const auto& app : apps) {
        std::string out;
        if (run_process(app, {script.string()}, input, out, err, 20000) && !trim(out).empty()) {
            output = out;
            return true;
        }
    }
#endif
    return false;
}

static json parse_agent_stdout(const std::string& raw, const json& payload) {
    std::string t = trim(raw);
    json out;
    out["llmRequired"] = false;
    if (!t.empty() && t.front() == '{') {
        try {
            auto parsed = json::parse(t);
            if (parsed.is_object() && parsed.contains("text") && parsed["text"].is_string()) {
                out["text"] = parsed["text"].get<std::string>();
                out["source"] = parsed.value("source", "agent");
                out["llmRequired"] = false;
                return out;
            }
        } catch (...) {
        }
    }
    if (!t.empty()) {
        out["text"] = t;
        out["source"] = "agent";
        return out;
    }
    out["text"] = fallback_insights(payload);
    out["source"] = "fallback";
    return out;
}

static json slim_graph(const json& graph) {
    json slim;
    slim["groupBy"] = graph.value("groupBy", "topic");
    slim["groups"] = graph.value("groups", json::array());
    slim["nodeCount"] = graph.contains("nodes") && graph["nodes"].is_array() ? graph["nodes"].size() : 0;
    slim["edgeCount"] = graph.contains("edges") && graph["edges"].is_array() ? graph["edges"].size() : 0;
    return slim;
}

}  // namespace

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
        return result;
    }
    result["text"] = safe_fallback();
    result["source"] = "fallback";
    return result;
}

}  // namespace kos
