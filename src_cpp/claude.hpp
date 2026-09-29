#pragma once

#include "config.hpp"
#include "db.hpp"

#include "json.hpp"

#include <string>
#include <vector>

namespace kos {

nlohmann::json claude_status(const Config& cfg);

nlohmann::json ask_dataset(Store& store, const Config& cfg, int64_t dataset_id, const std::string& question,
                           const std::string& topic, const std::vector<int64_t>& ids);

nlohmann::json explain_dataset(Store& store, const Config& cfg, int64_t dataset_id, const std::string& topic,
                               const std::vector<int64_t>& ids);

}  // namespace kos
