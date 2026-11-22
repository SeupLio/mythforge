#pragma once

#include <cstdint>
#include <deque>
#include <functional>
#include <queue>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "mfc/module.h"
#include "mfvm/value.h"

namespace mfvm {

// Host boundary: the game registers one implementation to receive quest calls
// and emitted events. The VM never depends on Unity/Android types directly.
class Host {
 public:
  virtual ~Host() = default;
  virtual Value call(const std::string& function, const std::vector<Value>& args) {
    (void)function;
    (void)args;
    return Value::nil();
  }
  virtual void emit(const std::string& event, const std::vector<Value>& args) {
    (void)event;
    (void)args;
  }
};

struct RunStats {
  int64_t steps = 0;
  int64_t faults = 0;
  bool budgetExceeded = false;
};

// Structured execution trace. The SDK turns these into JSON lines for live
// preview/telemetry, and `nodeId` lets an editor highlight the graph node that
// produced an emit/call (debug info is shipped inside the .mfb).
struct TraceEvent {
  std::string kind;    // start | emit | call | wait | fault
  std::string name;    // event/call/chunk name (empty for wait time)
  std::string nodeId;  // graph node provenance, may be empty
  std::vector<Value> args;
  int64_t atMicros = 0;
};

using TraceCallback = std::function<void(const TraceEvent&)>;

// Deterministic, single-threaded coroutine VM. Each script instance is a heap
// frame (register array + pc), so tens of thousands of quests can wait for
// events/timers without native stack cost.
class VM {
 public:
  VM();
  ~VM();
  VM(const VM&) = delete;
  VM& operator=(const VM&) = delete;

  void setHost(Host* host) { host_ = host; }
  void setTraceCallback(TraceCallback callback) { trace_ = std::move(callback); }

  bool loadModule(const mfc::Module& module, std::string* error);
  void unloadAll();

  bool hasChunk(const std::string& name) const;
  uint64_t startChunk(const std::string& name);
  int startQuest();  // starts "<id>/on_*<" listeners first, then "<id>/start"

  bool emitEvent(const std::string& name, const std::vector<std::pair<std::string, Value>>& fields);
  void advanceTime(int64_t deltaMicros);

  RunStats runUntilIdle(int64_t maxSteps = 2000000);
  RunStats runUntilAllIdle(int64_t maxSteps = 2000000);

  bool hasLiveInstances() const;
  size_t liveInstanceCount() const;
  int64_t nowMicros() const { return now_; }
  void setNowMicros(int64_t value) { now_ = value; }

  const std::string& lastError() const { return lastError_; }
  const std::string& moduleId() const { return module_.id; }

  uint32_t intern(const std::string& text);
  Value stringValue(const std::string& text);
  const std::string& stringOf(uint32_t id) const;
  std::string valueToString(const Value& value) const;

 private:
  struct Instance {
    enum class State { Ready, Running, WaitingTime, WaitingEvent, Done, Faulted };
    uint64_t id = 0;
    int chunk = -1;
    uint32_t pc = 0;
    State state = State::Ready;
    std::vector<Value> regs;
    int64_t wakeAt = 0;
    uint64_t timerSeq = 0;
    std::string waitEvent;
    int predicate = 0;
    std::vector<std::pair<std::string, Value>> eventPayload;
  };

  struct Timer {
    int64_t wakeAt = 0;
    uint64_t seq = 0;
    Instance* instance = nullptr;
  };

  struct TimerGreater {
    bool operator()(const Timer& a, const Timer& b) const {
      if (a.wakeAt != b.wakeAt) return a.wakeAt > b.wakeAt;
      return a.seq > b.seq;
    }
  };

  Instance* allocateInstance();
  void releaseInstance(Instance* instance);
  void execute(Instance* instance, int64_t& stepBudget, RunStats& stats);
  void fault(Instance* instance, const std::string& message);
  void deliver(const std::string& name, const std::vector<std::pair<std::string, Value>>& fields);
  bool evaluatePredicate(const mfc::Predicate& predicate,
                         const std::vector<std::pair<std::string, Value>>& fields);
  const mfc::Chunk* chunkOf(const Instance& instance) const;
  std::string nodeIdFor(const mfc::Chunk& chunk, uint32_t pc) const;
  void emitTrace(const std::string& kind, const std::string& name, const std::string& nodeId,
                 const std::vector<Value>& args);

  mfc::Module module_;
  std::unordered_map<std::string, int> chunkIndex_;
  std::vector<std::string> strings_;
  std::unordered_map<std::string, uint32_t> stringIds_;

  std::vector<Instance*> instances_;
  std::vector<Instance*> freeInstances_;
  std::deque<Instance*> ready_;
  std::priority_queue<Timer, std::vector<Timer>, TimerGreater> timers_;
  std::unordered_map<std::string, std::vector<Instance*>> waiters_;

  Host* host_ = nullptr;
  TraceCallback trace_ = nullptr;
  int64_t now_ = 0;
  uint64_t nextInstanceId_ = 1;
  uint64_t nextTimerSeq_ = 1;
  std::string lastError_;
};

}  // namespace mfvm
