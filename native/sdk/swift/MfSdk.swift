// MythForge Swift binding (reference implementation).
//
// Build the C SDK first, then add `MythForgeSDK` (module.modulemap next to this
// file) to the Xcode target. The ABI is the same one covered by the Unity tests;
// Xcode/iOS was not available in this environment, so this file is source-only.

import Foundation

public final class MfVm {
    private let handle: OpaquePointer
    private var buffer = [CChar](repeating: 0, count: 64 * 1024)

    public init?() {
        guard let vm = mf_vm_create() else { return nil }
        handle = vm
    }

    deinit {
        mf_vm_destroy(handle)
    }

    public var version: String {
        String(cString: mf_sdk_version())
    }

    public var nowMicros: Int64 {
        mf_vm_now_micros(handle)
    }

    public var isRunning: Bool {
        mf_vm_running(handle) == 1
    }

    public var lastError: String {
        String(cString: mf_vm_last_error(handle))
    }

    public func loadModule(path: String) -> Bool {
        var error = [CChar](repeating: 0, count: 4096)
        return path.withCString { pointer in
            mf_vm_load_module_file(handle, pointer, &error, Int32(error.count)) == 1
        }
    }

    public func start() -> Int32 {
        mf_vm_start(handle)
    }

    public func emit(_ name: String, fieldsJson: String = "{}") -> Bool {
        name.withCString { event in
            fieldsJson.withCString { fields in
                mf_vm_emit(handle, event, fields) == 1
            }
        }
    }

    public func advanceTime(micros: Int64) -> Bool {
        mf_vm_advance_time(handle, micros) == 1
    }

    public func run() -> Bool {
        mf_vm_run(handle) == 1
    }

    /// Returns every queued trace line (JSON objects), oldest first.
    public func poll() -> [String] {
        var lines: [String] = []
        while mf_vm_poll(handle, &buffer, Int32(buffer.count)) == 1 {
            if let line = String(validatingUTF8: buffer) {
                lines.append(line)
            }
        }
        return lines
    }
}
