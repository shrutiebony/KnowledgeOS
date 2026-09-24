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

void delete_dataset_and_contents(Store& store, const Dataset& dataset) {
    // delete_dataset now drops documents, edges, child rows, and orphans in one transaction.
    store.delete_dataset(dataset.id);
}

bool is_protected_wikipedia(const Dataset& dataset) {
    if (dataset.kind == KIND_WIKI || dataset.kind == KIND_WIKI_SUBSET) return true;
    return is_wikipedia_name(dataset.name);
}

bool is_gdelt_dataset(const Dataset& dataset) {
    if (dataset.kind == KIND_GDELT) return true;
    return ascii_lower(trim(dataset.name)) == "gdelt";
}

int visible_dataset_count(Store& store) {
    int n = 0;
    for (const auto& d : store.all_datasets()) {
        if (d.kind == KIND_WIKI_SUBSET) continue;
        n++;
    }
    return n;
}

bool at_dataset_cap(Store& store) {
    return visible_dataset_count(store) >= MAX_DATASETS;
}

static int parse_wiki_target(const std::string& parent_topic) {
    for (const char* prefix : {"countries:", "india:", "general:"}) {
        if (parent_topic.rfind(prefix, 0) == 0) {
            try {
                return std::stoi(parent_topic.substr(std::char_traits<char>::length(prefix)));
            } catch (...) {
                return 0;
            }
        }
    }
    return 0;
}

static int topic_count_of(const std::map<std::string, int>& counts, const std::string& topic) {
    auto it = counts.find(topic);
    return it == counts.end() ? 0 : it->second;
}

static std::map<std::string, int> stored_topic_counts(Store& store, int64_t dataset_id) {
    std::map<std::string, int> out;
    for (const auto& d : store.docs_by_dataset(dataset_id, false)) {
        std::string raw = trim(d.topic);
        if (raw.empty()) continue;
        std::string key = canonical_wiki_topic(raw, raw);
        if (key.empty() || iequals(key, "General")) key = raw;
        out[key]++;
    }
    return out;
}

static bool country_topics_incomplete(const std::map<std::string, int>& counts, int per_topic) {
    return topic_count_of(counts, SHARED_TOPIC_USA) < per_topic ||
           topic_count_of(counts, SHARED_TOPIC_GERMANY) < per_topic ||
           topic_count_of(counts, SHARED_TOPIC_AUSTRALIA) < per_topic;
}

static int wiki_india_count(const std::map<std::string, int>& counts) {
    return topic_count_of(counts, SHARED_TOPIC_INDIA) + topic_count_of(counts, SHARED_TOPIC_GEO_INDIA);
}

static bool wiki_topics_incomplete(const Config& cfg, const std::map<std::string, int>& counts, int per_topic) {
    if (cfg.wikipedia_crawl_india && wiki_india_count(counts) < 500) return true;
    if (cfg.wikipedia_crawl_germany && topic_count_of(counts, SHARED_TOPIC_GERMANY) < per_topic) return true;
    if (cfg.wikipedia_crawl_usa && topic_count_of(counts, SHARED_TOPIC_USA) < per_topic) return true;
    if (cfg.wikipedia_crawl_australia && topic_count_of(counts, SHARED_TOPIC_AUSTRALIA) < per_topic) return true;
    return false;
}

int prune_extra_datasets(Store& store) {
    int renamed = 0;
    for (const auto& d : store.all_datasets()) {
        if (!is_protected_wikipedia(d)) continue;
        if (d.kind == KIND_WIKI_SUBSET) continue;
        if (iequals(trim(d.name), WIKI_DATASET_NAME) && d.kind == KIND_WIKI) continue;
        Dataset next = d;
        next.name = WIKI_DATASET_NAME;
        next.kind = KIND_WIKI;
        store.save_dataset(next);
        renamed++;
        std::cerr << "Renamed leftover Wikipedia collection '" << d.name << "' to " << WIKI_DATASET_NAME << "\n";
    }
    return renamed;
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
    m["wikipedia"] = is_protected_wikipedia(dataset);
    m["gdelt"] = is_gdelt_dataset(dataset);
    m["deletable"] = !is_protected_wikipedia(dataset) && !is_gdelt_dataset(dataset);
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
         "India is a country in South Asia, the most populous in the world and the seventh-largest by land area. "
         "The subcontinent has been home to the Indus Valley Civilisation and later to successive empires that "
         "left languages, legal ideas, and cities still in use. New Delhi is the capital; Mumbai, Kolkata, Chennai, "
         "and Bengaluru are major commercial and cultural centres. A federal parliamentary republic since 1950, "
         "the Union includes states and union territories that differ sharply in language, climate, and economy. "
         "Agriculture, services, and manufacturing all employ large workforces, and internal migration links "
         "the coast, the Deccan, and the northern plains."},
        {"Mumbai", "Cities in India", "2023-06-15",
         "Mumbai is the capital of Maharashtra and India's principal financial centre. The modern city grew from "
         "a cluster of islands joined by land reclamation and now concentrates banking, film production, and a "
         "dense suburban railway that moves millions each day. The harbour, the film studios often grouped under "
         "the name Bollywood, and a long coastline shape both work and popular culture. Housing pressure and "
         "monsoon flooding are persistent local problems. Despite that, the metropolitan region remains a magnet "
         "for migrants from across the country who come for jobs in trade, services, and entertainment."},
        {"Hindi", "Languages of India", "2022-06-15",
         "Hindi is an Indo-Aryan language spoken across much of northern and central India. It is written in the "
         "Devanagari script and is one of the official languages of the Union government, used alongside English "
         "in central administration. Everyday speech sits on a continuum with Urdu and with regional varieties "
         "that differ in vocabulary and sound. Standard Hindi draws a large learned vocabulary from Sanskrit, "
         "while film, radio, and school textbooks have spread a more uniform public register. Millions of people "
         "use Hindi as a second language for work and travel even when another language is spoken at home."},
        {"Ganges", "Rivers of India", "2021-06-15",
         "The Ganges rises in the Himalayas of Uttarakhand and flows across the North Indian plain before "
         "entering the delta that opens into the Bay of Bengal. Along that course it is joined by large "
         "tributaries such as the Yamuna, Ghaghara, Gandaki, and Kosi, and it supports irrigation, fishing, "
         "and dense settlement on some of the most farmed land in Asia. In Hindu tradition the river is sacred, "
         "and pilgrimage towns such as Haridwar and Varanasi stand on its banks. Seasonal snowmelt and monsoon "
         "rain drive a flood pulse that both renews soils and damages homes. Pollution from cities and industry "
         "is a long-running public issue. The basin remains a single document-length subject: one river system, "
         "not a list of isolated word tokens, tying mountain source to tidal mouth."},
        {"Kerala", "States and union territories of India", "2023-06-15",
         "Kerala is a state on the Malabar Coast, between the Western Ghats and the Arabian Sea. High literacy, "
         "a long coastline, and centuries of spice trade with West Asia and Europe still shape its reputation. "
         "Coconut, rice, and fishing remain visible in the lowlands, while tea and cardamom grow in the hills. "
         "A strong tradition of public services and overseas remittances from workers in the Gulf has changed "
         "household incomes. Malayalam is the principal language. Backwaters, churches, temples, and a distinctive "
         "cuisine draw visitors, but the state's economy also includes information services and small manufacturing."},
        {"Tamil Nadu", "States and union territories of India", "2022-06-15",
         "Tamil Nadu occupies the southeastern coast of India, facing the Bay of Bengal. Tamil is among the oldest "
         "continuously used literary languages, and it is the language of state administration and a large cinema "
         "industry. Chennai is a major port and a centre for automobile and software work. Inland, temple towns "
         "and agricultural districts around the Kaveri contrast with industrial corridors. The state has a long "
         "history of social reform movements and a competitive party system. Seasonal northeast rains matter as "
         "much as the southwest monsoon for farming in several districts."},
        {"Rajasthan", "States and union territories of India", "2021-06-15",
         "Rajasthan is India's largest state by area, stretching from the Thar Desert to the edges of the Aravalli "
         "range. Forts, palaces, and cities such as Jaipur, Jodhpur, and Udaipur dominate popular images of the "
         "region, but the economy also includes mining, textiles, and irrigated agriculture where canals reach. "
         "Rainfall is uneven; pastoralism and tank irrigation have long been adaptations to scarcity. Rajput "
         "political history and later princely states left a dense heritage of courts and trade routes. Tourism "
         "is important, yet large rural populations still depend on livestock, crafts, and seasonal labour."},
        {"Bengaluru", "Cities in India", "2024-06-15",
         "Bengaluru is the capital of Karnataka and a major centre for information technology, start-ups, and "
         "public-sector research. The city's altitude on the Deccan plateau gives it a milder climate than much "
         "of southern India, which helped it grow as a cantonment and later as a science hub. Software parks and "
         "aerospace work sit beside older neighbourhoods, lakes, and a large informal service economy. Rapid "
         "growth has strained water, traffic, and housing. Kannada is the local language, while English is widely "
         "used in the technology sector that drew migrants from other states."},
        {"Kolkata", "Cities in India", "2020-06-15",
         "Kolkata, formerly Calcutta, was the capital of British India until 1911 and remains the principal city "
         "of West Bengal. It stands on the Hooghly, a distributary of the Ganges, and grew as a port and "
         "administrative centre. Bengali literature, theatre, and political debate still give the city a strong "
         "cultural identity. Trams, howrah-bound traffic, and dense neighbourhoods mark daily life. After "
         "partition and later industrial change, services and education became more visible than older mills. "
         "The metropolitan area continues to draw people from the eastern hinterland for work and study."},
        {"Chennai", "Cities in India", "2022-06-15",
         "Chennai is the capital of Tamil Nadu and a major port on the Coromandel Coast. Carnatic music, Tamil "
         "cinema, and automobile plants are local institutions, and the city is also a centre for information "
         "technology and medical services. A long beachfront, colonial-era neighbourhoods, and expanding suburbs "
         "sit on a low coastal plain that is exposed to cyclones and flooding. Tamil is the everyday language. "
         "The harbour and rail links tie the city to the rest of the peninsula, while internal migrants staff "
         "factories and construction."},
        {"Indian Railways", "Transport in India", "2023-06-15",
         "Indian Railways is among the world's largest rail networks by route length and passenger volume. It "
         "moves freight and people across broad-gauge main lines, suburban systems in the largest cities, and "
         "long-distance expresses that still define how many families travel. Gauge conversion, electrification, "
         "and dedicated freight corridors have been long projects. The network is a state-owned organisation "
         "with regional zones, and it remains a major employer. Timetables, reserved seating, and unreserved "
         "ordinary trains coexist. Weather, festivals, and harvest seasons regularly test capacity."},
        {"Monsoon", "Climate of India", "2021-06-15",
         "The Indian monsoon is a seasonal reversal of winds that brings most of the country's annual rain "
         "between June and September in much of the peninsula and the north. Moisture from the Indian Ocean "
         "is lifted over the Western Ghats and the Himalayan front, producing sharp regional contrasts: a wet "
         "west coast, a rain-shadow Deccan, and delayed or failed bursts that still decide harvests. Agriculture, "
         "reservoirs, and city drainage are planned around its arrival. A weaker northeast monsoon later in the "
         "year matters for Tamil Nadu and nearby coasts. Forecasting has improved, but year-to-year variation "
         "remains a central fact of Indian climate."},
        {"Himalayas", "Mountain ranges of India", "2020-06-15",
         "The Himalayas form India's northern wall, a chain of ranges that includes high peaks in India, Nepal, "
         "Bhutan, and Tibet. Snow and glaciers feed the Indus, Ganges, and Brahmaputra systems that water the "
         "plains. The mountains are also a seismic belt, a barrier that shapes monsoon circulation, and a home "
         "to distinct languages and farming systems in the valleys. Roads and trekking routes have opened some "
         "districts to tourism and the army, while others remain remote. Uplift is geologically young, which "
         "helps explain steep rivers, landslides, and the sharp rise from the plains to the snow line."},
        {"Indian independence movement", "History of India", "2019-06-15",
         "The independence movement gathered mass politics under the Indian National Congress and other groups "
         "over several decades, combining legal petition, non-cooperation, and, in some strands, armed revolt. "
         "Leaders argued over social reform, the place of religion in public life, and how to confront colonial "
         "rule. Independence in 1947 was accompanied by Partition of British India into two dominions and by "
         "large-scale displacement. The movement left a vocabulary of rights, civil disobedience, and "
         "constitutionalism that later governments still cite. Regional movements and princely-state accession "
         "were part of the same end of empire, not a single street protest."},
        {"Constitution of India", "Law of India", "2024-06-15",
         "The Constitution of India came into force on 26 January 1950. It establishes a federal parliamentary "
         "republic with a long list of fundamental rights, directive principles, and an independent judiciary. "
         "The text is among the world's longest written constitutions and has been amended many times. It "
         "distributes powers between the Union and the states, provides for emergency provisions, and sets "
         "rules for elections and public services. Debates in the Constituent Assembly drew on colonial law, "
         "other constitutions, and domestic political experience. Courts continue to interpret equality, "
         "liberty, and federal balance in light of that document."},
        {"Lok Sabha", "Politics of India", "2023-06-15",
         "The Lok Sabha is the lower house of India's Parliament. Members are elected from territorial "
         "constituencies for terms of up to five years unless the house is dissolved earlier. Government is "
         "formed by the party or coalition that can command a majority, and the council of ministers is "
         "collectively responsible to this house. Business includes legislation, budgets, and questions to "
         "ministers. Representation is based on population, with reserved seats for scheduled castes and "
         "scheduled tribes in specified constituencies. The Rajya Sabha, the upper house, is not a copy of "
         "the same electoral map and plays a different federal role."},
        {"Cricket in India", "Sport in India", "2022-06-15",
         "Cricket is the most widely followed spectator sport in India. The Board of Control for Cricket in India "
         "runs the national team and the Indian Premier League, a franchise tournament that changed the sport's "
         "calendar and finances. Test, one-day, and Twenty20 formats all have large audiences, and neighbourhood "
         "grounds still produce players who move into state associations. Television rights and sponsorship "
         "make cricket a major media industry. Other sports have strong regional followings, but cricket occupies "
         "a unique place in public conversation after a win or a collapse."},
        {"Bollywood", "Cinema of India", "2021-06-15",
         "Bollywood usually refers to the Hindi-language film industry based in Mumbai, with songs, stars, and "
         "wide distribution across India and the diaspora. Indian cinema as a whole also includes large Tamil, "
         "Telugu, Malayalam, and Bengali industries, each with its own studios, audiences, and award circuits. "
         "Production has shifted between studio lots, location shooting, and streaming platforms. Film music "
         "and dialogue feed popular speech. The word Bollywood is often used loosely for all Indian popular "
         "film, which hides how regional industries compete and collaborate rather than forming a single factory."},
        {"Ayurveda", "Medicine in India", "2020-06-15",
         "Ayurveda is a traditional medical system with roots in South Asia. Classical texts discuss diet, "
         "herbal preparations, surgery in some early sources, and humoral ideas that still appear in popular "
         "practice. Modern India regulates Ayurvedic education and pharmacies alongside biomedicine, and many "
         "households use both. Critics ask for stronger evidence on specific remedies; practitioners point to "
         "long clinical traditions and preventive routines. The subject sits at the intersection of history of "
         "science, public health, and a large commercial market for oils, tonics, and wellness tourism."},
        {"Indian cuisine", "Cuisine of India", "2022-06-15",
         "Indian cuisine varies sharply by region rather than forming one national menu. Rice and coconut are "
         "common on the coasts, wheat and dairy in much of the north, and millet or rice inland depending on "
         "rainfall. Spice blends such as garam masala appear in many home kitchens, but the actual mix changes "
         "by household and community. Vegetarian and non-vegetarian traditions coexist, shaped by religion, "
         "caste history, and local supply. Restaurant 'Indian' food abroad often reflects a few North Indian "
         "and restaurant-adapted dishes, not the full range of tiffin, street snacks, and festival sweets."},
        {"Sanskrit", "Languages of India", "2018-06-15",
         "Sanskrit is a classical Indo-Aryan language of ancient India. It is the language of many Hindu, "
         "Buddhist, and Jain texts and the source of a large learned vocabulary in modern Indian languages. "
         "Panini's grammar made it a model of linguistic description. Today it is a scheduled language with "
         "a small number of first-language speakers and a larger number of students who read it for religion, "
         "literature, or historical research. Inscriptions, drama, and scientific treatises show how the "
         "language was used in courts and monasteries, not only in hymns."},
        {"Brahmaputra", "Rivers of India", "2021-06-15",
         "The Brahmaputra rises in Tibet, cuts through the eastern Himalaya, and flows through Arunachal Pradesh "
         "and Assam before entering Bangladesh, where it joins the Ganges-Meghna system. In Assam the river is "
         "wide, braided, and unstable: seasonal floods reshape channels, erode villages, and deposit new sand "
         "bars. It carries snowmelt and some of the heaviest monsoon rain in the country. Navigation, fishing, "
         "and ferry crossings remain everyday uses. The valley's tea gardens, wetlands, and towns all sit in "
         "relation to this one river. Treaties and flood-control works treat it as a shared international basin, "
         "not as a short label on a map."},
        {"Goa", "States and union territories of India", "2023-06-15",
         "Goa is India's smallest state by area, on the west coast between the Western Ghats and the Arabian Sea. "
         "A long Portuguese colonial period left churches, place names, legal traces, and a Catholic community "
         "alongside a Hindu majority. The coastline is a major tourist region, with fishing villages and later "
         "resort strips. Konkani is the official language; Marathi and English are also widely used. Iron ore "
         "mining and tourism have been economic mainstays, each bringing environmental disputes. After 1961 the "
         "territory was integrated into the Indian Union and later became a state."},
        {"Punjab, India", "States and union territories of India", "2022-06-15",
         "Punjab in India is a major wheat- and rice-growing state on the alluvial plain east of the international "
         "border. The Green Revolution raised yields through tubewells, fertiliser, and high-yielding varieties, "
         "and also left a legacy of groundwater stress. Sikh history is closely tied to the region's cities and "
         "gurdwaras, and Punjabi is the official language. Chandigarh serves as a shared capital with Haryana. "
         "Migration to other Indian cities and abroad is common. The state is densely settled, canal-irrigated "
         "in many districts, and politically organised around farmers as well as urban trade."},
        {"Hyderabad", "Cities in India", "2024-06-15",
         "Hyderabad is the capital of Telangana. The old city around Charminar, with its markets and Qutb Shahi "
         "and Asaf Jahi heritage, sits beside a large information-technology and pharmaceutical economy in the "
         "west of the urban area. Telugu and Urdu have long been spoken here, and the city was the seat of the "
         "Nizams before integration into India. Lakes, rock outcrops, and planned townships mark the landscape. "
         "After the creation of Telangana, Hyderabad remained a joint capital for a transitional period and then "
         "the state's own capital, while continuing as a national technology hub."},
        {"Indian Ocean", "Oceans", "2019-06-15",
         "The Indian Ocean washes India's west and east coasts and links the subcontinent to East Africa, Arabia, "
         "and Southeast Asia. Monsoon winds historically carried sailing trade across that basin; steam and later "
         "container shipping still use the same sea lanes. The ocean drives the monsoon that waters Indian "
         "agriculture, and it is the source of cyclones that strike both coasts. Ports such as Mumbai, Kochi, "
         "Chennai, and Visakhapatnam sit on its rim. Exclusive economic zones, fisheries, and naval presence "
         "make it a standing subject of Indian geography and policy, not a decorative label on a coastal map."}
    };
    for (const auto& a : articles) {
        std::string url = std::string("https://en.wikipedia.org/wiki/") + a.title;
        for (char& c : url) if (c == ' ') c = '_';
        add_document(store, dataset, a.title, a.body, url, WIKI_SOURCE, a.topic, a.date, a.date);
    }
}

static std::string wiki_title_key(const Document& doc) {
    auto t = wiki_title_from_url(doc.url);
    std::string key = t ? *t : doc.title;
    for (char& c : key) if (c == '_') c = ' ';
    return trim(key);
}

static int apply_revision_dates(Store& store, const std::map<std::string, std::vector<int64_t>>& ids_by_title,
                                const std::map<std::string, std::string>& revisions, bool created,
                                bool overwrite = false) {
    int updated = 0;
    for (const auto& [want, date] : revisions) {
        if (date.size() < 4) continue;
        std::string key = want;
        for (char& c : key) if (c == '_') c = ' ';
        key = trim(key);
        auto it = ids_by_title.find(key);
        if (it == ids_by_title.end()) {
            std::string needle = ascii_lower(key);
            for (auto row = ids_by_title.begin(); row != ids_by_title.end(); ++row) {
                if (ascii_lower(row->first) == needle) {
                    it = row;
                    break;
                }
            }
        }
        if (it == ids_by_title.end()) continue;
        for (int64_t id : it->second) {
            int n = 0;
            if (created) {
                n = overwrite ? store.update_created_at(id, date)
                              : store.update_created_at_if_null(id, date);
            } else {
                n = store.update_published_at_if_null(id, date);
            }
            updated += n;
        }
    }
    return updated;
}

static void backfill_dates(Store& store, const Config& cfg, int64_t dataset_id) {
    auto docs = store.docs_by_dataset(dataset_id, false);
    std::vector<std::string> titles;
    std::map<std::string, std::vector<int64_t>> ids_by_title;
    for (const auto& doc : docs) {
        if (!doc.published_at.empty() || doc.url.empty()) continue;
        std::string key = wiki_title_key(doc);
        if (key.empty()) continue;
        titles.push_back(key);
        ids_by_title[key].push_back(doc.id);
    }
    if (titles.empty()) return;
    apply_revision_dates(store, ids_by_title, fetch_last_revisions(titles, cfg), false);
}

static int wiki_year_prefix(const std::string& date) {
    if (date.size() < 4) return 0;
    try {
        return std::stoi(date.substr(0, 4));
    } catch (...) {
        return 0;
    }
}

static bool wiki_created_collapsed(const std::vector<Document>& docs) {
    int n = 0;
    int bad = 0;
    std::map<int, int> years;
    for (const auto& doc : docs) {
        n++;
        int y = wiki_year_prefix(doc.created_at);
        if (y <= 0 || y == 2016) bad++;
        if (y > 0) years[y]++;
    }
    if (n <= 0) return false;
    if (bad * 2 >= n) return true;
    return years.size() <= 1;
}

static int backfill_created_at(Store& store, const Config& cfg, int64_t dataset_id) {
    auto docs = store.docs_by_dataset(dataset_id, false);
    const bool collapsed = wiki_created_collapsed(docs);
    std::vector<std::string> titles;
    std::map<std::string, std::vector<int64_t>> ids_by_title;
    for (const auto& doc : docs) {
        if (doc.url.empty()) continue;
        bool need = doc.created_at.empty();
        if (collapsed) need = true;
        if (!need) continue;
        std::string key = wiki_title_key(doc);
        if (key.empty()) continue;
        titles.push_back(key);
        ids_by_title[key].push_back(doc.id);
    }
    if (titles.empty()) return 0;
    std::cerr << "Backfilling Wikipedia first-revision createdAt for " << titles.size() << " pages.\n";
    int updated = apply_revision_dates(store, ids_by_title, fetch_first_revisions(titles, cfg), true, collapsed);
    std::cerr << "First-revision createdAt written for " << updated << " documents.\n";
    return updated;
}

static void normalize_stored_wiki_topics(Store& store, int64_t dataset_id) {
    auto docs = store.docs_by_dataset(dataset_id, false);
    for (const auto& doc : docs) {
        std::string next = canonical_wiki_topic(doc.topic, doc.title);
        if (next != doc.topic) store.update_topic(doc.id, next);
    }
}

static std::atomic<bool> g_wiki_created_backfill_busy{false};

static void start_wiki_created_at_backfill(Store& store, const Config& cfg, int64_t dataset_id) {
    bool expected = false;
    if (!g_wiki_created_backfill_busy.compare_exchange_strong(expected, true)) return;
    std::thread([store_ptr = &store, cfg, dataset_id]() {
        try {
            backfill_created_at(*store_ptr, cfg, dataset_id);
        } catch (const std::exception& e) {
            std::cerr << "Wikipedia first-revision createdAt backfill failed: " << e.what() << "\n";
        }
        g_wiki_created_backfill_busy.store(false);
    }).detach();
}

static int prepare_wiki_metadata(Store& store, const Config& cfg, int64_t dataset_id) {
    try {
        normalize_stored_wiki_topics(store, dataset_id);
    } catch (const std::exception& e) {
        std::cerr << "Wikipedia topic normalize failed: " << e.what() << "\n";
    }
    start_wiki_created_at_backfill(store, cfg, dataset_id);
    return 0;
}

static void ensure_analyzed(Store& store, const Config& cfg, int64_t dataset_id) {
    if (store.count_docs(dataset_id) <= 0) return;
    int created_at_writes = prepare_wiki_metadata(store, cfg, dataset_id);
    try {
        // Recompute scores when first-revision dates arrive so recency and the 50/50 split match the page.
        analyze_dataset(store, cfg, dataset_id, created_at_writes <= 0);
    } catch (const std::exception& e) {
        auto ds = store.get_dataset(dataset_id);
        if (ds) {
            ds->analysis_state = "error";
            store.save_dataset(*ds);
        }
        std::cerr << "Wikipedia analysis failed: " << e.what() << "\n";
    }
}

static void crawl_and_analyze(Store& store, Config cfg, int64_t dataset_id) {
    auto dataset = store.get_dataset(dataset_id);
    if (!dataset) return;
    std::unordered_set<std::string> already;
    for (const auto& doc : store.docs_by_dataset(dataset_id, false)) {
        if (!doc.url.empty()) already.insert(doc.url);
    }
    if (!already.empty() && store.count_unscored(dataset_id) > 0) {
        std::cerr << "Deferring score of " << already.size()
                  << " already-stored Wikipedia pages until country crawl flushes.\n";
    }
    auto topic_counts = stored_topic_counts(store, dataset_id);
    std::cerr << "Crawling English Wikipedia (Germany / India; Australia harvest off). Already stored="
              << already.size() << " per-topic cap=" << cfg.wikipedia_max_pages
              << " Germany=" << topic_count_of(topic_counts, SHARED_TOPIC_GERMANY)
              << " India=" << wiki_india_count(topic_counts)
              << " USA=" << topic_count_of(topic_counts, SHARED_TOPIC_USA)
              << " Australia=" << topic_count_of(topic_counts, SHARED_TOPIC_AUSTRALIA) << "\n";
    int persisted = 0;
    WikiCrawlStats stats;
    try {
        stats = crawl_wikipedia(cfg, already, [&](const WikiPage& page) {
            const std::string topic = page.topic.empty() ? "General" : page.topic;
            if (!is_shared_country_topic(topic) && !is_shared_india_topic(topic)) return;
            add_document(store, *dataset, page.title, page.text, page.url, WIKI_SOURCE,
                         topic, page.published_at, page.created_at);
            persisted++;
            if (persisted % cfg.wikipedia_flush_every == 0) {
                std::cerr << "Wikipedia progress: " << persisted << " new pages this run, "
                          << store.count_docs(dataset_id) << " stored. Analyzing incrementally.\n";
                try {
                    analyze_dataset(store, cfg, dataset_id, true);
                } catch (const std::exception& e) {
                    std::cerr << "Incremental analysis failed: " << e.what() << "\n";
                }
            }
        }, topic_counts);
    } catch (const std::exception& e) {
        std::cerr << "Wikipedia crawl failed: " << e.what() << "\n";
        auto ds = store.get_dataset(dataset_id);
        if (ds) {
            ds->analysis_state = "error";
            store.save_dataset(*ds);
        }
        return;
    }
    long stored = store.count_docs(dataset_id);
    if ((!stats.error.empty() && stored == 0) || stored == 0) {
        std::cerr << "Wikipedia crawl stored 0 pages. Installing offline fixture.\n";
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
    std::cerr << "Wikipedia crawl finished: " << stored << " pages stored (" << persisted
              << " new). Analyzing.\n";
    try {
        analyze_dataset(store, cfg, dataset_id, false);
    } catch (const std::exception& e) {
        std::cerr << "Wikipedia analysis failed: " << e.what() << "\n";
    }
}

static std::optional<Dataset> find_wikipedia_dataset(Store& store) {
    auto existing = store.find_by_kind_and_name(KIND_WIKI, WIKI_DATASET_NAME);
    if (existing) return existing;
    existing = store.find_by_name_ignore_case(WIKI_DATASET_NAME);
    if (existing) return existing;
    existing = store.find_by_name_ignore_case(WIKI_DATASET_NAME_LEGACY);
    if (existing) return existing;
    existing = store.find_by_name_ignore_case("wikipedia sample");
    if (existing) return existing;
    return store.find_first_by_kind(KIND_WIKI);
}

void seed_wikipedia(Store& store, const Config& cfg) {
    if (!cfg.seed_wikipedia) return;
    auto existing = find_wikipedia_dataset(store);
    int target = cfg.wikipedia_max_pages;
    if (existing) {
        if (!iequals(trim(existing->name), WIKI_DATASET_NAME) || existing->kind != KIND_WIKI) {
            std::cerr << "Renaming Wikipedia collection '" << existing->name << "' to " << WIKI_DATASET_NAME << "\n";
            existing->name = WIKI_DATASET_NAME;
            existing->kind = KIND_WIKI;
            store.save_dataset(*existing);
        }
        int dropped = store.delete_docs_below_word_count(existing->id, WIKI_MIN_ARTICLE_WORDS);
        if (dropped > 0) {
            std::cerr << "Removed " << dropped
                      << " short Wikipedia stubs (< " << WIKI_MIN_ARTICLE_WORDS
                      << " words) so live crawl can store article text.\n";
        }
        long count = store.count_docs(existing->id);
        int stored_target = parse_wiki_target(existing->parent_topic);
        target = std::max({WIKI_MIN_COUNTRY_PAGES, WIKI_MIN_SHARED_PAGES, cfg.wikipedia_max_pages});
        if (stored_target != target || existing->parent_topic.rfind("countries:", 0) != 0) {
            existing->parent_topic = std::string("countries:") + std::to_string(target);
            store.save_dataset(*existing);
            std::cerr << "Wikipedia country-topic target: keep " << count
                      << " existing pages, aim for " << target
                      << " each enabled country topic (Germany / India; Australia off).\n";
        }
    }
    if (!cfg.wikipedia_crawl) {
        if (!existing) {
            auto ds = create_dataset(store, WIKI_DATASET_NAME, KIND_WIKI);
            install_offline_fixture(store, ds);
            analyze_dataset(store, cfg, ds.id, false);
            std::cerr << "Installed offline Wikipedia fixture (" << store.count_docs(ds.id)
                      << " documents).\n";
        } else {
            ensure_analyzed(store, cfg, existing->id);
        }
        return;
    }
    if (existing && !wiki_topics_incomplete(cfg, stored_topic_counts(store, existing->id), target)) {
        std::cerr << "Wikipedia country topics already stored (" << store.count_docs(existing->id)
                  << " pages total).\n";
        ensure_analyzed(store, cfg, existing->id);
        return;
    }
    Dataset dataset = existing ? *existing : create_dataset(store, WIKI_DATASET_NAME, KIND_WIKI);
    if (!wiki_topics_incomplete(cfg, stored_topic_counts(store, dataset.id), target)) {
        start_wiki_created_at_backfill(store, cfg, dataset.id);
    } else {
        std::cerr << "Skipping Wikipedia revision backfill until country topics are stored.\n";
    }
    if (!existing) {
        target = std::max({WIKI_MIN_COUNTRY_PAGES, WIKI_MIN_SHARED_PAGES, cfg.wikipedia_max_pages});
        dataset.parent_topic = std::string("countries:") + std::to_string(target);
    }
    dataset.analysis_state = "crawling";
    store.save_dataset(dataset);
    Config crawl_cfg = cfg;
    crawl_cfg.wikipedia_max_pages = target;
    std::thread([store_ptr = &store, crawl_cfg, id = dataset.id]() {
        try {
            crawl_and_analyze(*store_ptr, crawl_cfg, id);
        } catch (const std::exception& e) {
            std::cerr << "Wikipedia crawl thread failed: " << e.what() << "\n";
        } catch (...) {
            std::cerr << "Wikipedia crawl thread failed with an unknown error.\n";
        }
    }).detach();
    std::cerr << "Started Wikipedia crawl in the background (per-topic maxPages=" << target
              << ", depth=" << cfg.wikipedia_max_depth << ").\n";
}

static void crawl_gdelt_and_analyze(Store& store, Config cfg, int64_t dataset_id) {
    auto dataset = store.get_dataset(dataset_id);
    if (!dataset) return;
    std::unordered_set<std::string> already;
    for (const auto& doc : store.docs_by_dataset(dataset_id, false)) {
        if (!doc.url.empty()) already.insert(doc.url);
    }
    if (!already.empty() && store.count_unscored(dataset_id) > 0) {
        std::cerr << "Deferring score of " << already.size()
                  << " already-stored GDELT pages until country harvest flushes.\n";
    }
    auto topic_counts = stored_topic_counts(store, dataset_id);
    std::cerr << "Crawling GDELT (United States / Germany / Australia, 1000 each). Already stored="
              << already.size() << " per-topic cap=" << cfg.gdelt_max_pages
              << " USA=" << topic_count_of(topic_counts, SHARED_TOPIC_USA)
              << " Germany=" << topic_count_of(topic_counts, SHARED_TOPIC_GERMANY)
              << " Australia=" << topic_count_of(topic_counts, SHARED_TOPIC_AUSTRALIA) << "\n";
    int persisted = 0;
    GdeltCrawlStats stats;
    try {
        stats = crawl_gdelt(cfg, already, [&](const GdeltPage& page) {
            if (!is_shared_country_topic(page.topic)) return;
            add_document(store, *dataset, page.title, page.text, page.url, GDELT_SOURCE,
                         page.topic, page.published_at, page.published_at);
            persisted++;
            if (persisted % cfg.gdelt_flush_every == 0) {
                std::cerr << "GDELT progress: " << persisted << " new pages this run, "
                          << store.count_docs(dataset_id) << " stored. Analyzing incrementally.\n";
                try {
                    analyze_dataset(store, cfg, dataset_id, true);
                } catch (const std::exception& e) {
                    std::cerr << "GDELT incremental analysis failed: " << e.what() << "\n";
                }
            }
        }, topic_counts);
    } catch (const std::exception& e) {
        std::cerr << "GDELT crawl failed: " << e.what() << "\n";
        auto ds = store.get_dataset(dataset_id);
        if (ds) {
            ds->analysis_state = "error";
            store.save_dataset(*ds);
        }
        return;
    }
    long stored = store.count_docs(dataset_id);
    auto ds = store.get_dataset(dataset_id);
    if (ds) {
        ds->analysis_state = stored > 0 ? "ingested" : "error";
        store.save_dataset(*ds);
    }
    if (!stats.error.empty()) std::cerr << stats.error << "\n";
    std::cerr << "GDELT crawl finished: " << stored << " pages stored (" << persisted << " new).\n";
    if (stored > 0) {
        try {
            analyze_dataset(store, cfg, dataset_id, false);
        } catch (const std::exception& e) {
            std::cerr << "GDELT analysis failed: " << e.what() << "\n";
        }
    }
}

void seed_gdelt(Store& store, const Config& cfg) {
    if (!cfg.seed_gdelt) return;
    auto existing = store.find_by_kind_and_name(KIND_GDELT, GDELT_DATASET_NAME);
    if (!existing) existing = store.find_by_name_ignore_case(GDELT_DATASET_NAME);
    if (existing) {
        if (!iequals(trim(existing->name), GDELT_DATASET_NAME) || existing->kind != KIND_GDELT) {
            existing->name = GDELT_DATASET_NAME;
            existing->kind = KIND_GDELT;
            store.save_dataset(*existing);
        }
        int per_topic = std::max(cfg.gdelt_max_pages, WIKI_MIN_COUNTRY_PAGES);
        if (!country_topics_incomplete(stored_topic_counts(store, existing->id), per_topic)) {
            std::cerr << "GDELT country topics already stored (" << store.count_docs(existing->id)
                      << " pages total).\n";
            int copied = 0;
            for (const auto& doc : store.docs_by_dataset(existing->id, false)) {
                if (!doc.created_at.empty() || doc.published_at.empty()) continue;
                copied += store.update_created_at_if_null(doc.id, doc.published_at);
            }
            if (copied) std::cerr << "Copied GDELT article dates onto createdAt for " << copied << " documents.\n";
            if (store.count_unscored(existing->id) > 0) {
                try { analyze_dataset(store, cfg, existing->id, true); } catch (...) {}
            }
            return;
        }
    }
    if (!cfg.gdelt_crawl) {
        if (existing) {
            for (const auto& doc : store.docs_by_dataset(existing->id, false)) {
                if (!doc.created_at.empty() || doc.published_at.empty()) continue;
                store.update_created_at_if_null(doc.id, doc.published_at);
            }
            try { analyze_dataset(store, cfg, existing->id, true); } catch (...) {}
        }
        return;
    }
    Dataset dataset = existing ? *existing : create_dataset(store, GDELT_DATASET_NAME, KIND_GDELT);
    dataset.analysis_state = "crawling";
    store.save_dataset(dataset);
    std::thread([store_ptr = &store, cfg, id = dataset.id]() {
        try {
            crawl_gdelt_and_analyze(*store_ptr, cfg, id);
        } catch (const std::exception& e) {
            std::cerr << "GDELT crawl thread failed: " << e.what() << "\n";
        } catch (...) {
            std::cerr << "GDELT crawl thread failed with an unknown error.\n";
        }
    }).detach();
    std::cerr << "Started GDELT crawl in the background (maxPages=" << cfg.gdelt_max_pages
              << ", depth=" << cfg.gdelt_max_depth << ").\n";
}

}  // namespace kos

