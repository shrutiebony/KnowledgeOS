#pragma once

#include <string>

namespace kos {

struct HttpResponse {
    int status = 0;
    std::string body;
    std::string final_url;
    std::string content_type;
    std::string last_modified;
    std::string location;
    std::string error;
};

HttpResponse http_get(const std::string& url, const std::string& user_agent, int timeout_ms, int max_bytes);

}  // namespace kos
