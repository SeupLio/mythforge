package com.mythforge.sdk

import com.sun.jna.Library
import com.sun.jna.Native
import com.sun.jna.Pointer

/**
 * JNA binding for mythforge.dll / libmythforge.so (reference implementation).
 *
 * The Android/Kotlin side was not compiled in this repository (no Android NDK
 * toolchain here); the ABI calls are identical to the Unity P/Invoke path that
 * IS covered by EditMode tests.
 */
interface MfLibrary : Library {
    fun mf_vm_create(): Pointer
    fun mf_vm_destroy(vm: Pointer)
    fun mf_vm_load_module_file(vm: Pointer, path: String, err: ByteArray, errCap: Int): Int
    fun mf_vm_start(vm: Pointer): Int
    fun mf_vm_emit(vm: Pointer, name: String, fieldsJson: String): Int
    fun mf_vm_advance_time(vm: Pointer, micros: Long): Int
    fun mf_vm_run(vm: Pointer): Int
    fun mf_vm_running(vm: Pointer): Int
    fun mf_vm_now_micros(vm: Pointer): Long
    fun mf_vm_poll(vm: Pointer, buffer: ByteArray, cap: Int): Int
    fun mf_vm_last_error(vm: Pointer): String
    fun mf_sdk_version(): String
}

class MfVm(libraryPath: String? = null) : AutoCloseable {
    private val library: MfLibrary =
        if (libraryPath == null) Native.load("mythforge", MfLibrary::class.java)
        else Native.load(libraryPath, MfLibrary::class.java)
    private val handle: Pointer = library.mf_vm_create()
    private val buffer = ByteArray(64 * 1024)

    val version: String get() = library.mf_sdk_version()
    val nowMicros: Long get() = library.mf_vm_now_micros(handle)
    val running: Boolean get() = library.mf_vm_running(handle) == 1
    val lastError: String get() = library.mf_vm_last_error(handle)

    fun loadModule(path: String): Boolean {
        val error = ByteArray(4096)
        return library.mf_vm_load_module_file(handle, path, error, error.size) == 1
    }

    fun start(): Int = library.mf_vm_start(handle)

    fun emit(name: String, fieldsJson: String = "{}"): Boolean =
        library.mf_vm_emit(handle, name, fieldsJson) == 1

    fun advanceTime(micros: Long): Boolean =
        library.mf_vm_advance_time(handle, micros) == 1

    fun run(): Boolean = library.mf_vm_run(handle) == 1

    /** Returns every queued trace line (JSON objects), oldest first. */
    fun poll(): List<String> {
        val lines = ArrayList<String>()
        while (library.mf_vm_poll(handle, buffer, buffer.size) == 1) {
            var length = 0
            while (length < buffer.size && buffer[length] != 0.toByte()) length++
            lines.add(String(buffer, 0, length, Charsets.UTF_8))
        }
        return lines
    }

    override fun close() {
        library.mf_vm_destroy(handle)
    }
}
