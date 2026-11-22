#include "mfc/events.h"

namespace mfc {

const std::vector<EventSchema>& builtinEvents() {
  static const std::vector<EventSchema> events = {
      {"Kill", {{"target", Type::String}, {"count", Type::Int}}},
      {"Collect", {{"item", Type::String}, {"count", Type::Int}}},
      {"Reach", {{"area", Type::String}}},
      {"Talk", {{"npc", Type::String}}},
      {"Damage", {{"target", Type::String}, {"amount", Type::Int}}},
      {"ItemUsed", {{"item", Type::String}}},
      {"SkillCast", {{"skill", Type::String}}},
      {"Timer", {{"name", Type::String}}},
      {"QuestStarted", {{"quest", Type::String}}},
      {"QuestCompleted", {{"quest", Type::String}}},
      {"QuestFailed", {{"quest", Type::String}}},
      {"Log", {{"text", Type::String}}},
      // Dialogue runtime contract. Line/choice nodes emit their presentation
      // event and then wait for the matching advance/selection event, so the
      // script never needs to know who drives the UI.
      {"DialogueLine", {{"speaker", Type::String}, {"text", Type::String}}},
      {"DialogueAdvance", {}},
      {"DialogueChoice", {{"options", Type::String}}},
      {"DialogueChoiceSelected", {}},
      {"DialogueFlag", {{"flag", Type::String}}},
      {"TimelineFinished", {{"asset", Type::String}}},
      // Skill timeline contract: a finished skill reports the module id.
      {"SkillEnded", {{"skill", Type::String}}},
  };
  return events;
}

const EventSchema* findEventSchema(const std::string& name) {
  for (const auto& e : builtinEvents()) {
    if (e.name == name) return &e;
  }
  return nullptr;
}

}  // namespace mfc
