#include "html.hpp"
#include "util.hpp"

#include <cctype>

namespace kos {

static bool is_name_char(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == ':' || c == '_';
}

static std::string lower_copy(std::string s) { return ascii_lower(s); }

ParsedHtml parse_html(const std::string& html) {
    ParsedHtml out;
    struct Frame {
        std::string tag;
        std::string id;
        std::string cls;
        bool skip = false;
        bool wiki_content = false;
        bool catlinks = false;
        bool lastmod = false;
    };
    std::vector<Frame> stack;
    bool in_title = false;
    int skip_depth = 0;
    std::string title_buf, body_buf, wiki_buf, footer_buf;
    bool any_wiki = false;

    auto in_wiki = [&]() {
        for (const auto& f : stack) if (f.wiki_content) return true;
        return false;
    };
    auto in_cats = [&]() {
        for (const auto& f : stack) if (f.catlinks) return true;
        return false;
    };
    auto in_lastmod = [&]() {
        for (const auto& f : stack) if (f.lastmod) return true;
        return false;
    };

    auto append_text = [&](const std::string& t) {
        if (t.empty()) return;
        std::string dec = html_unescape(t);
        if (in_title) title_buf += dec;
        if (skip_depth > 0) return;
        if (!body_buf.empty() && !std::isspace(static_cast<unsigned char>(body_buf.back())) &&
            !std::isspace(static_cast<unsigned char>(dec[0]))) {
            body_buf.push_back(' ');
        }
        body_buf += dec;
        if (in_wiki()) {
            any_wiki = true;
            if (!wiki_buf.empty() && !std::isspace(static_cast<unsigned char>(wiki_buf.back())) &&
                !std::isspace(static_cast<unsigned char>(dec[0]))) {
                wiki_buf.push_back(' ');
            }
            wiki_buf += dec;
        }
        if (in_lastmod()) footer_buf += dec;
    };

    size_t i = 0;
    const size_t n = html.size();
    while (i < n) {
        if (html[i] == '<') {
            if (i + 3 < n && html.compare(i, 4, "<!--") == 0) {
                auto end = html.find("-->", i + 4);
                i = end == std::string::npos ? n : end + 3;
                continue;
            }
            size_t j = i + 1;
            bool closing = false;
            if (j < n && html[j] == '/') {
                closing = true;
                ++j;
            }
            size_t tag_start = j;
            while (j < n && is_name_char(html[j])) ++j;
            std::string tag = lower_copy(html.substr(tag_start, j - tag_start));
            std::string attrs;
            size_t attr_start = j;
            bool self = false;
            if (tag == "script" || tag == "style" || tag == "noscript") {
                // find matching close, but still parse attributes first
            }
            while (j < n && html[j] != '>') {
                if (html[j] == '/' && j + 1 < n && html[j + 1] == '>') self = true;
                ++j;
            }
            attrs = html.substr(attr_start, j - attr_start);
            if (j < n) ++j;  // skip '>'

            auto attr = [&](const char* name) -> std::string {
                std::string needle = std::string(name) + "=";
                std::string al = ascii_lower(attrs);
                auto p = al.find(needle);
                if (p == std::string::npos) return "";
                size_t v = p + needle.size();
                while (v < attrs.size() && std::isspace(static_cast<unsigned char>(attrs[v]))) ++v;
                if (v >= attrs.size()) return "";
                char q = attrs[v];
                if (q == '"' || q == '\'') {
                    auto e = attrs.find(q, v + 1);
                    if (e == std::string::npos) return "";
                    return html_unescape(attrs.substr(v + 1, e - v - 1));
                }
                size_t e = v;
                while (e < attrs.size() && !std::isspace(static_cast<unsigned char>(attrs[e])) && attrs[e] != '/' &&
                       attrs[e] != '>')
                    ++e;
                return html_unescape(attrs.substr(v, e - v));
            };

            if (tag == "script" || tag == "style" || tag == "noscript") {
                if (!closing) {
                    std::string close = "</" + tag;
                    auto end = ascii_lower(html.substr(j)).find(close);
                    i = end == std::string::npos ? n : j + end;
                    auto gt = html.find('>', i);
                    i = gt == std::string::npos ? n : gt + 1;
                    continue;
                }
            }

            if (!closing && (tag == "br" || tag == "p" || tag == "div" || tag == "li" || tag == "tr" || tag == "h1" ||
                             tag == "h2" || tag == "h3" || tag == "h4")) {
                append_text(" ");
            }

            if (closing) {
                if (tag == "title") in_title = false;
                if (!stack.empty() && stack.back().tag == tag) stack.pop_back();
                else {
                    for (int k = static_cast<int>(stack.size()) - 1; k >= 0; --k) {
                        if (stack[k].tag == tag) {
                            stack.resize(static_cast<size_t>(k));
                            break;
                        }
                    }
                }
                i = j;
                continue;
            }

            std::string id = attr("id");
            std::string cls = attr("class");
            std::string href = attr("href");
            std::string idl = ascii_lower(id);
            std::string clsl = ascii_lower(cls);

            if (tag == "a" && !href.empty()) out.hrefs.push_back(href);
            if (tag == "meta") {
                std::string prop = ascii_lower(attr("property"));
                std::string name = ascii_lower(attr("name"));
                std::string content = attr("content");
                if (prop == "article:modified_time" || prop == "og:updated_time" || name == "last-modified" ||
                    prop == "article:published_time") {
                    if (out.last_modified_meta.empty()) out.last_modified_meta = content;
                }
            }
            if (tag == "time") {
                std::string dt = attr("datetime");
                if (!dt.empty() && out.time_datetime.empty()) out.time_datetime = dt;
            }

            bool skip = tag == "script" || tag == "style" || tag == "noscript" ||
                        clsl.find("mw-editsection") != std::string::npos ||
                        (tag == "sup" && clsl.find("reference") != std::string::npos) ||
                        clsl.find("reflist") != std::string::npos ||
                        clsl.find("mw-references-wrap") != std::string::npos ||
                        clsl.find("navbox") != std::string::npos ||
                        clsl.find("vertical-navbox") != std::string::npos ||
                        idl == "toc" || clsl.find("toc") == 0;
            bool wiki = idl == "mw-content-text" || clsl.find("mw-parser-output") != std::string::npos;
            bool cats = idl == "mw-normal-catlinks";
            bool lastmod = idl == "footer-info-lastmod" || clsl.find("mw-last-modified") != std::string::npos ||
                           idl == "mw-revision-date";

            if (tag == "title") in_title = true;
            if (tag == "a" && in_cats()) {
                // category link text collected from following text — mark frame
            }

            if (!self) {
                Frame f;
                f.tag = tag;
                f.id = id;
                f.cls = cls;
                f.skip = skip;
                f.wiki_content = wiki;
                f.catlinks = cats;
                f.lastmod = lastmod;
                stack.push_back(f);
            }

            if (tag == "a" && cats) {
                // handled via text while in cats
            }
            i = j;
            continue;
        }
        size_t j = i;
        while (j < n && html[j] != '<') ++j;
        std::string chunk = html.substr(i, j - i);
        if (in_cats() && !stack.empty() && stack.back().tag == "a") {
            std::string t = trim(html_unescape(chunk));
            if (!t.empty()) out.category_texts.push_back(t);
        }
        append_text(chunk);
        i = j;
    }

    auto collapse = [](std::string s) {
        std::string o;
        bool space = true;
        for (unsigned char c : s) {
            if (std::isspace(c)) {
                if (!space) {
                    o.push_back(' ');
                    space = true;
                }
            } else {
                o.push_back(static_cast<char>(c));
                space = false;
            }
        }
        return trim(o);
    };

    out.title = trim(html_unescape(title_buf));
    out.body_text = collapse(body_buf);
    out.wiki_text = any_wiki ? collapse(wiki_buf) : out.body_text;
    out.footer_blob = collapse(footer_buf);
    if (out.footer_blob.empty()) out.footer_blob = out.body_text;
    return out;
}

std::string strip_tags_text(const std::string& html) {
    return parse_html(html).body_text;
}

}  // namespace kos
