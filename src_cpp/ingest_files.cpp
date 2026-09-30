#include "ingest.hpp"
#include "analysis.hpp"
#include "gdelt_crawl.hpp"
#include "html.hpp"
#include "pdf.hpp"
#include "util.hpp"
#include "wiki_crawl.hpp"

#include "miniz.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <iostream>
#include <map>
#include <optional>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <unordered_set>

namespace kos {

static const std::regex COUNT_SUFFIX(R"(\s*\(\d+\)\s*$)");

static std::string sanitize_display_name(std::string raw) {
    raw = trim(raw);
    raw = std::regex_replace(raw, COUNT_SUFFIX, "");
    raw = trim(raw);
    std::string out;
    bool space = false;
    for (char c : raw) {
        if (std::isspace(static_cast<unsigned char>(c))) {
            if (!space) {
                out.push_back(' ');
                space = true;
            }
        } else {
            out.push_back(c);
            space = false;
        }
    }
    return out;
}

static std::string sanitize_token(std::string raw) {
    raw = std::regex_replace(trim(raw), COUNT_SUFFIX, "");
    raw = ascii_lower(raw);
    std::string out;
    for (char c : raw) {
        if (std::isalnum(static_cast<unsigned char>(c))) out.push_back(c);
        else out.push_back('-');
    }
    while (!out.empty() && out.front() == '-') out.erase(out.begin());
    while (!out.empty() && out.back() == '-') out.pop_back();
    std::string collapsed;
    bool dash = false;
    for (char c : out) {
        if (c == '-') {
            if (!dash) collapsed.push_back('-');
            dash = true;
        } else {
            collapsed.push_back(c);
            dash = false;
        }
    }
    if (collapsed.empty()) return "";
    return collapsed.size() <= 40 ? collapsed : collapsed.substr(0, 40);
}

static std::string strip_ext(std::string name) {
    auto dot = name.find_last_of('.');
    return (dot != std::string::npos && dot > 0) ? name.substr(0, dot) : name;
}

static std::string file_base_name(std::string filename) {
    if (is_blank(filename)) return "";
    for (char& c : filename) if (c == '\\') c = '/';
    auto slash = filename.find_last_of('/');
    if (slash != std::string::npos) filename = filename.substr(slash + 1);
    return sanitize_token(strip_ext(filename));
}

static std::string short_name_from_host(std::string host) {
    if (is_blank(host)) return "url";
    host = ascii_lower(trim(host));
    if (host.rfind("www.", 0) == 0) host = host.substr(4);
    if (!host.empty() && host.back() == '.') host.pop_back();
    auto dot = host.find('.');
    std::string first = dot == std::string::npos ? host : host.substr(0, dot);
    static const char* suffixes[] = {"project", "projects", "site", "online", "web"};
    for (const char* s : suffixes) {
        std::string suf = s;
        if (first.size() > suf.size() + 2 && first.size() >= suf.size() &&
            first.compare(first.size() - suf.size(), suf.size(), suf) == 0) {
            first = first.substr(0, first.size() - suf.size());
            break;
        }
    }
    auto cleaned = sanitize_token(first);
    return cleaned.empty() ? "url" : cleaned;
}

static std::string first_path_slug(const std::string& url) {
    auto n = normalize_url(url);
    if (!n) return "";
    auto path = url_path(*n);
    if (!path || *path == "/") return "";
    static const std::unordered_set<std::string> boring = {"index", "home", "default", "www", "html", "page", "pages", "wiki"};
    std::string p = *path;
    size_t i = 0;
    while (i < p.size()) {
        if (p[i] == '/') {
            ++i;
            continue;
        }
        size_t j = i;
        while (j < p.size() && p[j] != '/') ++j;
        auto token = sanitize_token(strip_ext(p.substr(i, j - i)));
        if (!token.empty() && !boring.count(token)) return token;
        i = j;
    }
    return "";
}

static std::string name_from_seed_url(const std::string& seed_url) {
    auto host = comparable_host(seed_url).value_or("");
    auto host_short = short_name_from_host(host);
    auto slug = first_path_slug(seed_url);
    if (!slug.empty() && slug != host_short) return host_short + "-" + slug;
    return host_short;
}

static bool available_name(Store& store, const std::string& name) {
    return !store.find_by_name_ignore_case(name);
}

static std::string unique_name(Store& store, std::string preferred, const std::string& kind) {
    if (is_blank(preferred)) preferred = (kind == KIND_URLS ? "url" : "upload");
    if (available_name(store, preferred)) return preferred;
    for (int n = 2; n <= 1000; ++n) {
        std::string cand = preferred + "-" + std::to_string(n);
        if (available_name(store, cand)) return cand;
    }
    return preferred + "-" + now_iso();
}

static std::string upload_collection_name(const std::string& requested, const std::vector<std::string>& filenames) {
    auto given = sanitize_display_name(requested);
    if (!given.empty()) return given;
    std::vector<std::string> bases;
    for (const auto& fn : filenames) {
        auto base = file_base_name(fn);
        if (!base.empty() && std::find(bases.begin(), bases.end(), base) == bases.end()) bases.push_back(base);
    }
    if (bases.size() == 1) return bases[0];
    if (bases.size() >= 2) return bases[0] + "-" + bases[1];
    return "upload";
}

static std::string url_collection_name(const std::string& requested, const UrlCrawlResult& crawled) {
    auto given = sanitize_display_name(requested);
    if (!given.empty()) return given;
    for (const auto& p : crawled.pages) {
        if (!is_blank(p.seed_url)) return name_from_seed_url(p.seed_url);
    }
    for (const auto& p : crawled.pages) {
        if (!is_blank(p.url)) return name_from_seed_url(p.url);
    }
    return "url";
}

static std::string source_for_seed(const CrawledPage& page) {
    if (!is_blank(page.seed_url)) return clip(page.seed_url, 2000);
    if (!is_blank(page.host)) return page.host;
    return host_of(page.url).value_or("url");
}

Dataset create_dataset(Store& store, const std::string& name, const std::string& kind) {
    Dataset d;
    d.name = name;
    d.kind = kind;
    d.created_at = now_iso();
    d.analysis_state = "ingested";
    d.graph_sage_status = GS_NOT_TRAINED;
    d.graph_sage_message = GS_DEFAULT_MSG;
    return store.insert_dataset(d);
}

Document add_document(Store& store, const Dataset& dataset, std::string title, std::string text,
                      const std::string& url, const std::string& source, const std::string& topic,
                      const std::string& published_at, const std::string& created_at) {
    text = trim(text);
    if (is_blank(text)) throw std::invalid_argument("Document has no extractable text: " + title);
    Document doc;
    doc.dataset_id = dataset.id;
    doc.title = is_blank(title) ? "Untitled" : title;
    doc.text = text;
    doc.url = clip(url, 2000);
    doc.source = source;
    bool wiki = dataset.kind == KIND_WIKI || iequals(source, WIKI_SOURCE);
    if (wiki) {
        doc.topic = canonical_wiki_topic(topic, doc.title);
    } else {
        std::string shared = canonical_wiki_topic(topic, topic);
        doc.topic = (is_shared_country_topic(shared) || is_shared_india_topic(shared)) ? shared : topic;
    }
    doc.published_at = published_at;
    doc.created_at = created_at;
    doc.word_count = stylometry_analyze(text).word_count;
    return store.insert_document(doc);
}

static std::string extract_json_field(const std::string& line, const std::string& key) {
    std::string needle = "\"" + key + "\"";
    auto i = line.find(needle);
    if (i == std::string::npos) return "";
    auto colon = line.find(':', i);
    auto q1 = line.find('"', colon + 1);
    if (q1 == std::string::npos) return "";
    std::string sb;
    for (size_t p = q1 + 1; p < line.size(); ++p) {
        char c = line[p];
        if (c == '\\' && p + 1 < line.size()) {
            sb.push_back(line[p + 1]);
            ++p;
            continue;
        }
        if (c == '"') break;
        sb.push_back(c);
    }
    return sb;
}

static void ingest_jsonl(Store& store, const Dataset& dataset, const std::string& bytes, const std::string& source) {
    std::string line;
    std::istringstream in(bytes);
    while (std::getline(in, line)) {
        if (is_blank(line)) continue;
        std::string title = extract_json_field(line, "title");
        std::string text = extract_json_field(line, "text");
        std::string url = extract_json_field(line, "url");
        std::string topic = extract_json_field(line, "topic");
        std::string src = extract_json_field(line, "source");
        if (is_blank(text)) continue;
        add_document(store, dataset, is_blank(title) ? "Untitled" : title, text, url,
                     is_blank(src) ? source : src, is_blank(topic) ? guess_topic(title + " " + text) : topic, "");
    }
}

static void ingest_one(Store& store, const Dataset& dataset, std::string filename, const std::string& bytes,
                       const std::string& content_type) {
    std::string lower = ascii_lower(filename);
    std::string title = filename;
    for (char& c : title) if (c == '\\') c = '/';
    auto slash = title.find_last_of('/');
    if (slash != std::string::npos) title = title.substr(slash + 1);
    std::string text;
    if (lower.size() >= 4 && (lower.rfind(".pdf") == lower.size() - 4 ||
                              content_type.find("pdf") != std::string::npos)) {
        text = extract_pdf_text(bytes);
        if (is_blank(text)) {
            throw std::invalid_argument("Could not extract text from PDF: " + filename);
        }
    } else if (lower.size() >= 6 && lower.rfind(".jsonl") == lower.size() - 6) {
        ingest_jsonl(store, dataset, bytes, filename);
        return;
    } else {
        text = bytes;
        if ((lower.size() >= 5 && lower.rfind(".html") == lower.size() - 5) ||
            (lower.size() >= 4 && lower.rfind(".htm") == lower.size() - 4)) {
            auto html = parse_html(text);
            if (!is_blank(html.title)) title = html.title;
            text = html.body_text;
        }
    }
    add_document(store, dataset, strip_ext(title), text, "", filename, guess_topic(title + " " + text), "");
}

static void ingest_zip(Store& store, const Dataset& dataset, const std::string& bytes) {
    mz_zip_archive zip{};
    if (!mz_zip_reader_init_mem(&zip, bytes.data(), bytes.size(), 0)) {
        throw std::invalid_argument("Could not read zip archive.");
    }
    mz_uint n = mz_zip_reader_get_num_files(&zip);
    for (mz_uint i = 0; i < n; ++i) {
        if (mz_zip_reader_is_file_a_directory(&zip, i)) continue;
        mz_zip_archive_file_stat st{};
        if (!mz_zip_reader_file_stat(&zip, i, &st)) continue;
        size_t sz = 0;
        void* p = mz_zip_reader_extract_to_heap(&zip, i, &sz, 0);
        if (!p) continue;
        std::string content(static_cast<char*>(p), sz);
        mz_free(p);
        try {
            ingest_one(store, dataset, st.m_filename, content, "");
        } catch (const std::invalid_argument&) {
        }
    }
    mz_zip_reader_end(&zip);
}

Dataset ingest_uploads(Store& store, const std::string& name, const std::vector<UploadedFile>& files) {
    if (at_dataset_cap(store)) throw std::invalid_argument(DATASET_CAP_ERROR);
    std::vector<std::string> filenames;
    std::vector<UploadedFile> present;
    for (const auto& f : files) {
        if (f.bytes.empty()) continue;
        present.push_back(f);
        filenames.push_back(f.filename.empty() ? "upload" : f.filename);
    }
    std::string dataset_name = unique_name(store, upload_collection_name(name, filenames), KIND_UPLOAD);
    Dataset dataset = create_dataset(store, dataset_name, KIND_UPLOAD);
    for (const auto& file : present) {
        std::string filename = file.filename.empty() ? "upload" : file.filename;
        std::string lower = ascii_lower(filename);
        try {
            if (lower.size() >= 4 && lower.rfind(".zip") == lower.size() - 4) {
                ingest_zip(store, dataset, file.bytes);
            } else {
                ingest_one(store, dataset, filename, file.bytes, file.content_type);
            }
        } catch (const std::invalid_argument& e) {
            if (present.size() == 1) throw;
            (void)e;
        }
    }
    if (store.count_docs(dataset.id) == 0) {
        store.delete_dataset(dataset.id);
        throw std::invalid_argument("No extractable documents found in upload.");
    }
    return dataset;
}

static Dataset reuse_or_create_url(Store& store, const std::string& dataset_name) {
    auto same = store.datasets_by_kind_and_name(KIND_URLS, dataset_name);
    if (!same.empty()) {
        Dataset keep = same[0];
        for (size_t i = 1; i < same.size(); ++i) {
            store.clear_dataset_contents(same[i].id);
            store.delete_dataset(same[i].id);
        }
        store.clear_dataset_contents(keep.id);
        keep.name = dataset_name;
        keep.created_at = now_iso();
        keep.last_analyzed_at.clear();
        keep.analysis_state = "ingested";
        keep.graph_sage_status = GS_NOT_TRAINED;
        keep.graph_sage_message = GS_DEFAULT_MSG;
        store.save_dataset(keep);
        return keep;
    }
    return create_dataset(store, unique_name(store, dataset_name, KIND_URLS), KIND_URLS);
}

UrlIngestResult ingest_urls(Store& store, const Config& cfg, const std::string& name,
                            const std::vector<std::string>& urls) {
    std::string guessed = sanitize_display_name(name);
    if (guessed.empty()) {
        for (const auto& u : urls) {
            if (!is_blank(u)) {
                guessed = name_from_seed_url(u);
                break;
            }
        }
    }
    auto same = guessed.empty() ? std::vector<Dataset>{} : store.datasets_by_kind_and_name(KIND_URLS, guessed);
    if (same.empty() && at_dataset_cap(store)) throw std::invalid_argument(DATASET_CAP_ERROR);

    auto probe = probe_same_host_reachability(urls, cfg);
    if (!probe.ok) throw std::invalid_argument(URL_MIN_PAGES_ERROR);

    std::string dataset_name = guessed.empty() ? "url" : guessed;
    Dataset dataset = reuse_or_create_url(store, dataset_name);
    UrlCrawlResult crawled;
    try {
        crawled = crawl_urls(urls, cfg, [&](const CrawledPage& page) {
            try {
                add_document(store, dataset, page.title, page.text, clip(page.url, 2000), source_for_seed(page),
                             guess_topic(page.title + " " + page.text), "");
            } catch (const std::invalid_argument&) {
            }
        });
    } catch (...) {
        if (store.count_docs(dataset.id) == 0) {
            store.delete_dataset(dataset.id);
        }
        throw;
    }
    if (store.count_docs(dataset.id) == 0) {
        store.delete_dataset(dataset.id);
        throw std::invalid_argument("No text could be extracted from the provided URLs.");
    }
    UrlIngestResult r;
    r.dataset = store.get_dataset(dataset.id).value_or(dataset);
    r.seed_count = crawled.seed_count;
    r.extra_pages = crawled.extra_pages;
    r.ingested_pages = static_cast<int>(crawled.pages.size());
    r.failed_pages = crawled.failed_pages;
    r.max_depth = cfg.crawl_max_depth;
    r.max_pages = cfg.crawl_max_pages;
    return r;
}

}  // namespace kos
