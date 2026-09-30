#pragma once

#include "json.hpp"

#include <filesystem>
#include <string>

namespace kos::insights_detail {

std::filesystem::path find_agent_script();
std::string fallback_insights(const nlohmann::json& payload);
bool run_agent(const std::filesystem::path& script, const std::string& input, std::string& output);
nlohmann::json parse_agent_stdout(const std::string& raw, const nlohmann::json& payload);
void attach_calculations(nlohmann::json& result, const nlohmann::json& payload);
nlohmann::json slim_graph(const nlohmann::json& graph);

}  // namespace kos::insights_detail
