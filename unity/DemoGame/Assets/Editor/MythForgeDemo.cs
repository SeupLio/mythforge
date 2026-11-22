using System.IO;
using System.Linq;
using MythForge.Editor;
using UnityEditor;
using UnityEditor.SceneManagement;
using UnityEngine;
using UnityEngine.UIElements;

/// <summary>Editor entry point used for demos and automated UI verification.</summary>
public static class MythForgeDemoTools
{
    public static void OpenQuestWindow()
    {
        EditorApplication.delayCall += () =>
        {
            QuestGraphWindow window = EditorWindow.GetWindow<QuestGraphWindow>();
            window.titleContent = new GUIContent("MythForge Quest");
            window.minSize = new Vector2(1100f, 700f);
            window.Show();
            window.Focus();
            EditorApplication.delayCall += () =>
            {
                MfGraphView graph = window.rootVisualElement.Q<MfGraphView>();
                int nodes = graph != null ? graph.NodeViews.Count() : -1;
                Debug.Log("MYTHFORGE_UI_READY nodes=" + nodes);
            };
        };
    }

    public static void OpenDialogueWindow()
    {
        EditorApplication.delayCall += () =>
        {
            DialogueGraphWindow window = EditorWindow.GetWindow<DialogueGraphWindow>();
            window.titleContent = new GUIContent("MythForge Dialogue");
            window.minSize = new Vector2(1100f, 700f);
            window.Show();
            window.Focus();
            EditorApplication.delayCall += () =>
            {
                MfGraphView graph = window.rootVisualElement.Q<MfGraphView>();
                int nodes = graph != null ? graph.NodeViews.Count() : -1;
                Debug.Log("MYTHFORGE_UI_READY dialogue nodes=" + nodes);
            };
        };
    }

    public static void OpenSkillWindow()
    {
        EditorApplication.delayCall += () =>
        {
            SkillTimelineWindow window = EditorWindow.GetWindow<SkillTimelineWindow>();
            window.titleContent = new GUIContent("MythForge Skill");
            window.minSize = new Vector2(1200f, 720f);
            window.Show();
            window.Focus();
            EditorApplication.delayCall += () =>
            {
                Debug.Log("MYTHFORGE_UI_READY skill frames=" + window.FrameCount);
            };
        };
    }

    /// <summary>
    /// Opens both new windows, logs their data state and exits. Used for
    /// automated UI verification in a GUI session.
    /// </summary>
    public static void VerifyM2Windows()
    {
        EditorApplication.delayCall += () =>
        {
            DialogueGraphWindow dialogue = EditorWindow.GetWindow<DialogueGraphWindow>();
            dialogue.Show();
            SkillTimelineWindow skill = EditorWindow.GetWindow<SkillTimelineWindow>();
            skill.Show();
            EditorApplication.delayCall += () =>
            {
                MfGraphView graph = dialogue.rootVisualElement.Q<MfGraphView>();
                int nodes = graph != null ? graph.NodeViews.Count() : -1;
                Debug.Log("MYTHFORGE_UI_READY dialogue nodes=" + nodes);
                Debug.Log("MYTHFORGE_UI_READY skill frames=" + skill.FrameCount);
                EditorApplication.delayCall += () => EditorApplication.Exit(0);
            };
        };
    }

    /// <summary>Copies compiled .mfb artifacts next to the player assets.</summary>
    [MenuItem("MythForge/同步运行时内容到 StreamingAssets")]
    public static void SyncRuntimeContent()
    {
        string root = MfCli.FindProjectRoot();
        if (string.IsNullOrEmpty(root))
        {
            Debug.LogError("[MythForge] 找不到仓库根目录");
            return;
        }
        string contentRoot = Path.Combine(root, "content");
        string outDir = Path.Combine(Application.streamingAssetsPath, "MythForge");
        Directory.CreateDirectory(outDir);
        MfProcessResult result = MfCli.BuildTo(contentRoot, outDir);
        Debug.Log("[MythForge] sync runtime content\n" + result.StdOut + result.StdErr);
        AssetDatabase.Refresh();
    }

    [MenuItem("MythForge/创建演示场景")]
    public static void CreateDemoScene()
    {
        var scene = EditorSceneManager.NewScene(NewSceneSetup.DefaultGameObjects, NewSceneMode.Single);
        var go = new GameObject("MythForgeDemo");
        go.AddComponent<MythForgeDemoGame>();
        string scenes = Path.Combine(Application.dataPath, "Scenes");
        if (!Directory.Exists(scenes)) Directory.CreateDirectory(scenes);
        EditorSceneManager.SaveScene(scene, "Assets/Scenes/MythForgeDemo.unity");
        AssetDatabase.Refresh();
        Debug.Log("MYTHFORGE_DEMO_SCENE_READY Assets/Scenes/MythForgeDemo.unity");
    }
}
