#include "http_client.hpp"
#include "util.hpp"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")
#else
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
#include "httplib.h"
#endif
#endif

#include <algorithm>

namespace kos {

#ifdef _WIN32

static std::wstring wide(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

static std::string narrow(const std::wstring& w) {
    if (w.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

static std::string header_value(HINTERNET req, DWORD info) {
    DWORD sz = 0;
    WinHttpQueryHeaders(req, info, WINHTTP_HEADER_NAME_BY_INDEX, WINHTTP_NO_OUTPUT_BUFFER, &sz, WINHTTP_NO_HEADER_INDEX);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || sz == 0) return "";
    std::wstring buf(sz / sizeof(wchar_t), L'\0');
    if (!WinHttpQueryHeaders(req, info, WINHTTP_HEADER_NAME_BY_INDEX, buf.data(), &sz, WINHTTP_NO_HEADER_INDEX)) {
        return "";
    }
    if (!buf.empty() && buf.back() == L'\0') buf.pop_back();
    return trim(narrow(buf));
}

static HttpResponse http_get_once(const std::string& url, const std::string& user_agent, int timeout_ms, int max_bytes) {
    HttpResponse out;
    out.final_url = url;
    auto nurl = normalize_url(url);
    if (!nurl) {
        out.error = "invalid url";
        return out;
    }
    URL_COMPONENTS uc{};
    uc.dwStructSize = sizeof(uc);
    uc.dwSchemeLength = static_cast<DWORD>(-1);
    uc.dwHostNameLength = static_cast<DWORD>(-1);
    uc.dwUrlPathLength = static_cast<DWORD>(-1);
    uc.dwExtraInfoLength = static_cast<DWORD>(-1);
    std::wstring wurl = wide(*nurl);
    if (!WinHttpCrackUrl(wurl.c_str(), static_cast<DWORD>(wurl.size()), 0, &uc)) {
        out.error = "url parse failed";
        return out;
    }
    std::wstring host(uc.lpszHostName, uc.dwHostNameLength);
    std::wstring path(uc.lpszUrlPath, uc.dwUrlPathLength);
    if (uc.lpszExtraInfo && uc.dwExtraInfoLength) path.append(uc.lpszExtraInfo, uc.dwExtraInfoLength);
    if (path.empty()) path = L"/";
    INTERNET_PORT port = uc.nPort;
    bool https = uc.nScheme == INTERNET_SCHEME_HTTPS;

    HINTERNET session = WinHttpOpen(wide(user_agent).c_str(), WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) {
        out.error = "WinHttpOpen failed";
        return out;
    }
    int to = std::max(1000, timeout_ms);
    WinHttpSetTimeouts(session, to, to, to, to);
    HINTERNET connect = WinHttpConnect(session, host.c_str(), port, 0);
    if (!connect) {
        out.error = "connect failed";
        WinHttpCloseHandle(session);
        return out;
    }
    DWORD flags = https ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET req = WinHttpOpenRequest(connect, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                       WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!req) {
        out.error = "open request failed";
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return out;
    }
    DWORD policy = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
    WinHttpSetOption(req, WINHTTP_OPTION_REDIRECT_POLICY, &policy, sizeof(policy));
    WinHttpAddRequestHeaders(req, L"Accept: text/html,application/xhtml+xml,application/json,*/*\r\nAccept-Language: en",
                             static_cast<DWORD>(-1), WINHTTP_ADDREQ_FLAG_ADD);
    if (!WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(req, nullptr)) {
        out.error = "request failed";
        WinHttpCloseHandle(req);
        WinHttpCloseHandle(connect);
        WinHttpCloseHandle(session);
        return out;
    }
    DWORD status = 0, slen = sizeof(status);
    WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                        &status, &slen, WINHTTP_NO_HEADER_INDEX);
    out.status = static_cast<int>(status);
    out.content_type = header_value(req, WINHTTP_QUERY_CONTENT_TYPE);
    out.last_modified = header_value(req, WINHTTP_QUERY_LAST_MODIFIED);
    out.location = header_value(req, WINHTTP_QUERY_LOCATION);

    wchar_t final_buf[2048];
    DWORD flen = sizeof(final_buf);
    if (WinHttpQueryOption(req, WINHTTP_OPTION_URL, final_buf, &flen)) {
        out.final_url = narrow(final_buf);
    }

    std::string body;
    DWORD avail = 0;
    while (WinHttpQueryDataAvailable(req, &avail) && avail > 0) {
        if (static_cast<int>(body.size()) >= max_bytes) break;
        DWORD chunk = std::min(avail, static_cast<DWORD>(max_bytes - static_cast<int>(body.size())));
        std::string buf(chunk, '\0');
        DWORD read = 0;
        if (!WinHttpReadData(req, buf.data(), chunk, &read) || read == 0) break;
        body.append(buf.data(), read);
    }
    out.body = std::move(body);
    WinHttpCloseHandle(req);
    WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    return out;
}

#else

static HttpResponse http_get_once(const std::string& url, const std::string& user_agent, int timeout_ms, int max_bytes) {
    HttpResponse out;
    out.final_url = url;
    auto nurl = normalize_url(url);
    if (!nurl) {
        out.error = "invalid url";
        return out;
    }
#ifdef CPPHTTPLIB_OPENSSL_SUPPORT
    auto scheme_end = nurl->find("://");
    std::string scheme = nurl->substr(0, scheme_end);
    std::string rest = nurl->substr(scheme_end + 3);
    auto slash = rest.find('/');
    std::string hostport = slash == std::string::npos ? rest : rest.substr(0, slash);
    std::string path = slash == std::string::npos ? "/" : rest.substr(slash);
    std::string host = hostport;
    int port = scheme == "https" ? 443 : 80;
    auto colon = hostport.rfind(':');
    if (colon != std::string::npos && hostport.find(']') == std::string::npos) {
        host = hostport.substr(0, colon);
        port = std::stoi(hostport.substr(colon + 1));
    }
    httplib::Headers headers = {{"User-Agent", user_agent}, {"Accept-Language", "en"}};
    httplib::Result res;
    if (scheme == "https") {
        httplib::SSLClient cli(host, port);
        cli.set_connection_timeout(timeout_ms / 1000, (timeout_ms % 1000) * 1000);
        cli.set_read_timeout(timeout_ms / 1000, (timeout_ms % 1000) * 1000);
        cli.set_follow_location(true);
        res = cli.Get(path, headers);
    } else {
        httplib::Client cli(host, port);
        cli.set_connection_timeout(timeout_ms / 1000, (timeout_ms % 1000) * 1000);
        cli.set_read_timeout(timeout_ms / 1000, (timeout_ms % 1000) * 1000);
        cli.set_follow_location(true);
        res = cli.Get(path, headers);
    }
    if (!res) {
        out.error = "request failed";
        return out;
    }
    out.status = res->status;
    out.body = res->body.substr(0, static_cast<size_t>(std::max(0, max_bytes)));
    if (res->has_header("Content-Type")) out.content_type = res->get_header_value("Content-Type");
    if (res->has_header("Last-Modified")) out.last_modified = res->get_header_value("Last-Modified");
    if (res->has_header("Location")) out.location = res->get_header_value("Location");
    return out;
#else
    out.error = "HTTPS client not compiled";
    return out;
#endif
}

#endif

HttpResponse http_get(const std::string& url, const std::string& user_agent, int timeout_ms, int max_bytes) {
    std::string current = url;
    HttpResponse out;
    for (int hop = 0; hop < 6; ++hop) {
        out = http_get_once(current, user_agent, timeout_ms, max_bytes);
        if (out.status < 300 || out.status >= 400) return out;
        if (out.location.empty()) return out;
        auto next = resolve_url(out.final_url.empty() ? current : out.final_url, out.location);
        if (!next || *next == current) return out;
        current = *next;
        out.final_url = current;
    }
    return out;
}

}  // namespace kos
