using System;
using System.Collections.Generic;
using System.IO;
using System.Text;
using UnityEditor;
using UnityEditor.Experimental.GraphView;
using UnityEngine;
using UnityEngine.UIElements;

namespace MythForge.Editor
{
    /// <summary>
    /// Graph editor window shared by every content kind. Subclasses (dialogue,
    /// skills) only override the catalog, folder and labels; all editing,
    /// validation and preview logic stays in one place. Validation/preview
    /// delegate to the native `mf` CLI so the editor and CI share exactly one
    /// compiler.
    /// </summary>
    public class QuestGraphWindow : EditorWindow
    {
        private const string LastQuestPrefix = "MythForge.Last.";

        protected virtual string ContentKind
        {
            get { return "quest"; }
        }

        protected virtual string WindowTitle
        {
            get { return "MythForge Quest"; }
        }

        protected virtual string ContentLabel
        {
            get { return "任务"; }
        }

        protected virtual MfNodeCatalog NodeCatalog
        {
            get { return MfQuestNodeCatalog.Catalog; }
        }

        protected virtual string ContentFolder
        {
            get { return "quests"; }
        }

        protected virtual string DemoFile
        {
            get { return "q_forest_wolves.json"; }
        }

        protected virtual string DefaultEvents
        {
            get { return "# 每行一个事件：名称 字段=值\nKill target=Wolf\nKill target=Wolf\nTalk npc=Elder\n"; }
        }

        private string LastQuestKey
        {
            get { return LastQuestPrefix + ContentKind; }
        }

        protected MfGraphDocument _document;
        private string _path;
        private MfGraphView _graphView;
        protected MfGraphUndoStack _undo;
        private readonly List<MfDiagnostic> _diagnostics = new List<MfDiagnostic>();
        private IMGUIContainer _inspector;
        private ScrollView _diagnosticsList;
        private Label _status;
        private TextField _searchField;
        private TextField _eventsField;
        private Label _previewOutput;
        private MfNodeView _selected;
        private string _selectedNodeId;
        private bool _dirty;

        [MenuItem("MythForge/任务图编辑器 %#q")]
        public static void Open()
        {
            var window = GetWindow<QuestGraphWindow>();
            window.titleContent = new GUIContent("MythForge Quest");
            window.minSize = new Vector2(900, 560);
            window.Show();
        }
        [MenuItem("MythForge/打开示例任务")]
        public static void OpenDemo()
        {
            Open();
            var window = GetWindow<QuestGraphWindow>();
            window.LoadDemoQuest();
        }

        /// <summary>Opens a specific quest file (used by the live preview window).</summary>
        public static QuestGraphWindow OpenFile(string path)
        {
            Open();
            QuestGraphWindow window = GetWindow<QuestGraphWindow>();
            window.LoadExternalFile(path);
            return window;
        }

        public void LoadExternalFile(string path)
        {
            LoadFile(path);
        }

        public void FocusNode(string nodeId)
        {
            if (_graphView != null && !string.IsNullOrEmpty(nodeId)) _graphView.FocusNode(nodeId);
        }

        private void OnEnable()
        {
            if (_undo == null) _undo = new MfGraphUndoStack();
            EditorApplication.update += PollSelection;
            titleContent = new GUIContent(WindowTitle);
        }

        private void OnDisable()
        {
            EditorApplication.update -= PollSelection;
        }

        // GraphView in 2022.3 has no selectionChanged event; polling one string
        // per editor tick keeps every selection path (click, rubber band, undo,
        // rebuild) in sync without relying on internal callbacks.
        private void PollSelection()
        {
            if (_graphView == null) return;
            MfNodeView node = _graphView.SelectedNodeView();
            string id = node != null ? node.Data.Id : null;
            if (id == _selectedNodeId) return;
            _selectedNodeId = id;
            _selected = node;
            if (_inspector != null) _inspector.MarkDirtyRepaint();
        }

        private void CreateGUI()
        {
            BuildUi();
            if (_document != null) return;
            string last = EditorPrefs.GetString(LastQuestKey, string.Empty);
            if (!string.IsNullOrEmpty(last) && File.Exists(last))
            {
                LoadFile(last);
            }
            else
            {
                LoadDemoQuest();
            }
        }

        private void BuildUi()
        {
            VisualElement root = rootVisualElement;
            root.style.flexDirection = FlexDirection.Column;

            // Toolbar.
            var toolbar = new VisualElement();
            toolbar.style.flexDirection = FlexDirection.Row;
            toolbar.style.paddingLeft = 6;
            toolbar.style.paddingRight = 6;
            toolbar.style.paddingTop = 4;
            toolbar.style.paddingBottom = 4;
            toolbar.style.borderBottomWidth = 1;
            toolbar.style.borderBottomColor = new Color(0.15f, 0.15f, 0.15f);
            toolbar.Add(MakeButton("新建", NewDocument));
            toolbar.Add(MakeButton("打开", OpenDocument));
            toolbar.Add(MakeButton("保存", () => SaveDocument()));
            toolbar.Add(MakeButton("另存为", () => SaveDocumentAs()));
            toolbar.Add(MakeSeparator());
            toolbar.Add(MakeButton("撤销", Undo));
            toolbar.Add(MakeButton("重做", Redo));
            toolbar.Add(MakeSeparator());
            toolbar.Add(MakeButton("校验", ValidateDocument));
            toolbar.Add(MakeButton("构建内容", BuildContent));
            toolbar.Add(MakeButton("Agent 修复", RunAgentFix));
            toolbar.Add(MakeSeparator());

            _searchField = new TextField();
            _searchField.style.width = 200;
            _searchField.style.marginLeft = 4;
            _searchField.RegisterValueChangedCallback(evt => ApplySearch(evt.newValue));
            toolbar.Add(_searchField);
            BuildToolbarExtras(toolbar);
            root.Add(toolbar);

            // Graph + side panel.
            var body = new VisualElement();
            body.style.flexDirection = FlexDirection.Row;
            body.style.flexGrow = 1;
            body.style.minHeight = 260;

            _graphView = new MfGraphView(NodeCatalog);
            _graphView.GraphChanged += OnGraphChanged;
            _graphView.CreateRequested += OnCreateRequested;
            body.Add(_graphView);

            var side = new VisualElement();
            side.style.width = 330;
            side.style.borderLeftWidth = 1;
            side.style.borderLeftColor = new Color(0.15f, 0.15f, 0.15f);
            side.style.flexDirection = FlexDirection.Column;

            var sideTitle = new Label("检查器");
            sideTitle.style.unityFontStyleAndWeight = FontStyle.Bold;
            sideTitle.style.paddingLeft = 6;
            sideTitle.style.paddingTop = 4;
            side.Add(sideTitle);

            _inspector = new IMGUIContainer(DrawInspector);
            _inspector.style.flexGrow = 1;
            _inspector.style.minHeight = 220;
            side.Add(_inspector);

            var diagnosticsTitle = new Label("诊断");
            diagnosticsTitle.style.unityFontStyleAndWeight = FontStyle.Bold;
            diagnosticsTitle.style.paddingLeft = 6;
            diagnosticsTitle.style.paddingTop = 4;
            side.Add(diagnosticsTitle);

            _diagnosticsList = new ScrollView();
            _diagnosticsList.style.height = 150;
            _diagnosticsList.style.borderTopWidth = 1;
            _diagnosticsList.style.borderTopColor = new Color(0.15f, 0.15f, 0.15f);
            side.Add(_diagnosticsList);

            body.Add(side);
            root.Add(body);

            // Preview panel.
            var preview = new VisualElement();
            preview.style.height = 170;
            preview.style.borderTopWidth = 1;
            preview.style.borderTopColor = new Color(0.15f, 0.15f, 0.15f);
            preview.style.flexDirection = FlexDirection.Row;

            var eventsColumn = new VisualElement();
            eventsColumn.style.width = 330;
            eventsColumn.style.paddingLeft = 6;
            eventsColumn.style.paddingTop = 4;
            var eventsLabel = new Label("预览事件序列");
            eventsLabel.style.unityFontStyleAndWeight = FontStyle.Bold;
            eventsColumn.Add(eventsLabel);
            _eventsField = new TextField();
            _eventsField.multiline = true;
            _eventsField.value = DefaultEvents;
            _eventsField.style.flexGrow = 1;
            eventsColumn.Add(_eventsField);
            eventsColumn.Add(MakeButton("运行预览", RunPreview));
            preview.Add(eventsColumn);

            var outputColumn = new VisualElement();
            outputColumn.style.flexGrow = 1;
            outputColumn.style.paddingLeft = 8;
            outputColumn.style.paddingTop = 4;
            var outputTitle = new Label("运行结果");
            outputTitle.style.unityFontStyleAndWeight = FontStyle.Bold;
            outputColumn.Add(outputTitle);
            _previewOutput = new Label("尚未运行");
            _previewOutput.style.whiteSpace = WhiteSpace.Normal;
            var outputScroll = new ScrollView();
            outputScroll.style.flexGrow = 1;
            outputScroll.Add(_previewOutput);
            outputColumn.Add(outputScroll);
            preview.Add(outputColumn);
            root.Add(preview);

            // Status bar.
            _status = new Label("就绪");
            _status.style.paddingLeft = 6;
            _status.style.paddingTop = 2;
            _status.style.paddingBottom = 2;
            _status.style.fontSize = 11;
            _status.style.color = new Color(0.7f, 0.7f, 0.7f);
            root.Add(_status);

            root.RegisterCallback<KeyDownEvent>(OnKeyDown, TrickleDown.TrickleDown);
        }

        private static Button MakeButton(string text, Action onClick)
        {
            var button = new Button(onClick) { text = text };
            button.style.marginRight = 3;
            return button;
        }

        private static VisualElement MakeSeparator()
        {
            var separator = new VisualElement();
            separator.style.width = 1;
            separator.style.backgroundColor = new Color(0.3f, 0.3f, 0.3f);
            separator.style.marginLeft = 4;
            separator.style.marginRight = 6;
            return separator;
        }

        private void OnKeyDown(KeyDownEvent evt)
        {
            if (!evt.ctrlKey && !evt.commandKey) return;
            if (evt.keyCode == KeyCode.Z && !evt.shiftKey)
            {
                Undo();
                evt.StopPropagation();
            }
            else if ((evt.keyCode == KeyCode.Y) || (evt.keyCode == KeyCode.Z && evt.shiftKey))
            {
                Redo();
                evt.StopPropagation();
            }
        }

        private string DefaultQuestFolder()
        {
            return Path.Combine(Application.dataPath, "MythForgeContent", ContentFolder);
        }

        protected void LoadDemoQuest()
        {
            string demo = Path.Combine(DefaultQuestFolder(), DemoFile);
            if (File.Exists(demo))
            {
                LoadFile(demo);
            }
            else
            {
                NewDocument();
                ShowStatus("没有找到示例" + ContentLabel + "，已新建空" + ContentLabel);
            }
        }

        private bool ConfirmDiscard()
        {
            if (!_dirty) return true;
            return EditorUtility.DisplayDialog("MythForge",
                "当前" + ContentLabel + "有未保存的修改，确定要放弃吗？", "放弃", "取消");
        }

        private void NewDocument()
        {
            if (!ConfirmDiscard()) return;
            _document = MfGraphDocument.CreateNew(ContentKind, null);
            _path = null;
            _undo.Clear();
            _diagnostics.Clear();
            RebuildGraph();
            SetDirty(true);
            ShowStatus("已新建" + ContentLabel);
        }

        private void OpenDocument()
        {
            if (!ConfirmDiscard()) return;
            string selected = EditorUtility.OpenFilePanel("打开内容图", DefaultQuestFolder(), "json");
            if (string.IsNullOrEmpty(selected)) return;
            LoadFile(selected);
        }

        private void LoadFile(string path)
        {
            string text;
            try
            {
                text = File.ReadAllText(path, Encoding.UTF8);
            }
            catch (Exception exception)
            {
                ShowStatus("读取失败: " + exception.Message);
                return;
            }
            string error;
            MfGraphDocument document = MfGraphDocument.FromJson(text, out error);
            if (document == null)
            {
                EditorUtility.DisplayDialog("MythForge", "无法打开内容图:\n" + error, "确定");
                ShowStatus("打开失败: " + error);
                return;
            }
            if (!string.Equals(document.Kind, ContentKind, StringComparison.Ordinal))
            {
                ShowStatus("该文件是 " + document.Kind + " 类型，请用对应的编辑器打开");
                EditorUtility.DisplayDialog("MythForge",
                    "该文件是 " + document.Kind + " 类型，请用对应的编辑器打开。", "确定");
                return;
            }
            _document = document;
            _path = path;
            _undo.Clear();
            _diagnostics.Clear();
            EditorPrefs.SetString(LastQuestKey, path);
            RebuildGraph();
            SetDirty(false);
            ShowStatus("已打开 " + path);
        }

        private bool SaveDocument()
        {
            if (_document == null) return false;
            if (string.IsNullOrEmpty(_path)) return SaveDocumentAs();
            return WriteDocument(_path);
        }

        private bool SaveDocumentAs()
        {
            if (_document == null) return false;
            string path = EditorUtility.SaveFilePanel("保存任务图", DefaultQuestFolder(),
                _document.Id + ".json", "json");
            if (string.IsNullOrEmpty(path)) return false;
            if (!WriteDocument(path)) return false;
            _path = path;
            EditorPrefs.SetString(LastQuestKey, path);
            return true;
        }

        private bool WriteDocument(string path)
        {
            try
            {
                var directory = Path.GetDirectoryName(path);
                if (!string.IsNullOrEmpty(directory) && !Directory.Exists(directory))
                {
                    Directory.CreateDirectory(directory);
                }
                File.WriteAllText(path, _document.ToJson(), new UTF8Encoding(false));
            }
            catch (Exception exception)
            {
                EditorUtility.DisplayDialog("MythForge", "保存失败:\n" + exception.Message, "确定");
                ShowStatus("保存失败: " + exception.Message);
                return false;
            }
            ImportIfInAssets(path);
            SetDirty(false);
            ShowStatus("已保存 " + path);
            return true;
        }

        private static void ImportIfInAssets(string path)
        {
            string full = Path.GetFullPath(path).Replace('\\', '/');
            string data = Path.GetFullPath(Application.dataPath).Replace('\\', '/');
            if (!full.StartsWith(data, StringComparison.OrdinalIgnoreCase)) return;
            string assetPath = "Assets" + full.Substring(data.Length);
            AssetDatabase.ImportAsset(assetPath);
        }

        protected void RebuildGraph()
        {
            if (_graphView == null) return;
            _graphView.Rebuild(_document);
            _selected = null;
            _selectedNodeId = null;
            ApplyDiagnosticsToNodes();
            _inspector.MarkDirtyRepaint();
        }
        private void OnGraphChanged(GraphViewChange change)
        {
            if (_document == null) return;
            string before = _document.ToJson();
            bool changed = false;

            if (change.movedElements != null)
            {
                for (int i = 0; i < change.movedElements.Count; i++)
                {
                    var node = change.movedElements[i] as MfNodeView;
                    if (node == null) continue;
                    float x = node.GetPosition().position.x;
                    float y = node.GetPosition().position.y;
                    if (Math.Abs(node.Data.X - x) > 0.01f || Math.Abs(node.Data.Y - y) > 0.01f)
                    {
                        node.Data.X = x;
                        node.Data.Y = y;
                        changed = true;
                    }
                }
            }

            if (change.edgesToCreate != null)
            {
                for (int i = change.edgesToCreate.Count - 1; i >= 0; i--)
                {
                    Edge edge = change.edgesToCreate[i];
                    if (edge.output == null || edge.input == null)
                    {
                        change.edgesToCreate.RemoveAt(i);
                        continue;
                    }
                    var from = edge.output.node as MfNodeView;
                    var to = edge.input.node as MfNodeView;
                    if (from == null || to == null)
                    {
                        change.edgesToCreate.RemoveAt(i);
                        continue;
                    }
                    string reason;
                    if (!_document.Connect(from.Data.Id, to.Data.Id, out reason))
                    {
                        change.edgesToCreate.RemoveAt(i);
                        ShowStatus("连线被拒绝: " + reason);
                        continue;
                    }
                    edge.userData = new MfEdgeRef(from.Data.Id, to.Data.Id);
                    changed = true;
                }
            }

            if (change.elementsToRemove != null)
            {
                for (int i = 0; i < change.elementsToRemove.Count; i++)
                {
                    var node = change.elementsToRemove[i] as MfNodeView;
                    if (node != null)
                    {
                        if (_document.RemoveNode(node.Data.Id)) changed = true;
                        continue;
                    }
                    var edge = change.elementsToRemove[i] as Edge;
                    if (edge != null)
                    {
                        var reference = edge.userData as MfEdgeRef;
                        if (reference != null && _document.Disconnect(reference.From, reference.To))
                        {
                            changed = true;
                        }
                    }
                }
            }

            if (!changed) return;
            _undo.RecordJson(before);
            SetDirty(true);
            ApplyDiagnosticsToNodes();
        }

        private void OnCreateRequested()
        {
            if (_graphView == null || _document == null) return;
            Vector2 position;
            MfNodeTypeSpec spec = _graphView.ConsumeCreateSpec(out position);
            if (spec == null) return;
            _undo.Record(_document);
            MfGraphNode node = _document.AddNode(spec.Id, position.x, position.y);
            if (node == null) return;
            MfNodeView view = _graphView.AddNodeView(node);
            if (view != null)
            {
                _graphView.ClearSelection();
                _graphView.AddToSelection(view);
                _graphView.FrameSelection();
            }
            SetDirty(true);
            ShowStatus("已创建 " + spec.Display);
        }

        private void Undo()
        {
            MfGraphDocument previous = _undo.Undo(_document);
            if (previous == null)
            {
                ShowStatus("没有可撤销的操作");
                return;
            }
            _document = previous;
            RebuildGraph();
            SetDirty(true);
            ShowStatus("已撤销");
        }

        private void Redo()
        {
            MfGraphDocument next = _undo.Redo(_document);
            if (next == null)
            {
                ShowStatus("没有可重做的操作");
                return;
            }
            _document = next;
            RebuildGraph();
            SetDirty(true);
            ShowStatus("已重做");
        }

        private void ValidateDocument()
        {
            if (_document == null) return;
            List<MfDiagnostic> local = _document.Validate();
            if (!SaveDocument())
            {
                if (string.IsNullOrEmpty(_path))
                {
                    _diagnostics.Clear();
                    _diagnostics.AddRange(local);
                    RefreshDiagnosticsPanel();
                    ApplyDiagnosticsToNodes();
                    ShowStatus("未保存，只运行了本地检查");
                    return;
                }
            }
            MfCheckResult result = MfCli.Check(_path);
            _diagnostics.Clear();
            if (!string.IsNullOrEmpty(result.Error))
            {
                ShowStatus("校验失败: " + result.Error);
            }
            else
            {
                _diagnostics.AddRange(result.Diagnostics);
            }
            // The native compiler is authoritative; show its diagnostics plus
            // any local structural issues it did not repeat.
            for (int i = 0; i < local.Count; i++)
            {
                bool duplicated = false;
                for (int j = 0; j < _diagnostics.Count; j++)
                {
                    if (_diagnostics[j].Message == local[i].Message &&
                        _diagnostics[j].NodeId == local[i].NodeId)
                    {
                        duplicated = true;
                        break;
                    }
                }
                if (!duplicated) _diagnostics.Add(local[i]);
            }
            RefreshDiagnosticsPanel();
            ApplyDiagnosticsToNodes();
            int errors = 0;
            for (int i = 0; i < _diagnostics.Count; i++)
            {
                if (_diagnostics[i].Severity == MfSeverity.Error) errors++;
            }
            ShowStatus(errors == 0
                ? "校验通过，" + _diagnostics.Count + " 条提示"
                : "校验发现 " + errors + " 个错误");
        }

        private void BuildContent()
        {
            if (_document == null) return;
            if (!SaveDocument()) return;
            string contentRoot = MfCli.FindContentRoot(_path);
            if (string.IsNullOrEmpty(contentRoot))
            {
                ShowStatus("无法定位内容根目录");
                return;
            }
            MfProcessResult result = MfCli.Build(contentRoot);
            string output = result.StdOut + result.StdErr;
            Debug.Log("[MythForge] mf build\n" + output);
            string summary = output.Trim();
            int newline = summary.LastIndexOf('\n');
            if (newline >= 0) summary = summary.Substring(newline + 1);
            ShowStatus("构建完成: " + summary);
        }

        private void RunAgentFix()
        {
            if (_document == null) return;
            if (!SaveDocument())
            {
                ShowStatus("请先保存内容，再运行 Agent 修复");
                return;
            }
            MfAgentResult result = MfCli.AgentFix(_path);
            if (!string.IsNullOrEmpty(result.Error))
            {
                ShowStatus("Agent 失败: " + result.Error);
                return;
            }
            if (result.Changed) LoadFile(_path);
            ShowStatus("Agent 修复 " + result.FixedCount + " 项，剩余 " +
                       result.RemainingCount + " 个错误");
        }

        private void RunPreview()
        {
            if (_document == null) return;
            string error;
            string eventsJson = BuildEventsJson(_eventsField.value, out error);
            if (eventsJson == null)
            {
                _previewOutput.text = "事件格式错误: " + error;
                return;
            }
            if (!SaveDocument()) return;
            MfSimResult result = MfCli.Simulate(_path, eventsJson);
            var builder = new StringBuilder();
            if (!string.IsNullOrEmpty(result.Error))
            {
                builder.AppendLine("错误: " + result.Error);
            }
            builder.AppendLine("emits (" + result.Emits.Count + "):");
            for (int i = 0; i < result.Emits.Count; i++) builder.AppendLine("  " + result.Emits[i]);
            if (result.Calls.Count > 0)
            {
                builder.AppendLine("calls:");
                for (int i = 0; i < result.Calls.Count; i++) builder.AppendLine("  " + result.Calls[i]);
            }
            _previewOutput.text = builder.ToString();
            ShowStatus(result.Ok ? "预览运行完成" : "预览运行失败");
        }

        private void RefreshDiagnosticsPanel()
        {
            if (_diagnosticsList == null) return;
            _diagnosticsList.Clear();
            if (_diagnostics.Count == 0)
            {
                _diagnosticsList.Add(new Label("没有诊断"));
                return;
            }
            for (int i = 0; i < _diagnostics.Count; i++)
            {
                MfDiagnostic diagnostic = _diagnostics[i];
                var button = new Button(() => FocusDiagnostic(diagnostic))
                {
                    text = diagnostic.ToDisplayString()
                };
                button.style.whiteSpace = WhiteSpace.Normal;
                button.style.unityTextAlign = TextAnchor.MiddleLeft;
                button.style.fontSize = 11;
                _diagnosticsList.Add(button);
            }
        }

        private void ApplyDiagnosticsToNodes()
        {
            if (_graphView == null) return;
            foreach (MfNodeView view in _graphView.NodeViews)
            {
                var matches = new List<MfDiagnostic>();
                for (int i = 0; i < _diagnostics.Count; i++)
                {
                    if (!string.IsNullOrEmpty(_diagnostics[i].NodeId) &&
                        _diagnostics[i].NodeId == view.Data.Id)
                    {
                        matches.Add(_diagnostics[i]);
                    }
                }
                view.SetDiagnostics(matches);
            }
        }

        private void FocusDiagnostic(MfDiagnostic diagnostic)
        {
            if (string.IsNullOrEmpty(diagnostic.NodeId)) return;
            _graphView.FocusNode(diagnostic.NodeId);
            ShowStatus(diagnostic.ToDisplayString());
        }

        private void ApplySearch(string query)
        {
            if (_graphView == null) return;
            foreach (MfNodeView view in _graphView.NodeViews)
            {
                bool match = MfSearch.MatchesNode(query, view.Spec);
                view.style.opacity = match ? 1f : 0.25f;
            }
        }

        protected void SetDirty(bool dirty)
        {
            _dirty = dirty;
            titleContent = new GUIContent(WindowTitle + (dirty ? " *" : string.Empty));
        }

        protected MfNodeView SelectedNodeView
        {
            get { return _selected; }
        }

        protected void ShowStatus(string message)
        {
            if (_status != null) _status.text = message;
        }

        // ------------------------------------------------------------------
        // Inspector
        // ------------------------------------------------------------------

        private void DrawInspector()
        {
            if (_document == null)
            {
                EditorGUILayout.HelpBox("新建或打开一个内容图。", MessageType.Info);
                return;
            }
            if (_selected == null)
            {
                DrawQuestInspector();
                return;
            }
            DrawNodeInspector(_selected);
        }

        private void DrawQuestInspector()
        {
            EditorGUILayout.LabelField(ContentLabel + "属性", EditorStyles.boldLabel);
            EditorGUI.BeginChangeCheck();
            string id = EditorGUILayout.DelayedTextField(ContentLabel + " ID", _document.Id);
            string title = EditorGUILayout.DelayedTextField("标题", _document.Title);
            if (EditorGUI.EndChangeCheck())
            {
                _undo.Record(_document);
                _document.Id = id;
                _document.Title = title;
                SetDirty(true);
            }
            EditorGUILayout.HelpBox(
                "点数: " + _document.Nodes.Count + "，连线: " + _document.Edges.Count +
                "\n在画布空白处右键可以创建节点。", MessageType.None);
            if (GUILayout.Button("全图适配"))
            {
                _graphView.FrameAll();
            }
        }

        private void DrawNodeInspector(MfNodeView view)
        {
            MfNodeTypeSpec spec = view.Spec;
            EditorGUILayout.LabelField(spec.Display, EditorStyles.boldLabel);
            EditorGUILayout.LabelField("类型", spec.Id);
            EditorGUILayout.LabelField("节点 ID", view.Data.Id);

            for (int i = 0; i < spec.Props.Count; i++)
            {
                MfPropSpec prop = spec.Props[i];
                MfJsonValue value;
                if (!view.Data.Props.TryGetValue(prop.Name, out value))
                {
                    value = prop.CreateDefault();
                }
                EditorGUI.BeginChangeCheck();
                MfJsonValue next = null;
                switch (prop.Kind)
                {
                    case MfPropKind.String:
                        if (prop.Choices != null && prop.Choices.Length > 0)
                        {
                            int index = Array.IndexOf(prop.Choices, value.AsString());
                            if (index < 0) index = 0;
                            index = EditorGUILayout.Popup(prop.Label, index, prop.Choices);
                            next = MfJsonValue.String(prop.Choices[index]);
                        }
                        else
                        {
                            string text = EditorGUILayout.DelayedTextField(prop.Label, value.AsString());
                            next = MfJsonValue.String(text);
                        }
                        break;
                    case MfPropKind.Int:
                        long number = EditorGUILayout.LongField(prop.Label, value.AsInt());
                        if (prop.Min != long.MinValue) number = Math.Max(number, prop.Min);
                        if (prop.Max != long.MaxValue) number = Math.Min(number, prop.Max);
                        next = MfJsonValue.Number(number);
                        break;
                    case MfPropKind.Float:
                        double real = EditorGUILayout.DoubleField(prop.Label,
                            value.IsNumber ? value.AsDouble() : 0.0);
                        next = MfJsonValue.Number(real);
                        break;
                    case MfPropKind.Bool:
                        bool flag = EditorGUILayout.Toggle(prop.Label, value.AsBool());
                        next = MfJsonValue.Bool(flag);
                        break;
                }
                if (EditorGUI.EndChangeCheck() && next != null)
                {
                    _undo.Record(_document);
                    view.Data.Props[prop.Name] = next;
                    view.RefreshSummary();
                    SetDirty(true);
                }
                if (prop.Required)
                {
                    EditorGUILayout.LabelField(" ", "必填", EditorStyles.miniLabel);
                }
            }

            EditorGUILayout.Space();
            DrawNodeExtras(view);
            EditorGUILayout.Space();
            if (GUILayout.Button("删除节点"))
            {
                if (EditorUtility.DisplayDialog("MythForge", "删除节点 " + view.Data.Id + "？", "删除", "取消"))
                {
                    _undo.Record(_document);
                    _document.RemoveNode(view.Data.Id);
                    RebuildGraph();
                    SetDirty(true);
                }
            }
        }

        /// <summary>Hook for kind-specific inspector widgets (e.g. Timeline).</summary>
        protected virtual void DrawNodeExtras(MfNodeView view)
        {
        }

        /// <summary>Hook for kind-specific toolbar buttons.</summary>
        protected virtual void BuildToolbarExtras(VisualElement toolbar)
        {
        }

        // ------------------------------------------------------------------
        // Preview event parsing
        // ------------------------------------------------------------------

        private sealed class RawToken
        {
            public string Text;
            public bool Quoted;
        }

        private static List<RawToken> Tokenize(string line)
        {
            var tokens = new List<RawToken>();
            var builder = new StringBuilder();
            bool inQuotes = false;
            bool quoted = false;
            bool any = false;
            for (int i = 0; i < line.Length; i++)
            {
                char c = line[i];
                if (c == '"')
                {
                    inQuotes = !inQuotes;
                    quoted = true;
                    any = true;
                    continue;
                }
                if (!inQuotes && (c == ' ' || c == '\t'))
                {
                    if (any)
                    {
                        tokens.Add(new RawToken { Text = builder.ToString(), Quoted = quoted });
                        builder.Length = 0;
                        quoted = false;
                        any = false;
                    }
                    continue;
                }
                builder.Append(c);
                any = true;
            }
            if (any)
            {
                tokens.Add(new RawToken { Text = builder.ToString(), Quoted = quoted });
            }
            return tokens;
        }

        private static string BuildEventsJson(string text, out string error)
        {
            error = null;
            var root = MfJsonValue.Object();
            var events = MfJsonValue.Array();
            string[] lines = (text ?? string.Empty).Split('\n');
            for (int lineIndex = 0; lineIndex < lines.Length; lineIndex++)
            {
                string line = lines[lineIndex].Trim();
                if (line.Length == 0 || line.StartsWith("#", StringComparison.Ordinal)) continue;
                List<RawToken> tokens = Tokenize(line);
                if (tokens.Count == 0) continue;
                var item = MfJsonValue.Object();
                item["name"] = MfJsonValue.String(tokens[0].Text);
                var fields = MfJsonValue.Object();
                for (int i = 1; i < tokens.Count; i++)
                {
                    string token = tokens[i].Text;
                    int equals = token.IndexOf('=');
                    if (equals <= 0)
                    {
                        error = "第 " + (lineIndex + 1) + " 行：字段必须是 key=value 形式";
                        return null;
                    }
                    string key = token.Substring(0, equals);
                    string rawValue = token.Substring(equals + 1);
                    fields[key] = ConvertValue(rawValue, tokens[i].Quoted);
                }
                item["fields"] = fields;
                events.Add(item);
            }
            root["events"] = events;
            return root.ToJson();
        }

        private static MfJsonValue ConvertValue(string raw, bool quoted)
        {
            if (quoted) return MfJsonValue.String(raw);
            long integer;
            if (long.TryParse(raw, out integer)) return MfJsonValue.Number(integer);
            double real;
            if (double.TryParse(raw, System.Globalization.NumberStyles.Float,
                    System.Globalization.CultureInfo.InvariantCulture, out real))
            {
                return MfJsonValue.Number(real);
            }
            if (string.Equals(raw, "true", StringComparison.OrdinalIgnoreCase)) return MfJsonValue.Bool(true);
            if (string.Equals(raw, "false", StringComparison.OrdinalIgnoreCase)) return MfJsonValue.Bool(false);
            return MfJsonValue.String(raw);
        }
    }
}
