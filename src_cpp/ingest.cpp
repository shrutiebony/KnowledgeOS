#include "ingest.hpp"
#include "analysis.hpp"
#include "html.hpp"
#include "pdf.hpp"
#include "util.hpp"
#include "wiki_crawl.hpp"

#include "miniz.h"

#include <algorithm>
#include <cctype>
#include <iostream>
#include <map>
#include <regex>
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
                      const std::string& published_at) {
    text = trim(text);
    if (is_blank(text)) throw std::invalid_argument("Document has no extractable text: " + title);
    Document doc;
    doc.dataset_id = dataset.id;
    doc.title = is_blank(title) ? "Untitled" : title;
    doc.text = text;
    doc.url = clip(url, 2000);
    doc.source = source;
    doc.topic = topic;
    doc.published_at = published_at;
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
    auto crawled = crawl_urls(urls, cfg);
    if (crawled.pages.empty()) {
        throw std::invalid_argument("No text could be extracted from the provided URLs.");
    }
    std::string dataset_name = url_collection_name(name, crawled);
    Dataset dataset = reuse_or_create_url(store, dataset_name);
    for (const auto& page : crawled.pages) {
        try {
            add_document(store, dataset, page.title, page.text, clip(page.url, 2000), source_for_seed(page),
                         guess_topic(page.title + " " + page.text), "");
        } catch (const std::invalid_argument&) {
        }
    }
    if (store.count_docs(dataset.id) == 0) {
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

void delete_dataset_and_contents(Store& store, const Dataset& dataset) {
    store.clear_dataset_contents(dataset.id);
    store.delete_dataset(dataset.id);
}

int prune_extra_datasets(Store& store) {
    int removed = 0;
    for (const auto& d : store.all_datasets()) {
        std::string lower = ascii_lower(trim(d.name));
        if (lower == "wikipedia india" || lower == "gdelt") continue;
        delete_dataset_and_contents(store, d);
        removed++;
    }
    return removed;
}

nlohmann::json dataset_brief(Store& store, const Dataset& dataset) {
    nlohmann::json m;
    m["id"] = dataset.id;
    m["name"] = dataset.name;
    m["kind"] = dataset.kind;
    m["analysisState"] = dataset.analysis_state;
    m["documentCount"] = store.count_docs(dataset.id);
    m["topics"] = nlohmann::json::array();
    m["graphSageStatus"] = dataset.graph_sage_status;
    m["wikipedia"] = dataset.kind == KIND_WIKI;
    return m;
}

struct Fixture {
    const char* title;
    const char* topic;
    const char* date;
    const char* body;
};

static void install_offline_fixture(Store& store, const Dataset& dataset) {
    static const Fixture articles[] = {
        {"India", "Countries in India", "2024-06-15",
         "India is a country in South Asia. It is the most populous country and the seventh-largest by area. "
         "The Indian subcontinent has been home to the Indus Valley Civilisation and later to successive empires. "
         "New Delhi is the capital; Mumbai, Kolkata, Chennai, and Bengaluru are major cities."},
        {"Mumbai", "Cities in India", "2023-06-15",
         "Mumbai is the capital of Maharashtra and India's financial centre. The city grew from seven islands "
         "and is known for the film industry in Bollywood, the harbour, and a dense suburban railway."},
        {"Hindi", "Languages of India", "2022-06-15",
         "Hindi is an Indo-Aryan language spoken across northern India. It is written in the Devanagari script "
         "and is one of the official languages of the Union government, alongside English."},
        {"Ganges", "Rivers of India", "2021-06-15",
         "The Ganges rises in the Himalayas and flows across the North Indian plain into the Bay of Bengal. "
         "It is sacred in Hindu tradition and supports a large agricultural population along its basin."},
        {"Kerala", "States and union territories of India", "2023-06-15",
         "Kerala is a state on the Malabar Coast. High literacy, a long coastline, and a history of spice trade "
         "with West Asia and Europe shape its modern reputation."},
        {"Tamil Nadu", "States and union territories of India", "2022-06-15",
         "Tamil Nadu sits on the southeastern coast. Tamil is among the oldest continuously used languages, "
         "and Chennai is a major port and automobile manufacturing hub."},
        {"Rajasthan", "States and union territories of India", "2021-06-15",
         "Rajasthan is India's largest state by area. The Thar Desert, Rajput forts, and cities such as Jaipur "
         "and Jodhpur draw visitors and dominate popular images of the region."},
        {"Bengaluru", "Cities in India", "2024-06-15",
         "Bengaluru is the capital of Karnataka and a centre for information technology and public-sector research. "
         "The city's altitude gives it a milder climate than much of the Deccan."},
        {"Kolkata", "Cities in India", "2020-06-15",
         "Kolkata, formerly Calcutta, was the capital of British India until 1911. It remains a cultural centre "
         "for Bengali literature, theatre, and politics on the Hooghly River."},
        {"Chennai", "Cities in India", "2022-06-15",
         "Chennai is the capital of Tamil Nadu and a major port on the Coromandel Coast. "
         "Carnatic music, Tamil cinema, and automobile plants are local institutions."},
        {"Indian Railways", "Transport in India", "2023-06-15",
         "Indian Railways is among the world's largest rail networks. It moves freight and passengers across "
         "gauge conversions, suburban systems, and long-distance expresses."},
        {"Monsoon", "Climate of India", "2021-06-15",
         "The Indian monsoon is a seasonal reversal of winds that brings most of the country's annual rain "
         "between June and September. Agriculture still tracks its arrival."},
        {"Himalayas", "Mountain ranges of India", "2020-06-15",
         "The Himalayas form India's northern wall. The range includes peaks in India, Nepal, Bhutan, and Tibet "
         "and feeds the Indus, Ganges, and Brahmaputra systems."},
        {"Indian independence movement", "History of India", "2019-06-15",
         "The independence movement gathered mass politics under the Indian National Congress and other groups. "
         "Independence in 1947 was accompanied by Partition of British India."},
        {"Constitution of India", "Law of India", "2024-06-15",
         "The Constitution of India came into force on 26 January 1950. It establishes a federal parliamentary "
         "republic with a long list of fundamental rights and directive principles."},
        {"Lok Sabha", "Politics of India", "2023-06-15",
         "The Lok Sabha is the lower house of India's Parliament. Members are elected from constituencies "
         "for terms of up to five years unless the house is dissolved earlier."},
        {"Cricket in India", "Sport in India", "2022-06-15",
         "Cricket is the most widely followed spectator sport in India. The Board of Control for Cricket in India "
         "runs the national team and the Indian Premier League."},
        {"Bollywood", "Cinema of India", "2021-06-15",
         "Bollywood usually refers to the Hindi-language film industry based in Mumbai. "
         "Indian cinema as a whole also includes large Tamil, Telugu, Malayalam, and Bengali industries."},
        {"Ayurveda", "Medicine in India", "2020-06-15",
         "Ayurveda is a traditional medical system with roots in South Asia. Classical texts discuss diet, "
         "herbal preparations, and humoral ideas that still appear in popular practice."},
        {"Indian cuisine", "Cuisine of India", "2022-06-15",
         "Indian cuisine varies sharply by region. Rice and coconut on the coasts, wheat in the north, "
         "and spice blends such as garam masala appear in many home kitchens."},
        {"Sanskrit", "Languages of India", "2018-06-15",
         "Sanskrit is a classical Indo-Aryan language of ancient India. It is the language of many Hindu, "
         "Buddhist, and Jain texts and the source of a large learned vocabulary in modern Indian languages."},
        {"Brahmaputra", "Rivers of India", "2021-06-15",
         "The Brahmaputra flows from Tibet through Arunachal Pradesh and Assam into Bangladesh. "
         "Seasonal floods reshape the valley and the river's many channels."},
        {"Goa", "States and union territories of India", "2023-06-15",
         "Goa is India's smallest state by area. A long Portuguese colonial period left churches, "
         "place names, and a coastline that is now a major tourist region."},
        {"Punjab, India", "States and union territories of India", "2022-06-15",
         "Punjab in India is a major wheat-growing state. The Green Revolution changed yields, "
         "and Sikh history is closely tied to the region's cities and gurdwaras."},
        {"Hyderabad", "Cities in India", "2024-06-15",
         "Hyderabad is the capital of Telangana. The old city around Charminar sits beside a large "
         "information-technology and pharmaceutical economy in the west of the urban area."},
        {"Indian Ocean", "Oceans", "2019-06-15",
         "The Indian Ocean washes India's west and east coasts. Monsoon winds historically carried "
         "trade between East Africa, Arabia, and the Indian peninsula."}
    };
    for (const auto& a : articles) {
        std::string url = std::string("https://en.wikipedia.org/wiki/") + a.title;
        for (char& c : url) if (c == ' ') c = '_';
        add_document(store, dataset, a.title, a.body, url, WIKI_SOURCE, a.topic, a.date);
    }
}

static void backfill_dates(Store& store, const Config& cfg, int64_t dataset_id) {
    auto docs = store.docs_by_dataset(dataset_id, false);
    std::vector<std::string> titles;
    std::map<std::string, std::vector<int64_t>> ids_by_title;
    for (const auto& doc : docs) {
        if (!doc.published_at.empty() || doc.url.empty()) continue;
        auto t = wiki_title_from_url(doc.url);
        std::string key = t ? *t : doc.title;
        for (char& c : key) if (c == '_') c = ' ';
        key = trim(key);
        if (key.empty()) continue;
        titles.push_back(key);
        ids_by_title[key].push_back(doc.id);
    }
    if (titles.empty()) return;
    auto revisions = fetch_last_revisions(titles, cfg);
    for (const auto& [want, date] : revisions) {
        std::string key = want;
        for (char& c : key) if (c == '_') c = ' ';
        key = trim(key);
        auto it = ids_by_title.find(key);
        if (it == ids_by_title.end()) {
            std::string needle = ascii_lower(key);
            for (auto& row : ids_by_title) {
                if (ascii_lower(row.first) == needle) {
                    it = ids_by_title.find(row.first);
                    break;
                }
            }
        }
        if (it == ids_by_title.end()) continue;
        for (int64_t id : it->second) store.update_published_at_if_null(id, date);
    }
}

static void ensure_analyzed(Store& store, const Config& cfg, int64_t dataset_id) {
    if (store.count_docs(dataset_id) <= 0) return;
    try {
        backfill_dates(store, cfg, dataset_id);
    } catch (...) {
    }
    try {
        analyze_dataset(store, cfg, dataset_id, true);
    } catch (const std::exception& e) {
        auto ds = store.get_dataset(dataset_id);
        if (ds) {
            ds->analysis_state = "error";
            store.save_dataset(*ds);
        }
        std::cerr << "Wikipedia India analysis failed: " << e.what() << "\n";
    }
}

static void crawl_and_analyze(Store& store, Config cfg, int64_t dataset_id) {
    auto dataset = store.get_dataset(dataset_id);
    if (!dataset) return;
    std::unordered_set<std::string> already;
    for (const auto& doc : store.docs_by_dataset(dataset_id, false)) {
        if (!doc.url.empty()) already.insert(doc.url);
    }
    if (!already.empty()) {
        std::cerr << "Scoring " << already.size() << " already-stored Wikipedia India pages.\n";
        ensure_analyzed(store, cfg, dataset_id);
    }
    std::cerr << "Crawling English Wikipedia (India seeds). Already stored=" << already.size()
              << " cap=" << cfg.wikipedia_max_pages << "\n";
    int persisted = 0;
    WikiCrawlStats stats;
    try {
        stats = crawl_wikipedia_india(cfg, already, [&](const WikiPage& page) {
            add_document(store, *dataset, page.title, page.text, page.url, WIKI_SOURCE,
                         page.topic.empty() ? "India" : page.topic, page.published_at);
            persisted++;
            if (persisted % cfg.wikipedia_flush_every == 0) {
                std::cerr << "Wikipedia India progress: " << persisted << " new pages this run, "
                          << store.count_docs(dataset_id) << " stored. Analyzing incrementally.\n";
                try {
                    analyze_dataset(store, cfg, dataset_id, true);
                } catch (const std::exception& e) {
                    std::cerr << "Incremental analysis failed: " << e.what() << "\n";
                }
            }
        });
    } catch (const std::exception& e) {
        std::cerr << "Wikipedia India crawl failed: " << e.what() << "\n";
        auto ds = store.get_dataset(dataset_id);
        if (ds) {
            ds->analysis_state = "error";
            store.save_dataset(*ds);
        }
        return;
    }
    long stored = store.count_docs(dataset_id);
    if ((!stats.error.empty() && stored == 0) || stored == 0) {
        std::cerr << "Wikipedia India crawl stored 0 pages. Installing offline fixture.\n";
        if (!stats.error.empty()) std::cerr << stats.error << "\n";
        install_offline_fixture(store, *dataset);
        stored = store.count_docs(dataset_id);
        if (stored == 0) {
            auto ds = store.get_dataset(dataset_id);
            if (ds) {
                ds->analysis_state = "error";
                store.save_dataset(*ds);
            }
            return;
        }
    }
    auto ds = store.get_dataset(dataset_id);
    if (ds) {
        ds->analysis_state = "ingested";
        store.save_dataset(*ds);
    }
    std::cerr << "Wikipedia India crawl finished: " << stored << " pages stored (" << persisted
              << " new). Analyzing.\n";
    ensure_analyzed(store, cfg, dataset_id);
}

void seed_wikipedia_india(Store& store, const Config& cfg) {
    if (!cfg.seed_wikipedia) return;
    for (const auto& d : store.all_datasets()) {
        bool wiki_kind = d.kind == KIND_WIKI;
        bool legacy = ascii_lower(trim(d.name)) == "wikipedia sample";
        bool india = iequals(trim(d.name), WIKI_DATASET_NAME);
        if ((wiki_kind || legacy) && !india) {
            std::cerr << "Removing legacy Wikipedia dataset '" << d.name << "'\n";
            delete_dataset_and_contents(store, d);
        }
    }
    auto existing = store.find_by_kind_and_name(KIND_WIKI, WIKI_DATASET_NAME);
    if (!existing) existing = store.find_by_name_ignore_case(WIKI_DATASET_NAME);
    if (!cfg.wikipedia_crawl) {
        if (!existing) {
            auto ds = create_dataset(store, WIKI_DATASET_NAME, KIND_WIKI);
            install_offline_fixture(store, ds);
            analyze_dataset(store, cfg, ds.id, false);
            std::cerr << "Installed offline Wikipedia India fixture (" << store.count_docs(ds.id)
                      << " documents).\n";
        } else {
            ensure_analyzed(store, cfg, existing->id);
        }
        return;
    }
    if (existing && store.count_docs(existing->id) >= cfg.wikipedia_max_pages) {
        std::cerr << "Wikipedia India already stored (" << store.count_docs(existing->id) << " pages).\n";
        ensure_analyzed(store, cfg, existing->id);
        return;
    }
    Dataset dataset = existing ? *existing : create_dataset(store, WIKI_DATASET_NAME, KIND_WIKI);
    dataset.analysis_state = "crawling";
    store.save_dataset(dataset);
    std::thread([store_ptr = &store, cfg, id = dataset.id]() {
        crawl_and_analyze(*store_ptr, cfg, id);
    }).detach();
    std::cerr << "Started Wikipedia India crawl in the background (maxPages=" << cfg.wikipedia_max_pages
              << ", depth=" << cfg.wikipedia_max_depth << ").\n";
}

}  // namespace kos
