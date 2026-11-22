#include "mf_sdk.h"

#include <algorithm>
#include <cstring>
#include <deque>
#include <new>
#include <string>
#include <vector>

#include "mfc/common.h"
#include "mfc/json.h"
#include "mfc/module.h"
#include "mfvm/vm.h"

namespace {

std::string compactJson(const mfc::json::Value& value) {
  std::string text = mfc::json::dump(value, 0);
  text.erase(std::remove(text.begin(), text.end(), '\n'), text.end());
  return text;
}

mfc::json::Value valueToJson(const mfvm::Value& value, const mfvm::VM& vm) {
  switch (value.tag) {
    case mfvm::ValueTag::Int: return mfc::json::Value::integer(value.as.i);
    case mfvm::ValueTag::Float: return mfc::json::Value::number(value.as.f);
    case mfvm::ValueTag::Bool: return mfc::json::Value::boolean(value.as.b != 0);
    case mfvm::ValueTag::String: return mfc::json::Value::string(vm.stringOf(value.as.s));
    case mfvm::ValueTag::Nil: return mfc::json::Value::null();
  }
  return mfc::json::Value::null();
}

mfvm::Value jsonToValue(const mfc::json::Value& value, mfvm::VM& vm) {
  if (value.isString()) return vm.stringValue(value.asString());
  if (value.isBool()) return mfvm::Value::boolean(value.asBool());
  if (value.isNumber()) {
    if (value.asInt() == value.asDouble()) return mfvm::Value::integer(value.asInt());
    return mfvm::Value::number(value.asDouble());
  }
  return mfvm::Value::nil();
}

void writeError(char* err, int32_t cap, const std::string& message) {
  if (err == nullptr || cap <= 0) return;
  const size_t count = std::min(static_cast<size_t>(cap - 1), message.size());
  std::memcpy(err, message.data(), count);
  err[count] = '\0';
}

}  // namespace

struct mf_vm {
  mfvm::VM vm;
  std::deque<std::string> lines;
  std::string lastError;
  bool hasModule = false;

  mf_vm() {
    vm.setTraceCallback([this](const mfvm::TraceEvent& event) { appendTrace(event); });
  }

  void appendTrace(const mfvm::TraceEvent& event) {
    mfc::json::Value root = mfc::json::Value::object();
    root["type"] = mfc::json::Value::string(event.kind);
    root["name"] = mfc::json::Value::string(event.name);
    root["node"] = mfc::json::Value::string(event.nodeId);
    root["at"] = mfc::json::Value::integer(event.atMicros);
    mfc::json::Value args = mfc::json::Value::array();
    for (const auto& arg : event.args) args.push(valueToJson(arg, vm));
    root["args"] = std::move(args);
    lines.push_back(compactJson(root));
  }

  void syncError() {
    lastError = vm.lastError();
  }
};

namespace {

bool loadBytes(mf_vm* handle, const uint8_t* data, int32_t size, std::string* error) {
  if (handle == nullptr || data == nullptr || size <= 0) {
    if (error) *error = "empty module";
    return false;
  }
  const std::string bytes(reinterpret_cast<const char*>(data), static_cast<size_t>(size));
  mfc::Module module;
  mfc::DiagnosticSink sink;
  if (!mfc::deserializeModule(bytes, module, sink)) {
    if (error) *error = sink.formatText();
    return false;
  }
  if (!mfc::verifyModule(module, sink)) {
    if (error) *error = sink.formatText();
    return false;
  }
  std::string loadError;
  if (!handle->vm.loadModule(module, &loadError)) {
    if (error) *error = loadError;
    return false;
  }
  handle->hasModule = true;
  handle->lines.clear();
  handle->lastError.clear();
  return true;
}

}  // namespace

extern "C" {

MF_API mf_vm* mf_vm_create(void) { return new (std::nothrow) mf_vm(); }

MF_API void mf_vm_destroy(mf_vm* vm) { delete vm; }

MF_API int32_t mf_vm_load_module(mf_vm* vm, const uint8_t* data, int32_t size, char* err,
                                 int32_t errCap) {
  if (vm == nullptr) return 0;
  std::string error;
  if (!loadBytes(vm, data, size, &error)) {
    writeError(err, errCap, error);
    return 0;
  }
  return 1;
}

MF_API int32_t mf_vm_load_module_file(mf_vm* vm, const char* path, char* err, int32_t errCap) {
  if (vm == nullptr || path == nullptr) return 0;
  std::string bytes;
  std::string error;
  if (!mfc::readFile(path, &bytes, &error)) {
    writeError(err, errCap, error);
    return 0;
  }
  return mf_vm_load_module(vm, reinterpret_cast<const uint8_t*>(bytes.data()),
                           static_cast<int32_t>(bytes.size()), err, errCap);
}

MF_API int32_t mf_vm_start(mf_vm* vm) {
  if (vm == nullptr || !vm->hasModule) return -1;
  const int started = vm->vm.startQuest();
  vm->vm.runUntilIdle();
  vm->syncError();
  return started;
}

MF_API int32_t mf_vm_emit(mf_vm* vm, const char* name, const char* fieldsJson) {
  if (vm == nullptr || name == nullptr) return 0;
  mfc::json::Value fields;
  std::string error;
  const std::string text = fieldsJson != nullptr ? fieldsJson : "{}";
  if (!mfc::json::parse(text, fields, &error)) {
    vm->lastError = "invalid fields JSON: " + error;
    return 0;
  }
  std::vector<std::pair<std::string, mfvm::Value>> payload;
  if (fields.isObject()) {
    for (const auto& member : fields.members()) {
      payload.emplace_back(member.first, jsonToValue(member.second, vm->vm));
    }
  }
  vm->vm.emitEvent(name, payload);
  vm->vm.runUntilIdle();
  vm->syncError();
  return vm->vm.lastError().empty() ? 1 : 0;
}

MF_API int32_t mf_vm_advance_time(mf_vm* vm, int64_t micros) {
  if (vm == nullptr) return 0;
  vm->vm.advanceTime(std::max<int64_t>(0, micros));
  vm->vm.runUntilIdle();
  vm->syncError();
  return vm->vm.lastError().empty() ? 1 : 0;
}

MF_API int32_t mf_vm_run(mf_vm* vm) {
  if (vm == nullptr) return 0;
  vm->vm.runUntilIdle();
  vm->syncError();
  return vm->vm.lastError().empty() ? 1 : 0;
}

MF_API int32_t mf_vm_running(mf_vm* vm) {
  if (vm == nullptr) return 0;
  return vm->vm.hasLiveInstances() ? 1 : 0;
}

MF_API int64_t mf_vm_now_micros(mf_vm* vm) {
  return vm != nullptr ? vm->vm.nowMicros() : 0;
}

MF_API int32_t mf_vm_poll(mf_vm* vm, char* buffer, int32_t cap) {
  if (vm == nullptr || buffer == nullptr || cap <= 0) return 0;
  if (vm->lines.empty()) return 0;
  const std::string& line = vm->lines.front();
  if (static_cast<int32_t>(line.size()) + 1 > cap) {
    vm->lines.pop_front();
    return 0;
  }
  std::memcpy(buffer, line.data(), line.size());
  buffer[line.size()] = '\0';
  vm->lines.pop_front();
  return 1;
}

MF_API const char* mf_vm_last_error(mf_vm* vm) {
  return vm != nullptr ? vm->lastError.c_str() : "null vm";
}

MF_API const char* mf_sdk_version(void) { return "mythforge-sdk-0.2.0"; }

}  // extern "C"
