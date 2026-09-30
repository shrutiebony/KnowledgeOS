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

#include "ingest_internal.hpp"

namespace kos {

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

static int wiki_india_count(const std::map<std::string, int>& counts) {
    return topic_count_of(counts, SHARED_TOPIC_INDIA) + topic_count_of(counts, SHARED_TOPIC_GEO_INDIA);
}

static bool wiki_topics_incomplete(const Config& cfg, const std::map<std::string, int>& counts, int per_topic) {
    if (cfg.wikipedia_crawl_india && wiki_india_count(counts) < per_topic) return true;
    if (cfg.wikipedia_crawl_germany && topic_count_of(counts, SHARED_TOPIC_GERMANY) < per_topic) return true;
    if (cfg.wikipedia_crawl_usa && topic_count_of(counts, SHARED_TOPIC_USA) < per_topic) return true;
    if (cfg.wikipedia_crawl_australia && topic_count_of(counts, SHARED_TOPIC_AUSTRALIA) < per_topic) return true;
    return false;
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
    std::cerr << "Crawling English Wikipedia (India / United States / Australia). Already stored="
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
                      << " each enabled country topic (India / United States / Australia).\n";
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

}  // namespace kos
