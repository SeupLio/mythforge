using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Reflection;
using NUnit.Framework;
using MythForge.Runtime;
using UnityEditor;
using UnityEngine;

namespace MythForge.Editor.Tests
{
    /// <summary>
    /// Headless construction tests for the real editor windows. They invoke the
    /// same CreateGUI/OnEnable entry points Unity calls, so window wiring and
    /// demo loading are covered without a display.
    /// </summary>
    public class WindowConstructionTests
    {
        [Test]
        public void DialogueWindowBuildsGraphFromDemo()
        {
            EditorPrefs.DeleteKey("MythForge.Last.dialogue");
            var window = ScriptableObject.CreateInstance<DialogueGraphWindow>();
            try
            {
                MethodInfo createGui = typeof(QuestGraphWindow).GetMethod("CreateGUI",
                    BindingFlags.Instance | BindingFlags.NonPublic);
                Assert.IsNotNull(createGui);
                createGui.Invoke(window, null);

                FieldInfo field = typeof(QuestGraphWindow).GetField("_graphView",
                    BindingFlags.Instance | BindingFlags.NonPublic);
                Assert.IsNotNull(field);
                var graph = (MfGraphView)field.GetValue(window);
                Assert.IsNotNull(graph);
                Assert.AreEqual("dialogue.start", graph.Catalog.Entry.Id);
                Assert.AreEqual(7, graph.NodeViews.Count());
            }
            finally
            {
                Object.DestroyImmediate(window);
            }
        }

        [Test]
        public void SkillWindowLoadsDemoOnEnable()
        {
            EditorPrefs.DeleteKey("MythForge.Last.skill");
            var window = ScriptableObject.CreateInstance<SkillTimelineWindow>();
            MethodInfo onEnable = typeof(SkillTimelineWindow).GetMethod("OnEnable",
                BindingFlags.Instance | BindingFlags.NonPublic);
            MethodInfo onDisable = typeof(SkillTimelineWindow).GetMethod("OnDisable",
                BindingFlags.Instance | BindingFlags.NonPublic);
            Assert.IsNotNull(onEnable);
            Assert.IsNotNull(onDisable);
            try
            {
                onEnable.Invoke(window, null);
                Assert.AreEqual(3, window.FrameCount);
            }
            finally
            {
                onDisable.Invoke(window, null);
                Object.DestroyImmediate(window);
            }
        }

        [Test]
        public void PlayWindowLoadsDemoAndStartsSession()
        {
            string previous = EditorPrefs.GetString("MythForge.LastPlay", string.Empty);
            string root = MfCli.FindProjectRoot();
            Assert.IsNotNull(root);
            string demo = Path.Combine(root, "content", "quests", "q_forest_wolves.json");
            Assert.IsTrue(File.Exists(demo), demo);
            EditorPrefs.SetString("MythForge.LastPlay", demo);

            var window = ScriptableObject.CreateInstance<PlayWindow>();
            MethodInfo createGui = typeof(PlayWindow).GetMethod("CreateGUI",
                BindingFlags.Instance | BindingFlags.NonPublic);
            MethodInfo onDisable = typeof(PlayWindow).GetMethod("OnDisable",
                BindingFlags.Instance | BindingFlags.NonPublic);
            Assert.IsNotNull(createGui);
            Assert.IsNotNull(onDisable);
            try
            {
                createGui.Invoke(window, null);
                FieldInfo sessionField = typeof(PlayWindow).GetField("_session",
                    BindingFlags.Instance | BindingFlags.NonPublic);
                Assert.IsNotNull(sessionField);
                var session = (MfPlaySession)sessionField.GetValue(window);
                Assert.IsNotNull(session);
                Assert.IsTrue(session.Lines.Any(l => l.Name == "QuestStarted"));
                Assert.IsFalse(string.IsNullOrEmpty(session.LastNodeId));
                Assert.IsTrue(session.Emit("Kill",
                    new Dictionary<string, object> { { "target", "Wolf" } }));
            }
            finally
            {
                onDisable.Invoke(window, null);
                EditorPrefs.SetString("MythForge.LastPlay", previous);
                Object.DestroyImmediate(window);
            }
        }
    }
}
