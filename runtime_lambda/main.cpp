// Create or update an AWS Lambda function from a package already stored in S3.
//
// This is the C++ form of the behavior described by
// https://github.com/shrutiebony/RuntimeLambdaUsingS3Bucket
// (a Java AWS CDK sample whose README says it deploys a jar from S3 at runtime).
// CDK has no C++ language, so this program calls the Lambda and IAM APIs directly.
//
// Credentials: AWS_ACCESS_KEY_ID, AWS_SECRET_ACCESS_KEY, optional AWS_SESSION_TOKEN,
// and AWS_REGION or AWS_DEFAULT_REGION. If those are unset, the shared AWS credentials
// and config files are used.
//
//   runtime_lambda deploy --bucket my-bucket --key app.jar --function demo \
//       --handler com.example.Handler::handleRequest
//   runtime_lambda --self-test

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iostream>
#include <map>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winhttp.h>
#else
#define CPPHTTPLIB_OPENSSL_SUPPORT
#include "httplib.h"
#endif

namespace {

constexpr uint32_t rotr(uint32_t x, uint32_t n) { return (x >> n) | (x << (32 - n)); }

constexpr uint32_t kSha[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

class Sha256 {
public:
    Sha256() { reset(); }

    void update(const uint8_t* data, size_t len) {
        bits_ += static_cast<uint64_t>(len) * 8;
        while (len > 0) {
            size_t take = std::min(len, sizeof(buf_) - n_);
            std::memcpy(buf_ + n_, data, take);
            n_ += take;
            data += take;
            len -= take;
            if (n_ == sizeof(buf_)) {
                transform(buf_);
                n_ = 0;
            }
        }
    }

    void update(const std::string& s) { update(reinterpret_cast<const uint8_t*>(s.data()), s.size()); }

    std::array<uint8_t, 32> digest() const {
        Sha256 copy = *this;
        uint64_t bits = bits_;
        copy.buf_[copy.n_++] = 0x80;
        if (copy.n_ > 56) {
            while (copy.n_ < 64) copy.buf_[copy.n_++] = 0;
            copy.transform(copy.buf_);
            copy.n_ = 0;
        }
        while (copy.n_ < 56) copy.buf_[copy.n_++] = 0;
        for (int i = 7; i >= 0; --i) copy.buf_[copy.n_++] = static_cast<uint8_t>(bits >> (i * 8));
        copy.transform(copy.buf_);
        std::array<uint8_t, 32> out{};
        for (int i = 0; i < 8; ++i) {
            out[i * 4] = static_cast<uint8_t>(copy.h_[i] >> 24);
            out[i * 4 + 1] = static_cast<uint8_t>(copy.h_[i] >> 16);
            out[i * 4 + 2] = static_cast<uint8_t>(copy.h_[i] >> 8);
            out[i * 4 + 3] = static_cast<uint8_t>(copy.h_[i]);
        }
        return out;
    }

private:
    void reset() {
        h_[0] = 0x6a09e667;
        h_[1] = 0xbb67ae85;
        h_[2] = 0x3c6ef372;
        h_[3] = 0xa54ff53a;
        h_[4] = 0x510e527f;
        h_[5] = 0x9b05688c;
        h_[6] = 0x1f83d9ab;
        h_[7] = 0x5be0cd19;
        bits_ = 0;
        n_ = 0;
    }

    void transform(const uint8_t block[64]) {
        uint32_t w[64];
        for (int i = 0; i < 16; ++i) {
            w[i] = (static_cast<uint32_t>(block[i * 4]) << 24) | (static_cast<uint32_t>(block[i * 4 + 1]) << 16) |
                   (static_cast<uint32_t>(block[i * 4 + 2]) << 8) | static_cast<uint32_t>(block[i * 4 + 3]);
        }
        for (int i = 16; i < 64; ++i) {
            uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = h_[0], b = h_[1], c = h_[2], d = h_[3], e = h_[4], f = h_[5], g = h_[6], h = h_[7];
        for (int i = 0; i < 64; ++i) {
            uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
            uint32_t ch = (e & f) ^ ((~e) & g);
            uint32_t temp1 = h + s1 + ch + kSha[i] + w[i];
            uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
            uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            uint32_t temp2 = s0 + maj;
            h = g;
            g = f;
            f = e;
            e = d + temp1;
            d = c;
            c = b;
            b = a;
            a = temp1 + temp2;
        }
        h_[0] += a;
        h_[1] += b;
        h_[2] += c;
        h_[3] += d;
        h_[4] += e;
        h_[5] += f;
        h_[6] += g;
        h_[7] += h;
    }

    uint32_t h_[8]{};
    uint64_t bits_ = 0;
    uint8_t buf_[64]{};
    size_t n_ = 0;
};

std::string hex_lower(const uint8_t* data, size_t n) {
    static const char* kHex = "0123456789abcdef";
    std::string out(n * 2, '\0');
    for (size_t i = 0; i < n; ++i) {
        out[i * 2] = kHex[data[i] >> 4];
        out[i * 2 + 1] = kHex[data[i] & 0x0f];
    }
    return out;
}

std::string sha256_hex(const std::string& s) {
    Sha256 h;
    h.update(s);
    auto d = h.digest();
    return hex_lower(d.data(), d.size());
}

std::array<uint8_t, 32> hmac_sha256(const uint8_t* key, size_t key_len, const std::string& msg) {
    uint8_t k[64]{};
    if (key_len > 64) {
        Sha256 h;
        h.update(key, key_len);
        auto d = h.digest();
        std::memcpy(k, d.data(), d.size());
    } else {
        std::memcpy(k, key, key_len);
    }
    uint8_t ipad[64], opad[64];
    for (int i = 0; i < 64; ++i) {
        ipad[i] = static_cast<uint8_t>(k[i] ^ 0x36);
        opad[i] = static_cast<uint8_t>(k[i] ^ 0x5c);
    }
    Sha256 inner;
    inner.update(ipad, 64);
    inner.update(msg);
    auto inner_dig = inner.digest();
    Sha256 outer;
    outer.update(opad, 64);
    outer.update(inner_dig.data(), inner_dig.size());
    return outer.digest();
}

std::array<uint8_t, 32> hmac_sha256(const std::string& key, const std::string& msg) {
    return hmac_sha256(reinterpret_cast<const uint8_t*>(key.data()), key.size(), msg);
}

std::array<uint8_t, 32> hmac_sha256(const std::array<uint8_t, 32>& key, const std::string& msg) {
    return hmac_sha256(key.data(), key.size(), msg);
}

std::string uri_escape(const std::string& s) {
    static const char* kHex = "0123456789ABCDEF";
    std::string out;
    out.reserve(s.size() * 3);
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out.push_back(static_cast<char>(c));
        } else {
            out.push_back('%');
            out.push_back(kHex[c >> 4]);
            out.push_back(kHex[c & 0x0f]);
        }
    }
    return out;
}

std::string canonical_uri(const std::string& path) {
    if (path.empty() || path == "/") return "/";
    std::string out;
    size_t i = 0;
    while (i < path.size()) {
        if (path[i] == '/') {
            out.push_back('/');
            ++i;
            continue;
        }
        size_t j = path.find('/', i);
        if (j == std::string::npos) j = path.size();
        out += uri_escape(path.substr(i, j - i));
        i = j;
    }
    return out;
}

std::string json_escape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (unsigned char c : s) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out.push_back(static_cast<char>(c));
                }
        }
    }
    return out;
}

std::string trim(const std::string& s) {
    size_t a = 0;
    while (a < s.size() && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    size_t b = s.size();
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

std::string xml_tag(const std::string& xml, const std::string& tag) {
    std::string open = "<" + tag + ">";
    std::string close = "</" + tag + ">";
    auto p = xml.find(open);
    if (p == std::string::npos) return "";
    p += open.size();
    auto e = xml.find(close, p);
    if (e == std::string::npos) return "";
    return xml.substr(p, e - p);
}

std::optional<std::string> json_string_field(const std::string& body, const std::string& key) {
    const std::string pat = "\"" + key + "\"";
    auto p = body.find(pat);
    if (p == std::string::npos) return std::nullopt;
    p = body.find(':', p + pat.size());
    if (p == std::string::npos) return std::nullopt;
    p = body.find('"', p + 1);
    if (p == std::string::npos) return std::nullopt;
    std::string out;
    for (size_t i = p + 1; i < body.size(); ++i) {
        char c = body[i];
        if (c == '\\' && i + 1 < body.size()) {
            char n = body[++i];
            if (n == 'n') out.push_back('\n');
            else if (n == 'r') out.push_back('\r');
            else if (n == 't') out.push_back('\t');
            else if (n == 'u' && i + 4 < body.size()) i += 4;
            else out.push_back(n);
        } else if (c == '"') {
            return out;
        } else {
            out.push_back(c);
        }
    }
    return std::nullopt;
}

struct Creds {
    std::string access_key;
    std::string secret_key;
    std::string session_token;
    std::string region;
};

std::map<std::string, std::string> ini_section(const std::string& path, const std::string& section) {
    std::map<std::string, std::string> out;
    std::ifstream in(path);
    if (!in) return out;
    bool in_section = false;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        auto hash = line.find('#');
        if (hash != std::string::npos) line = line.substr(0, hash);
        line = trim(line);
        if (line.empty()) continue;
        if (line.front() == '[' && line.back() == ']') {
            in_section = line.substr(1, line.size() - 2) == section;
            continue;
        }
        if (!in_section) continue;
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = trim(line.substr(0, eq));
        std::string v = trim(line.substr(eq + 1));
        if (v.size() >= 2 && ((v.front() == '"' && v.back() == '"') || (v.front() == '\'' && v.back() == '\''))) {
            v = v.substr(1, v.size() - 2);
        }
        out[k] = v;
    }
    return out;
}

std::string home_dir() {
#ifdef _WIN32
    const char* home = std::getenv("USERPROFILE");
#else
    const char* home = std::getenv("HOME");
#endif
    return home ? home : "";
}

Creds load_creds() {
    Creds c;
    if (const char* v = std::getenv("AWS_ACCESS_KEY_ID")) c.access_key = v;
    if (const char* v = std::getenv("AWS_SECRET_ACCESS_KEY")) c.secret_key = v;
    if (const char* v = std::getenv("AWS_SESSION_TOKEN")) c.session_token = v;
    if (const char* v = std::getenv("AWS_REGION")) c.region = v;
    if (c.region.empty()) {
        if (const char* v = std::getenv("AWS_DEFAULT_REGION")) c.region = v;
    }
    std::string profile = "default";
    if (const char* v = std::getenv("AWS_PROFILE")) profile = v;
    std::string home = home_dir();
    if (!home.empty() && (c.access_key.empty() || c.secret_key.empty())) {
        auto cred = ini_section(home + "/.aws/credentials", profile);
        if (c.access_key.empty()) c.access_key = cred["aws_access_key_id"];
        if (c.secret_key.empty()) c.secret_key = cred["aws_secret_access_key"];
        if (c.session_token.empty()) c.session_token = cred["aws_session_token"];
        if (c.region.empty()) c.region = cred["region"];
    }
    if (!home.empty() && c.region.empty()) {
        auto cfg = ini_section(home + "/.aws/config", profile == "default" ? "default" : "profile " + profile);
        c.region = cfg["region"];
    }
    if (c.region.empty()) c.region = "us-east-1";
    return c;
}

std::string amz_date_now() {
    std::time_t t = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y%m%dT%H%M%SZ", &tm);
    return buf;
}

struct SignedRequest {
    std::string authorization;
    std::string amz_date;
    std::string payload_hash;
    std::string canonical_uri;
};

SignedRequest sign_request(const std::string& method, const std::string& uri_path, const std::string& host,
                           const std::string& region, const std::string& service, const std::string& content_type,
                           const std::string& body, const Creds& creds, bool include_payload_hash_header,
                           const std::string& amz_date_override = "", const std::string& query = "") {
    SignedRequest signed_req;
    signed_req.amz_date = amz_date_override.empty() ? amz_date_now() : amz_date_override;
    signed_req.payload_hash = sha256_hex(body);
    signed_req.canonical_uri = canonical_uri(uri_path);
    std::string date_stamp = signed_req.amz_date.substr(0, 8);

    std::vector<std::pair<std::string, std::string>> headers;
    if (!content_type.empty()) headers.emplace_back("content-type", content_type);
    headers.emplace_back("host", host);
    headers.emplace_back("x-amz-date", signed_req.amz_date);
    if (include_payload_hash_header) headers.emplace_back("x-amz-content-sha256", signed_req.payload_hash);
    if (!creds.session_token.empty()) headers.emplace_back("x-amz-security-token", creds.session_token);
    std::sort(headers.begin(), headers.end());

    std::ostringstream canon;
    canon << method << "\n" << signed_req.canonical_uri << "\n" << query << "\n";
    std::string signed_headers;
    for (const auto& h : headers) {
        canon << h.first << ":" << h.second << "\n";
        if (!signed_headers.empty()) signed_headers += ";";
        signed_headers += h.first;
    }
    canon << "\n" << signed_headers << "\n" << signed_req.payload_hash;
    std::string canonical = canon.str();

    std::string scope = date_stamp + "/" + region + "/" + service + "/aws4_request";
    std::string to_sign = "AWS4-HMAC-SHA256\n" + signed_req.amz_date + "\n" + scope + "\n" + sha256_hex(canonical);
    auto k_date = hmac_sha256("AWS4" + creds.secret_key, date_stamp);
    auto k_region = hmac_sha256(k_date, region);
    auto k_service = hmac_sha256(k_region, service);
    auto k_signing = hmac_sha256(k_service, "aws4_request");
    auto sig = hmac_sha256(k_signing, to_sign);
    signed_req.authorization = "AWS4-HMAC-SHA256 Credential=" + creds.access_key + "/" + scope +
                               ", SignedHeaders=" + signed_headers + ", Signature=" + hex_lower(sig.data(), sig.size());
    return signed_req;
}

struct HttpResult {
    int status = 0;
    std::string body;
    std::string error;
    std::string error_type;
};

#ifdef _WIN32

std::wstring wide(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

std::string narrow(const std::wstring& w) {
    if (w.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

std::string header_by_name(HINTERNET req, const wchar_t* name) {
    DWORD sz = 0;
    WinHttpQueryHeaders(req, WINHTTP_QUERY_CUSTOM, name, WINHTTP_NO_OUTPUT_BUFFER, &sz, WINHTTP_NO_HEADER_INDEX);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || sz == 0) return "";
    std::wstring buf(sz / sizeof(wchar_t), L'\0');
    if (!WinHttpQueryHeaders(req, WINHTTP_QUERY_CUSTOM, name, buf.data(), &sz, WINHTTP_NO_HEADER_INDEX)) return "";
    if (!buf.empty() && buf.back() == L'\0') buf.pop_back();
    return narrow(buf);
}

HttpResult http_call(const std::string& method, const std::string& host, const std::string& uri,
                     const std::vector<std::pair<std::string, std::string>>& headers, const std::string& body) {
    HttpResult out;
    HINTERNET session = WinHttpOpen(L"runtime-lambda/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
                                    WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) {
        out.error = "WinHttpOpen failed";
        return out;
    }
    WinHttpSetTimeouts(session, 20000, 20000, 60000, 60000);
    HINTERNET connect = WinHttpConnect(session, wide(host).c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!connect) {
        out.error = "connect failed";
        WinHttpCloseHandle(session);
        return out;
    }
    HINTERNET req = WinHttpOpenRequest(connect, wide(method).c_str(), wide(uri).c_str(), nullptr, WINHTTP_NO_REFERER,
                                       WINHTTP_NO_ADDITIONAL_HEADERS, WINHTTP_FLAG_SECURE);
    if (!req) {
        out.error = "open request failed";
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return out;
    }
    DWORD policy = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
    WinHttpSetOption(req, WINHTTP_OPTION_REDIRECT_POLICY, &policy, sizeof(policy));
    for (const auto& h : headers) {
        std::wstring line = wide(h.first + ": " + h.second);
        WinHttpAddRequestHeaders(req, line.c_str(), static_cast<DWORD>(-1), WINHTTP_ADDREQ_FLAG_ADD);
    }
    LPVOID payload = body.empty() ? WINHTTP_NO_REQUEST_DATA : const_cast<char*>(body.data());
    DWORD plen = static_cast<DWORD>(body.size());
    if (!WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0, payload, plen, plen, 0) ||
        !WinHttpReceiveResponse(req, nullptr)) {
        out.error = "request failed";
        WinHttpCloseHandle(req);
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return out;
    }
    DWORD status = 0, slen = sizeof(status);
    WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status,
                        &slen, WINHTTP_NO_HEADER_INDEX);
    out.status = static_cast<int>(status);
    out.error_type = header_by_name(req, L"x-amzn-ErrorType");
    std::string accum;
    DWORD avail = 0;
    while (WinHttpQueryDataAvailable(req, &avail) && avail > 0) {
        if (accum.size() >= 1024 * 1024) break;
        DWORD chunk = avail;
        std::string buf(chunk, '\0');
        DWORD read = 0;
        if (!WinHttpReadData(req, buf.data(), chunk, &read) || read == 0) break;
        accum.append(buf.data(), read);
    }
    out.body = std::move(accum);
    WinHttpCloseHandle(req);
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    return out;
}

#else

HttpResult http_call(const std::string& method, const std::string& host, const std::string& uri,
                     const std::vector<std::pair<std::string, std::string>>& headers, const std::string& body) {
    HttpResult out;
    httplib::SSLClient cli(host, 443);
    cli.set_connection_timeout(20, 0);
    cli.set_read_timeout(60, 0);
    cli.set_write_timeout(60, 0);
    cli.set_follow_location(false);
    httplib::Headers hdrs;
    for (const auto& h : headers) hdrs.emplace(h.first, h.second);
    httplib::Result res;
    if (method == "GET") res = cli.Get(uri, hdrs);
    else if (method == "HEAD") res = cli.Head(uri, hdrs);
    else if (method == "PUT") res = cli.Put(uri, hdrs, body, "");
    else res = cli.Post(uri, hdrs, body, "");
    if (!res) {
        out.error = "request failed";
        return out;
    }
    out.status = res->status;
    out.body = res->body;
    if (res->has_header("x-amzn-ErrorType")) out.error_type = res->get_header_value("x-amzn-ErrorType");
    return out;
}

#endif

HttpResult aws_call(const std::string& method, const std::string& host, const std::string& uri_path,
                    const std::string& region, const std::string& service, const std::string& content_type,
                    const std::string& body, const Creds& creds, bool s3_payload_header) {
    auto signed_req = sign_request(method, uri_path, host, region, service, content_type, body, creds, s3_payload_header);
    std::vector<std::pair<std::string, std::string>> headers = {
        {"Authorization", signed_req.authorization},
        {"x-amz-date", signed_req.amz_date},
    };
    if (!content_type.empty()) headers.emplace_back("Content-Type", content_type);
    if (s3_payload_header) headers.emplace_back("x-amz-content-sha256", signed_req.payload_hash);
    if (!creds.session_token.empty()) headers.emplace_back("x-amz-security-token", creds.session_token);
    return http_call(method, host, signed_req.canonical_uri, headers, body);
}

std::string aws_message(const HttpResult& r) {
    if (!r.error.empty() && r.status == 0) return r.error;
    if (auto m = json_string_field(r.body, "Message")) return *m;
    if (auto m = json_string_field(r.body, "message")) return *m;
    std::string xml = xml_tag(r.body, "Message");
    if (!xml.empty()) return xml;
    if (!r.error_type.empty()) return r.error_type;
    if (r.body.empty()) return "HTTP " + std::to_string(r.status);
    return r.body.size() > 500 ? r.body.substr(0, 500) : r.body;
}

bool error_is(const HttpResult& r, const std::string& name) {
    if (r.error_type.find(name) != std::string::npos) return true;
    return r.body.find(name) != std::string::npos;
}

std::string role_name_for(const std::string& function) {
    std::string name;
    for (unsigned char c : function) {
        if (std::isalnum(c) || c == '+' || c == '=' || c == ',' || c == '.' || c == '@' || c == '_' || c == '-') {
            name.push_back(static_cast<char>(c));
        } else {
            name.push_back('-');
        }
    }
    if (name.empty()) name = "runtime-lambda";
    const std::string suffix = "-exec";
    if (name.size() + suffix.size() > 64) name.resize(64 - suffix.size());
    return name + suffix;
}

std::string ensure_role(const Creds& creds, const std::string& role_name) {
    const std::string policy =
        "{\"Version\":\"2012-10-17\",\"Statement\":[{\"Effect\":\"Allow\",\"Principal\":{\"Service\":\"lambda.amazonaws.com\"},"
        "\"Action\":\"sts:AssumeRole\"}]}";
    std::string create_body = "Action=CreateRole&AssumeRolePolicyDocument=" + uri_escape(policy) +
                              "&RoleName=" + uri_escape(role_name) + "&Version=2010-05-08";
    auto created = aws_call("POST", "iam.amazonaws.com", "/", "us-east-1", "iam", "application/x-www-form-urlencoded",
                            create_body, creds, false);
    std::string arn = xml_tag(created.body, "Arn");
    if (arn.empty()) {
        std::string code = xml_tag(created.body, "Code");
        if (code != "EntityAlreadyExists") {
            throw std::runtime_error("CreateRole failed: " + aws_message(created));
        }
        std::string get_body = "Action=GetRole&RoleName=" + uri_escape(role_name) + "&Version=2010-05-08";
        auto got = aws_call("POST", "iam.amazonaws.com", "/", "us-east-1", "iam", "application/x-www-form-urlencoded", get_body,
                            creds, false);
        arn = xml_tag(got.body, "Arn");
        if (arn.empty()) throw std::runtime_error("GetRole failed: " + aws_message(got));
    }
    const std::string basic = "arn:aws:iam::aws:policy/service-role/AWSLambdaBasicExecutionRole";
    std::string attach_body = "Action=AttachRolePolicy&PolicyArn=" + uri_escape(basic) + "&RoleName=" + uri_escape(role_name) +
                              "&Version=2010-05-08";
    auto attached = aws_call("POST", "iam.amazonaws.com", "/", "us-east-1", "iam", "application/x-www-form-urlencoded",
                             attach_body, creds, false);
    if (attached.status < 200 || attached.status >= 300 || xml_tag(attached.body, "Code").size() > 0) {
        std::string code = xml_tag(attached.body, "Code");
        if (!code.empty()) throw std::runtime_error("AttachRolePolicy failed: " + aws_message(attached));
    }
    return arn;
}

void require_object(const Creds& creds, const std::string& bucket, const std::string& key) {
    std::string host = "s3." + creds.region + ".amazonaws.com";
    std::string path = "/" + bucket + "/" + key;
    auto head = aws_call("HEAD", host, path, creds.region, "s3", "", "", creds, true);
    if (head.status == 200) return;
    if (head.status == 404 || error_is(head, "NoSuchKey") || error_is(head, "NotFound")) {
        throw std::runtime_error("S3 object s3://" + bucket + "/" + key + " was not found. Put the package in the bucket first.");
    }
    if (head.status == 301 || head.status == 307) {
        throw std::runtime_error("S3 bucket region does not match " + creds.region + ".");
    }
    throw std::runtime_error("Could not read s3://" + bucket + "/" + key + ": " + aws_message(head));
}

struct DeployOptions {
    std::string bucket;
    std::string key;
    std::string function;
    std::string handler;
    std::string runtime = "java17";
    std::string role;
    std::string description;
    int memory = 512;
    int timeout = 30;
};

std::string function_code_json(const DeployOptions& opt, bool full) {
    std::ostringstream body;
    if (full) {
        body << "{\"FunctionName\":\"" << json_escape(opt.function) << "\",\"Runtime\":\"" << json_escape(opt.runtime)
             << "\",\"Role\":\"" << json_escape(opt.role) << "\",\"Handler\":\"" << json_escape(opt.handler)
             << "\",\"Description\":\"" << json_escape(opt.description) << "\",\"Timeout\":" << opt.timeout
             << ",\"MemorySize\":" << opt.memory << ",\"Publish\":true,\"Code\":{\"S3Bucket\":\"" << json_escape(opt.bucket)
             << "\",\"S3Key\":\"" << json_escape(opt.key) << "\"}}";
    } else {
        body << "{\"S3Bucket\":\"" << json_escape(opt.bucket) << "\",\"S3Key\":\"" << json_escape(opt.key)
             << "\",\"Publish\":true}";
    }
    return body.str();
}

bool retryable(const HttpResult& r) {
    if (r.status == 429 || r.status == 503) return true;
    if (error_is(r, "ResourceConflictException")) return true;
    std::string msg = aws_message(r);
    return msg.find("cannot be assumed") != std::string::npos || msg.find("AssumeRole") != std::string::npos;
}

void deploy(const Creds& creds, DeployOptions opt) {
    if (!opt.key.empty() && opt.key.front() == '/') opt.key.erase(opt.key.begin());
    require_object(creds, opt.bucket, opt.key);
    if (opt.role.empty()) {
        std::string name = role_name_for(opt.function);
        std::cout << "ensuring IAM role " << name << "\n";
        opt.role = ensure_role(creds, name);
        std::cout << "role " << opt.role << "\n";
    }
    std::string host = "lambda." + creds.region + ".amazonaws.com";
    std::string get_path = "/2015-03-31/functions/" + opt.function;
    HttpResult done;
    bool created = false;
    for (int attempt = 1; attempt <= 12; ++attempt) {
        auto existing = aws_call("GET", host, get_path, creds.region, "lambda", "", "", creds, false);
        bool missing = existing.status == 404 || error_is(existing, "ResourceNotFoundException");
        if (!missing && existing.status != 200) {
            if (retryable(existing) && attempt < 12) {
                std::this_thread::sleep_for(std::chrono::seconds(2));
                continue;
            }
            throw std::runtime_error("GetFunction failed: " + aws_message(existing));
        }
        if (missing) {
            auto created_res = aws_call("POST", host, "/2015-03-31/functions", creds.region, "lambda", "application/json",
                                        function_code_json(opt, true), creds, false);
            if (created_res.status == 201 || created_res.status == 200) {
                done = created_res;
                created = true;
                break;
            }
            if (error_is(created_res, "ResourceConflictException") && attempt < 12) {
                std::this_thread::sleep_for(std::chrono::seconds(2));
                continue;
            }
            if (retryable(created_res) && attempt < 12) {
                std::this_thread::sleep_for(std::chrono::seconds(2));
                continue;
            }
            throw std::runtime_error("CreateFunction failed: " + aws_message(created_res));
        }
        auto updated = aws_call("PUT", host, get_path + "/code", creds.region, "lambda", "application/json",
                                function_code_json(opt, false), creds, false);
        if (updated.status == 200) {
            done = updated;
            break;
        }
        if (retryable(updated) && attempt < 12) {
            std::this_thread::sleep_for(std::chrono::seconds(2));
            continue;
        }
        throw std::runtime_error("UpdateFunctionCode failed: " + aws_message(updated));
    }
    if (done.status == 0) throw std::runtime_error("Lambda did not become ready.");
    auto arn = json_string_field(done.body, "FunctionArn");
    std::cout << (created ? "created" : "updated") << " function " << (arn ? *arn : opt.function) << "\n";
    std::cout << "code s3://" << opt.bucket << "/" << opt.key << "\n";
    std::cout << "runtime " << opt.runtime << " handler " << opt.handler << "\n";
}

void usage() {
    std::cerr
        << "Usage:\n"
        << "  runtime_lambda deploy --bucket BUCKET --key OBJECT_KEY --function NAME --handler HANDLER\n"
        << "      [--runtime java17] [--role ROLE_ARN] [--region REGION] [--memory 512] [--timeout 30]\n"
        << "      [--description TEXT]\n"
        << "  runtime_lambda --self-test\n\n"
        << "The object must already be in S3 (a Java jar, or a zip for a custom runtime).\n"
        << "Without --role, an IAM role named <function>-exec is created for Lambda.\n"
        << "Example:\n"
        << "  runtime_lambda deploy --bucket builds --key app.jar --function demo \\\n"
        << "      --handler com.example.Handler::handleRequest --runtime java17\n";
}

int self_test() {
    if (sha256_hex("") != "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855") {
        std::cerr << "sha256 empty mismatch\n";
        return 1;
    }
    if (sha256_hex("abc") != "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad") {
        std::cerr << "sha256 abc mismatch\n";
        return 1;
    }
    Creds creds;
    creds.access_key = "AKIDEXAMPLE";
    creds.secret_key = "wJalrXUtnFEMI/K7MDENG+bPxRfiCYEXAMPLEKEY";
    auto signed_req =
        sign_request("GET", "/", "iam.amazonaws.com", "us-east-1", "iam",
                     "application/x-www-form-urlencoded; charset=utf-8", "", creds, false, "20150830T123600Z",
                     "Action=ListUsers&Version=2010-05-08");
    const std::string expected_sig = "5d672d79c15b13162d9279b0855cfba6789a8edb4c82c400e06b5924a6f2b5d7";
    if (signed_req.authorization.find("Signature=" + expected_sig) == std::string::npos) {
        std::cerr << "sigv4 example mismatch: " << signed_req.authorization << "\n";
        return 1;
    }
    if (canonical_uri("/2015-03-31/functions/My Func/code") != "/2015-03-31/functions/My%20Func/code") {
        std::cerr << "uri escape mismatch\n";
        return 1;
    }
    std::cout << "self-test ok\n";
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        std::vector<std::string> args;
        std::map<std::string, std::string> flags;
        for (int i = 1; i < argc; ++i) {
            std::string a = argv[i];
            if (a == "--self-test") return self_test();
            if (a == "--help" || a == "-h") {
                usage();
                return 0;
            }
            if (a.rfind("--", 0) == 0) {
                if (i + 1 >= argc) {
                    usage();
                    return 2;
                }
                flags[a.substr(2)] = argv[++i];
            } else {
                args.push_back(a);
            }
        }
        if (args.size() != 1 || args[0] != "deploy") {
            usage();
            return 2;
        }
        DeployOptions opt;
        opt.bucket = flags["bucket"];
        opt.key = flags["key"];
        opt.function = flags["function"];
        opt.handler = flags["handler"];
        if (flags.count("runtime")) opt.runtime = flags["runtime"];
        if (flags.count("role")) opt.role = flags["role"];
        if (flags.count("description")) opt.description = flags["description"];
        if (flags.count("memory")) opt.memory = std::stoi(flags["memory"]);
        if (flags.count("timeout")) opt.timeout = std::stoi(flags["timeout"]);
        if (opt.bucket.empty() || opt.key.empty() || opt.function.empty() || opt.handler.empty()) {
            usage();
            return 2;
        }
        if (opt.memory < 128 || opt.memory > 10240 || opt.timeout < 1 || opt.timeout > 900) {
            std::cerr << "memory must be 128..10240 and timeout 1..900\n";
            return 2;
        }
        Creds creds = load_creds();
        if (flags.count("region")) creds.region = flags["region"];
        if (creds.access_key.empty() || creds.secret_key.empty()) {
            std::cerr << "Set AWS_ACCESS_KEY_ID and AWS_SECRET_ACCESS_KEY, or configure ~/.aws/credentials.\n";
            return 2;
        }
        deploy(creds, opt);
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << ex.what() << "\n";
        return 1;
    }
}
