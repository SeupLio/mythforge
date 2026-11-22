using System;
using System.Collections.Generic;
using System.IO;
using System.Reflection;
using UnityEditor;
using UnityEngine;

namespace MythForge.Editor
{
    /// <summary>
    /// Best-effort bridge to Unity's Timeline package. The editor package must
    /// compile even when Timeline is not installed, so every Timeline type is
    /// reached through AssetDatabase filters and reflection instead of a
    /// compile-time dependency.
    /// </summary>
    public static class MfTimelineBridge
    {
        private const string TimelineFilter = "t:TimelineAsset";

        /// <summary>Project-relative paths of every TimelineAsset in the project.</summary>
        public static List<string> Search()
        {
            var paths = new List<string>();
            string[] guids = AssetDatabase.FindAssets(TimelineFilter);
            for (int i = 0; i < guids.Length; i++)
            {
                string path = AssetDatabase.GUIDToAssetPath(guids[i]);
                if (!string.IsNullOrEmpty(path)) paths.Add(path);
            }
            paths.Sort(StringComparer.Ordinal);
            return paths;
        }

        public static UnityEngine.Object Load(string pathOrName)
        {
            if (string.IsNullOrEmpty(pathOrName)) return null;
            UnityEngine.Object asset = AssetDatabase.LoadAssetAtPath<UnityEngine.Object>(pathOrName);
            if (asset != null) return asset;
            // Accept a bare asset name for hand-written content.
            List<string> paths = Search();
            for (int i = 0; i < paths.Count; i++)
            {
                if (string.Equals(Path.GetFileNameWithoutExtension(paths[i]), pathOrName,
                        StringComparison.OrdinalIgnoreCase))
                {
                    return AssetDatabase.LoadAssetAtPath<UnityEngine.Object>(paths[i]);
                }
            }
            return null;
        }

        public static bool Exists(string pathOrName)
        {
            return Load(pathOrName) != null;
        }

        public static string ResolvePath(string pathOrName)
        {
            UnityEngine.Object asset = Load(pathOrName);
            return asset != null ? AssetDatabase.GetAssetPath(asset) : string.Empty;
        }

        /// <summary>Select and ping the asset in the Project window.</summary>
        public static bool Ping(string pathOrName)
        {
            UnityEngine.Object asset = Load(pathOrName);
            if (asset == null) return false;
            Selection.activeObject = asset;
            EditorGUIUtility.PingObject(asset);
            return true;
        }

        /// <summary>
        /// Select the asset and try to open the Timeline window on it. Opening is
        /// reflection-only because UnityEditor.Timeline is not a public API.
        /// </summary>
        public static bool Open(string pathOrName)
        {
            UnityEngine.Object asset = Load(pathOrName);
            if (asset == null) return false;
            Selection.activeObject = asset;
            EditorGUIUtility.PingObject(asset);

            Type editorType = Type.GetType("UnityEditor.Timeline.TimelineEditor, Unity.Timeline.Editor");
            if (editorType != null)
            {
                MethodInfo[] methods = editorType.GetMethods(
                    BindingFlags.Static | BindingFlags.Public | BindingFlags.NonPublic);
                for (int i = 0; i < methods.Length; i++)
                {
                    MethodInfo method = methods[i];
                    if (!string.Equals(method.Name, "OpenTimeline", StringComparison.Ordinal)) continue;
                    ParameterInfo[] parameters = method.GetParameters();
                    if (parameters.Length != 1) continue;
                    if (!parameters[0].ParameterType.IsInstanceOfType(asset)) continue;
                    try
                    {
                        method.Invoke(null, new object[] { asset });
                        return true;
                    }
                    catch (Exception)
                    {
                        // Fall through to select+ping; the user can open it manually.
                    }
                }
            }
            return false;
        }

        /// <summary>Converts an absolute path under Assets/ to the project form.</summary>
        public static string ToProjectPath(string absolutePath)
        {
            if (string.IsNullOrEmpty(absolutePath)) return string.Empty;
            string full = Path.GetFullPath(absolutePath).Replace('\\', '/');
            string data = Path.GetFullPath(Application.dataPath).Replace('\\', '/');
            if (full.StartsWith(data, StringComparison.OrdinalIgnoreCase))
            {
                return "Assets" + full.Substring(data.Length);
            }
            return absolutePath.Replace('\\', '/');
        }
    }
}
