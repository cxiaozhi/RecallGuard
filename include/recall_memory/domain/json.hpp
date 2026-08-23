#pragma once

#include "recall_memory/domain/model.hpp"

#include <nlohmann/json.hpp>

namespace recall_memory {

void to_json(nlohmann::json& json, const Scope& value);
void from_json(const nlohmann::json& json, Scope& value);
void to_json(nlohmann::json& json, const Evidence& value);
void from_json(const nlohmann::json& json, Evidence& value);
void to_json(nlohmann::json& json, const VerificationStep& value);
void from_json(const nlohmann::json& json, VerificationStep& value);
void to_json(nlohmann::json& json, const ExperienceDraft& value);
void from_json(const nlohmann::json& json, ExperienceDraft& value);
void to_json(nlohmann::json& json, const Experience& value);
void from_json(const nlohmann::json& json, Experience& value);
void to_json(nlohmann::json& json, const GraphNode& value);
void to_json(nlohmann::json& json, const GraphEdge& value);
void to_json(nlohmann::json& json, const IndexReport& value);
void to_json(nlohmann::json& json, const GraphStatus& value);
void to_json(nlohmann::json& json, const RecallRequest& value);
void from_json(const nlohmann::json& json, RecallRequest& value);
void to_json(nlohmann::json& json, const RecallHit& value);
void to_json(nlohmann::json& json, const GuardRequest& value);
void from_json(const nlohmann::json& json, GuardRequest& value);
void to_json(nlohmann::json& json, const GuardResult& value);

}  // namespace recall_memory
