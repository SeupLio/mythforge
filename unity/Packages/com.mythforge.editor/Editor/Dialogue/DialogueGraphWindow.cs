using System;
using System.IO;
using UnityEditor;
using UnityEngine;
using UnityEngine.UIElements;

namespace MythForge.Editor
{
    /// <summary>
    /// Dialogue graph editor: a quest-style linear graph plus Timeline linkage.
    /// Timeline nodes are bound to a TimelineAsset by project path; the lowerer
    /// turns them into PlayTimeline calls and (optionally) a TimelineFinished wait.
    /// </summary>
    public sealed class DialogueGraphWindow : QuestGraphWindow
    {
        protected override string ContentKind
        {
            get { return "dialogue"; }
        }

        protected override string WindowTitle
        {
            get { return "MythForge Dialogue"; }
        }

        protected override string ContentLabel
        {
            get { return "剧情"; }
        }

        protected override MfNodeCatalog NodeCatalog
        {
            get { return MfDialogueNodeCatalog.Catalog; }
        }

        protected override string ContentFolder
        {
            get { return "dialogues"; }
        }

        protected override string DemoFile
        {
            get { return "d_elder_intro.json"; }
        }

        protected override string DefaultEvents
        {
            get
            {
                return "# 剧情预览事件：名称 字段=值\n" +
                       "DialogueAdvance\n" +
                       "DialogueChoiceSelected\n" +
                       "TimelineFinished asset=\"Assets/MythForgeContent/Timelines/TL_ElderIntro.playable\"\n" +
                       "DialogueAdvance\n";
            }
        }

        [MenuItem("MythForge/剧情编辑器 %#d")]
        public static void Open()
        {
            var window = GetWindow<DialogueGraphWindow>();
            window.titleContent = new GUIContent("MythForge Dialogue");
            window.minSize = new Vector2(900, 560);
            window.Show();
        }

        [MenuItem("MythForge/打开示例剧情")]
        public static void OpenDemo()
        {
            Open();
            var window = GetWindow<DialogueGraphWindow>();
            window.LoadDemoQuest();
        }

        public static DialogueGraphWindow OpenFile(string path)
        {
            Open();
            DialogueGraphWindow window = GetWindow<DialogueGraphWindow>();
            window.LoadExternalFile(path);
            return window;
        }

        protected override void BuildToolbarExtras(VisualElement toolbar)
        {
            var button = new Button(OpenSelectedTimeline) { text = "Timeline 联动" };
            button.style.marginLeft = 6;
            button.tooltip = "定位或打开当前选中 Timeline 节点的资产";
            toolbar.Add(button);
        }

        private void OpenSelectedTimeline()
        {
            MfNodeView view = SelectedNodeView;
            if (view == null || view.Data.Type != "dialogue.timeline")
            {
                ShowStatus("请先选中一个 Timeline 演出节点");
                return;
            }
            string asset = PropString(view, "asset");
            if (string.IsNullOrEmpty(asset))
            {
                ShowStatus("该 Timeline 节点还没有绑定资产");
                return;
            }
            bool opened = MfTimelineBridge.Open(asset);
            ShowStatus(opened
                ? "已打开 Timeline: " + asset
                : "已在 Project 中定位（该 Unity 版本不支持脚本打开 Timeline）");
        }

        protected override void DrawNodeExtras(MfNodeView view)
        {
            if (_document == null || view.Data.Type != "dialogue.timeline") return;
            EditorGUILayout.LabelField("Timeline 联动", EditorStyles.boldLabel);
            string asset = PropString(view, "asset");
            EditorGUILayout.LabelField("资产", string.IsNullOrEmpty(asset) ? "未指定" : asset);

            if (GUILayout.Button("选择 Timeline 资产…"))
            {
                string picked = EditorUtility.OpenFilePanel("选择 Timeline 资产",
                    Application.dataPath, "playable");
                if (!string.IsNullOrEmpty(picked))
                {
                    SetTimelineAsset(view, MfTimelineBridge.ToProjectPath(picked));
                }
            }

            using (new EditorGUILayout.HorizontalScope())
            {
                using (new EditorGUI.DisabledScope(string.IsNullOrEmpty(asset)))
                {
                    if (GUILayout.Button("在 Project 中定位"))
                    {
                        ShowStatus(MfTimelineBridge.Ping(asset)
                            ? "已定位 " + asset
                            : "工程中找不到该 TimelineAsset");
                    }
                    if (GUILayout.Button("打开 Timeline"))
                    {
                        bool opened = MfTimelineBridge.Open(asset);
                        ShowStatus(opened
                            ? "已打开 Timeline: " + asset
                            : "已在 Project 中定位（该 Unity 版本不支持脚本打开 Timeline）");
                    }
                }
            }

            if (!string.IsNullOrEmpty(asset) && !MfTimelineBridge.Exists(asset))
            {
                EditorGUILayout.HelpBox(
                    "在工程中找不到该 TimelineAsset。编辑与编译不受影响，运行时 PlayTimeline 会收到该路径。",
                    MessageType.Warning);
            }
            EditorGUILayout.HelpBox(
                "Timeline 节点会降级为 call PlayTimeline(asset)；勾选“等待播放完成”时再等待 TimelineFinished(asset)。",
                MessageType.None);
        }

        private void SetTimelineAsset(MfNodeView view, string asset)
        {
            if (string.IsNullOrEmpty(asset)) return;
            _undo.Record(_document);
            view.Data.Props["asset"] = MfJsonValue.String(asset);
            view.RefreshSummary();
            SetDirty(true);
            ShowStatus("已绑定 Timeline: " + asset);
        }

        private static string PropString(MfNodeView view, string name)
        {
            MfJsonValue value;
            if (view.Data.Props.TryGetValue(name, out value) && value.IsString) return value.AsString();
            return string.Empty;
        }
    }
}
