#pragma once

// MythForge runtime C ABI (M7). The SDK embeds the same compiler output the
// native CLI produces; hosts (Unity/Kotlin/Swift) only need this header plus
// mythforge.dll / libmythforge.so.
//
// Threading: one mf_vm instance must only be used from one thread at a time.
// Poll all trace lines after every call; the queue is unbounded, so a host that
// never polls will grow memory.

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32)
#define MF_API __declspec(dllexport)
#else
#define MF_API __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct mf_vm mf_vm;

// Creates a VM with an empty trace queue. Returns NULL on allocation failure.
MF_API mf_vm* mf_vm_create(void);
MF_API void mf_vm_destroy(mf_vm* vm);

// Loads a .mfb container. Returns 1 on success, 0 on failure (err receives a
// UTF-8 message). Loading replaces any previous module and resets the VM.
MF_API int32_t mf_vm_load_module(mf_vm* vm, const uint8_t* data, int32_t size, char* err,
                                 int32_t errCap);
MF_API int32_t mf_vm_load_module_file(mf_vm* vm, const char* path, char* err, int32_t errCap);

// Starts every "<id>/on_*" listener and the "<id>/start" or "<id>/cast" entry.
// Runs only until the next wait, so timers are driven by the host.
// Returns the number of started chunks, or -1 if the VM has no module.
MF_API int32_t mf_vm_start(mf_vm* vm);

// Injects an event. fieldsJson is a JSON object ({} for no fields). Runs until
// idle (without advancing the clock). Returns 1 on success, 0 on failure.
MF_API int32_t mf_vm_emit(mf_vm* vm, const char* name, const char* fieldsJson);

// Advances the virtual clock and wakes due timers. Returns 1 on success.
MF_API int32_t mf_vm_advance_time(mf_vm* vm, int64_t micros);

// Drains ready instances without moving the clock. Returns 1 if no fault.
MF_API int32_t mf_vm_run(mf_vm* vm);

// 1 while any instance is waiting for an event/timer.
MF_API int32_t mf_vm_running(mf_vm* vm);
MF_API int64_t mf_vm_now_micros(mf_vm* vm);

// Pops one JSON line into buffer (UTF-8). Returns 1 when a line was written,
// 0 when the queue is empty. Lines longer than cap are dropped.
// Line shape: {"type":"emit|call|wait|start|fault","name":"...","node":"...",
//              "at":123,"args":[...]}
MF_API int32_t mf_vm_poll(mf_vm* vm, char* buffer, int32_t cap);

// Last VM error (empty when healthy); valid until the next SDK call.
MF_API const char* mf_vm_last_error(mf_vm* vm);
MF_API const char* mf_sdk_version(void);

#ifdef __cplusplus
}
#endif
