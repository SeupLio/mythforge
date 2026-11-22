using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;
using UnityEngine;

namespace MythForge.Runtime
{
    /// <summary>
    /// Locates mythforge.dll (repo build output or MYTHFORGE_SDK) and declares
    /// the C ABI from native/sdk/include/mf_sdk.h. The wrapper never
    /// re-implements VM semantics; it only marshals.
    /// </summary>
    public static class MfNativeLibrary
    {
        private const string Lib = "mythforge";
        private static bool _ready;

        public static void EnsureLoaded()
        {
            if (_ready) return;
            _ready = true;
            string explicitPath = Environment.GetEnvironmentVariable("MYTHFORGE_SDK");
            if (!string.IsNullOrEmpty(explicitPath))
            {
                string directory = System.IO.Path.GetDirectoryName(explicitPath);
                SetSearchDirectory(directory);
                return;
            }
            string root = FindProjectRoot();
            if (root != null)
            {
                SetSearchDirectory(System.IO.Path.Combine(root, "build", "native"));
            }
        }

        private static void SetSearchDirectory(string directory)
        {
            if (string.IsNullOrEmpty(directory) || !System.IO.Directory.Exists(directory)) return;
#if UNITY_STANDALONE_WIN || UNITY_EDITOR_WIN
            SetDllDirectory(directory);
            // Make the Unity process able to find the DLL even when the current
            // working directory differs from the repository root.
            string path = Environment.GetEnvironmentVariable("PATH") ?? string.Empty;
            if (path.IndexOf(directory, StringComparison.OrdinalIgnoreCase) < 0)
            {
                Environment.SetEnvironmentVariable("PATH", directory + ";" + path);
            }
#else
            string path = Environment.GetEnvironmentVariable("LD_LIBRARY_PATH") ?? string.Empty;
            if (path.IndexOf(directory, StringComparison.Ordinal) < 0)
            {
                Environment.SetEnvironmentVariable("LD_LIBRARY_PATH", directory + ":" + path);
            }
#endif
        }

        private static string FindProjectRoot()
        {
            string directory = Application.dataPath;
            for (int depth = 0; depth < 10 && !string.IsNullOrEmpty(directory); depth++)
            {
                if (System.IO.File.Exists(System.IO.Path.Combine(directory, "schema", "node_types.json")))
                {
                    return directory;
                }
                directory = System.IO.Path.GetDirectoryName(directory);
            }
            return null;
        }

#if UNITY_STANDALONE_WIN || UNITY_EDITOR_WIN
        [DllImport("kernel32", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern bool SetDllDirectory(string path);
#endif

        public static string Version()
        {
            EnsureLoaded();
            return PtrToUtf8(mf_sdk_version());
        }

        internal static string PtrToUtf8(IntPtr pointer)
        {
            if (pointer == IntPtr.Zero) return string.Empty;
            int length = 0;
            while (Marshal.ReadByte(pointer, length) != 0) length++;
            var bytes = new byte[length];
            Marshal.Copy(pointer, bytes, 0, length);
            return Encoding.UTF8.GetString(bytes);
        }

        internal const string Library = Lib;

        // --- C ABI ---------------------------------------------------------
        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern IntPtr mf_vm_create();

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern void mf_vm_destroy(IntPtr vm);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int mf_vm_load_module_file(IntPtr vm, byte[] path, [In, Out] byte[] err,
            int errCap);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int mf_vm_start(IntPtr vm);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int mf_vm_emit(IntPtr vm, byte[] name, byte[] fieldsJson);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int mf_vm_advance_time(IntPtr vm, long micros);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int mf_vm_run(IntPtr vm);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int mf_vm_running(IntPtr vm);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern long mf_vm_now_micros(IntPtr vm);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern int mf_vm_poll(IntPtr vm, [In, Out] byte[] buffer, int cap);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        internal static extern IntPtr mf_vm_last_error(IntPtr vm);

        [DllImport(Lib, CallingConvention = CallingConvention.Cdecl)]
        private static extern IntPtr mf_sdk_version();
    }
}
