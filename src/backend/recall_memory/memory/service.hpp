#pragma once

#include "recall_memory/domain/model.hpp"
#include "recall_memory/storage/store.hpp"

#include <vector>

namespace recall_memory {

class MemoryService {
public:
    explicit MemoryService(Store& store);

    Experience propose(const ExperienceDraft& draft);
    Experience verify(const std::string& id, const std::vector<Evidence>& evidence);
    Experience mark_stale(const std::string& id);
    void feedback(const std::string& id, const std::string& value, const std::string& note);

    std::vector<RecallHit> recall(const RecallRequest& request) const;
    GuardResult guard(const GuardRequest& request) const;

private:
    Store& store_;
};

}  // namespace recall_memory
