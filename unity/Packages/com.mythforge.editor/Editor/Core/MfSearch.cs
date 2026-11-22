using System;
using System.Collections.Generic;

namespace MythForge.Editor
{
    /// <summary>
    /// Fuzzy + pinyin-keyword matching for the node creation menu and search bar.
    /// Node types carry explicit pinyin keys ("jisha", "shouji", ...), so the
    /// editor does not need a full pinyin dictionary while still supporting the
    /// workflow designers expect.
    /// </summary>
    public static class MfSearch
    {
        public static bool MatchesNode(string query, MfNodeTypeSpec spec)
        {
            if (spec == null) return false;
            return Matches(query, spec.Display, spec.SearchKeys, spec.Id);
        }

        public static bool Matches(string query, params string[] fields)
        {
            if (string.IsNullOrEmpty(query) || query.Trim().Length == 0) return true;
            string[] tokens = query.ToLowerInvariant().Split(new[] { ' ', '\t' }, StringSplitOptions.RemoveEmptyEntries);
            if (tokens.Length == 0) return true;

            var haystack = new System.Text.StringBuilder();
            for (int i = 0; i < fields.Length; i++)
            {
                if (string.IsNullOrEmpty(fields[i])) continue;
                if (haystack.Length > 0) haystack.Append(' ');
                haystack.Append(fields[i].ToLowerInvariant());
            }
            string text = haystack.ToString();
            for (int i = 0; i < tokens.Length; i++)
            {
                string token = tokens[i];
                if (text.Contains(token)) continue;
                if (!IsSubsequence(token, text)) return false;
            }
            return true;
        }

        private static bool IsSubsequence(string needle, string haystack)
        {
            int index = 0;
            for (int i = 0; i < haystack.Length && index < needle.Length; i++)
            {
                if (haystack[i] == needle[index]) index++;
            }
            return index == needle.Length;
        }
    }
}
