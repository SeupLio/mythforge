using System;
using System.Collections.Generic;
using System.IO;
using System.Text;
using UnityEditor;
using UnityEngine;

namespace MythForge.Editor
{
    /// <summary>
    /// Skill timeline editor: a time ruler with a draggable playhead, one lane
    /// per event type, a frame list and a property inspector. The window edits
    /// the same kind=skill graph JSON the compiler consumes, so saving is the
    /// export step.
    /// </summary>
    public sealed class SkillTimelineWindow : EditorWindow
    {
        private const string LastPathKey = "MythForge.Last.skill";
        private const float RulerHeight = 20f;
        private const float LaneLabelWidth = 64f;
        private const float SidePanelWidth = 300f;

        private MfGraphDocument _document;
        private string _path;
        private readonly MfGraphUndoStack _undo = new MfGraphUndoStack();
        private readonly List<MfDiagnostic> _diagnostics = new List<MfDiagnostic>();
        private readonly Dictionary<string, Rect> _markerRects =
            new Dictionary<string, Rect>(StringComparer.Ordinal);
        private string _selectedId;
        private long _playheadMs;
        private bool _playing;
        private double _lastUpdateTime;
        private bool _dirty;
        private string _status = "就绪";
        private string _output = string.Empty;
        private Vector2 _frameScroll;
        private Vector2 _inspectorScroll;
        private Vector2 _outputScroll;
        private bool _draggingPlayhead;
        private string _draggingFrameId;
        private bool _dragRecorded;

        [MenuItem("MythForge/技能时间轴编辑器 %#e")]
        public static void Open()
        {
            var window = GetWindow<SkillTimelineWindow>();
            window.titleContent = new GUIContent("MythForge Skill");
            window.minSize = new Vector2(1000, 600);
            window.Show();
        }

        [MenuItem("MythForge/打开示例技能")]
        public static void OpenDemo()
        {
            Open();
            var window = GetWindow<SkillTimelineWindow>();
            window.LoadDemoSkill();
        }

        public static SkillTimelineWindow OpenFile(string path)
        {
            Open();
            SkillTimelineWindow window = GetWindow<SkillTimelineWindow>();
            window.LoadExternalFile(path);
            return window;
        }

        public void LoadExternalFile(string path)
        {
            LoadFile(path);
        }

        public int FrameCount
        {
            get { return _document != null ? MfSkillTimeline.Frames(_document).Count : -1; }
        }

        private string DefaultSkillFolder()
        {
            return Path.Combine(Application.dataPath, "MythForgeContent", "skills");
        }

        private void OnEnable()
        {
            EditorApplication.update += Tick;
            titleContent = new GUIContent(WindowTitleText);
            if (_document != null) return;
            string last = EditorPrefs.GetString(LastPathKey, string.Empty);
            if (!string.IsNullOrEmpty(last) && File.Exists(last))
            {
                LoadFile(last);
            }
            else
            {
                LoadDemoSkill();
            }
        }

        private void OnDisable()
        {
            EditorApplication.update -= Tick;
        }

        private string WindowTitleText
        {
            get { return "MythForge Skill" + (_dirty ? " *" : string.Empty); }
        }

        private void LoadDemoSkill()
        {
            string demo = Path.Combine(DefaultSkillFolder(), "sk_wolf_bite.json");
            if (File.Exists(demo))
            {
                LoadFile(demo);
            }
            else
            {
                NewDocument();
                SetStatus("没有找到示例技能，已新建空技能");
            }
        }

        private void Tick()
        {
            if (!_playing) return;
            double now = EditorApplication.timeSinceStartup;
            if (_lastUpdateTime <= 0.0) _lastUpdateTime = now;
            double delta = Math.Max(0.0, now - _lastUpdateTime);
            _lastUpdateTime = now;
            _playheadMs += (long)(delta * 1000.0);
            long duration = Math.Max(2000, MfSkillTimeline.DurationMs(_document) + 500);
            if (_playheadMs >= duration)
            {
                _playheadMs = duration;
                _playing = false;
            }
            Repaint();
        }

        private void OnGUI()
        {
            DrawToolbar();
            if (_document == null)
            {
                EditorGUILayout.HelpBox("新建或打开一个技能图。", MessageType.Info);
                DrawStatusBar();
                return;
            }

            GUILayout.BeginHorizontal();
            DrawFrameList();
            DrawTimeline();
            DrawInspectorPanel();
            GUILayout.EndHorizontal();

            DrawOutputPanel();
            DrawStatusBar();
        }

        private void DrawToolbar()
        {
            GUILayout.BeginHorizontal(EditorStyles.toolbar);
            if (GUILayout.Button("新建", EditorStyles.toolbarButton)) NewDocument();
            if (GUILayout.Button("打开", EditorStyles.toolbarButton)) OpenDocument();
            if (GUILayout.Button("保存", EditorStyles.toolbarButton)) SaveDocument();
            if (GUILayout.Button("另存为", EditorStyles.toolbarButton)) SaveDocumentAs();
            GUILayout.Space(6);
            if (GUILayout.Button("撤销", EditorStyles.toolbarButton)) Undo();
            if (GUILayout.Button("重做", EditorStyles.toolbarButton)) Redo();
            GUILayout.Space(6);
            if (GUILayout.Button("校验", EditorStyles.toolbarButton)) ValidateDocument();
            if (GUILayout.Button("构建内容", EditorStyles.toolbarButton)) BuildContent();
            if (GUILayout.Button("Agent 修复", EditorStyles.toolbarButton)) RunAgentFix();
            if (GUILayout.Button("预览运行", EditorStyles.toolbarButton)) RunPreview();
            GUILayout.FlexibleSpace();
            if (GUILayout.Button("回到起点", EditorStyles.toolbarButton))
            {
                _playheadMs = 0;
            }
            _playing = GUILayout.Toggle(_playing, "播放", EditorStyles.toolbarButton);
            if (GUILayout.Button("+ 在播放头添加帧", EditorStyles.toolbarButton)) AddFrameAtPlayhead();
            GUILayout.EndHorizontal();

            // Undo/redo are drawn disabled above; they are re-enabled here by
            // handling shortcuts, which keeps the toolbar compact.
            Event evt = Event.current;
            if (evt.type == EventType.KeyDown && (evt.control || evt.command))
            {
                if (evt.keyCode == KeyCode.Z && !evt.shift)
                {
                    Undo();
                    evt.Use();
                }
                else if (evt.keyCode == KeyCode.Y || (evt.keyCode == KeyCode.Z && evt.shift))
                {
                    Redo();
                    evt.Use();
                }
            }
        }

        private void DrawFrameList()
        {
            GUILayout.BeginVertical(GUILayout.Width(SidePanelWidth));
            EditorGUILayout.LabelField("帧事件", EditorStyles.boldLabel);
            List<MfGraphNode> frames = MfSkillTimeline.Frames(_document);
            _frameScroll = EditorGUILayout.BeginScrollView(_frameScroll, GUILayout.ExpandHeight(true));
            if (frames.Count == 0)
            {
                EditorGUILayout.LabelField("还没有帧，点击工具栏添加。");
            }
            for (int i = 0; i < frames.Count; i++)
            {
                MfGraphNode frame = frames[i];
                bool selected = frame.Id == _selectedId;
                string label = string.Format("{0,8:F0}ms  {1,-6} {2} {3}",
                    MfSkillTimeline.TimeMs(frame), PropString(frame, "event"),
                    PropString(frame, "target"), PropDelta(frame));
                if (GUILayout.Toggle(selected, label, "Button"))
                {
                    _selectedId = frame.Id;
                    _playheadMs = MfSkillTimeline.TimeMs(frame);
                }
            }
            EditorGUILayout.EndScrollView();
            GUILayout.EndVertical();
        }

        private static string PropDelta(MfGraphNode frame)
        {
            MfJsonValue value;
            if (frame.Props.TryGetValue("amount", out value) && value.IsNumber && value.AsInt() != 0)
            {
                return (value.AsInt() > 0 ? "+" : string.Empty) + value.AsInt();
            }
            return string.Empty;
        }

        private void DrawTimeline()
        {
            GUILayout.BeginVertical(GUILayout.ExpandWidth(true), GUILayout.ExpandHeight(true));
            Rect rect = GUILayoutUtility.GetRect(10f, 100000f, 140f, 100000f,
                GUILayout.ExpandWidth(true), GUILayout.ExpandHeight(true));
            if (Event.current.type == EventType.Repaint)
            {
                DrawTimelineRepaint(rect);
            }
            HandleTimelineEvents(rect);
            GUILayout.EndVertical();
        }

        private void DrawTimelineRepaint(Rect rect)
        {
            EditorGUI.DrawRect(rect, new Color(0.13f, 0.14f, 0.16f));
            Rect ruler = new Rect(rect.x, rect.y, rect.width, RulerHeight);
            EditorGUI.DrawRect(ruler, new Color(0.19f, 0.20f, 0.23f));

            long duration = Math.Max(2000, MfSkillTimeline.DurationMs(_document) + 500);
            float left = rect.x + LaneLabelWidth;
            float width = Math.Max(10f, rect.width - LaneLabelWidth - 12f);

            // Ruler: minor tick every 100ms, label every 500ms.
            for (long ms = 0; ms <= duration; ms += 100)
            {
                float x = left + width * (ms / (float)duration);
                bool major = ms % 500 == 0;
                EditorGUI.DrawRect(new Rect(x, rect.y + (major ? 4f : 12f), 1f,
                    major ? RulerHeight - 4f : 6f), new Color(0.55f, 0.58f, 0.62f));
                if (major)
                {
                    GUI.Label(new Rect(x + 2f, rect.y + 1f, 60f, 16f), (ms / 1000f).ToString("0.0") + "s",
                        EditorStyles.miniLabel);
                }
            }

            _markerRects.Clear();
            float laneHeight = Math.Max(24f, (rect.height - RulerHeight - 10f) / MfSkillTimeline.EventTypes.Length);
            for (int lane = 0; lane < MfSkillTimeline.EventTypes.Length; lane++)
            {
                float y = rect.y + RulerHeight + lane * laneHeight;
                if (lane % 2 == 0)
                {
                    EditorGUI.DrawRect(new Rect(rect.x, y, rect.width, laneHeight),
                        new Color(0.15f, 0.16f, 0.19f));
                }
                GUI.Label(new Rect(rect.x + 6f, y + 3f, LaneLabelWidth - 8f, 18f),
                    MfSkillTimeline.EventTypes[lane], EditorStyles.miniLabel);
            }

            List<MfGraphNode> frames = MfSkillTimeline.Frames(_document);
            for (int i = 0; i < frames.Count; i++)
            {
                MfGraphNode frame = frames[i];
                string eventType = PropString(frame, "event");
                int lane = Array.IndexOf(MfSkillTimeline.EventTypes, eventType);
                if (lane < 0) lane = 0;
                float x = left + width * (MfSkillTimeline.TimeMs(frame) / (float)duration);
                float y = rect.y + RulerHeight + lane * laneHeight;
                var marker = new Rect(x - 6f, y + laneHeight * 0.5f - 6f, 12f, 12f);
                _markerRects[frame.Id] = marker;
                Color color = LaneColor(eventType);
                EditorGUI.DrawRect(new Rect(marker.x - 1f, marker.y - 1f, marker.width + 2f,
                    marker.height + 2f), frame.Id == _selectedId ? Color.white : new Color(0f, 0f, 0f, 0.6f));
                EditorGUI.DrawRect(marker, color);
            }

            float playheadX = left + width * (_playheadMs / (float)duration);
            EditorGUI.DrawRect(new Rect(playheadX - 1f, rect.y, 2f, rect.height),
                new Color(0.95f, 0.75f, 0.25f));
            GUI.Label(new Rect(playheadX + 3f, rect.y + rect.height - 18f, 90f, 16f),
                _playheadMs + "ms", EditorStyles.miniLabel);
        }

        private static Color LaneColor(string eventType)
        {
            switch (eventType)
            {
                case "damage": return new Color(0.85f, 0.30f, 0.30f);
                case "buff": return new Color(0.30f, 0.75f, 0.45f);
                case "vfx": return new Color(0.35f, 0.60f, 0.95f);
                case "sfx": return new Color(0.80f, 0.65f, 0.25f);
                case "move": return new Color(0.65f, 0.45f, 0.90f);
            }
            return new Color(0.6f, 0.6f, 0.6f);
        }

        private void HandleTimelineEvents(Rect rect)
        {
            Event evt = Event.current;
            long duration = Math.Max(2000, MfSkillTimeline.DurationMs(_document) + 500);
            float left = rect.x + LaneLabelWidth;
            float width = Math.Max(10f, rect.width - LaneLabelWidth - 12f);

            if (evt.type == EventType.MouseDown && evt.button == 0 && rect.Contains(evt.mousePosition))
            {
                string hit = null;
                foreach (var pair in _markerRects)
                {
                    if (pair.Value.Contains(evt.mousePosition))
                    {
                        hit = pair.Key;
                        break;
                    }
                }
                if (hit != null)
                {
                    _selectedId = hit;
                    _draggingFrameId = hit;
                    _dragRecorded = false;
                    evt.Use();
                    return;
                }
                _playheadMs = SnapTime(XToTime(evt.mousePosition.x, left, width, duration));
                _draggingPlayhead = true;
                _playing = false;
                evt.Use();
                return;
            }

            if (evt.type == EventType.MouseDrag && evt.button == 0)
            {
                if (_draggingFrameId != null)
                {
                    if (!_dragRecorded)
                    {
                        _undo.Record(_document);
                        _dragRecorded = true;
                    }
                    long time = SnapTime(XToTime(evt.mousePosition.x, left, width, duration));
                    if (MfSkillTimeline.MoveFrame(_document, _draggingFrameId, time))
                    {
                        _playheadMs = time;
                        SetDirty(true);
                    }
                    evt.Use();
                }
                else if (_draggingPlayhead)
                {
                    _playheadMs = SnapTime(XToTime(evt.mousePosition.x, left, width, duration));
                    evt.Use();
                }
            }

            if (evt.type == EventType.MouseUp && evt.button == 0)
            {
                _draggingFrameId = null;
                _draggingPlayhead = false;
                _dragRecorded = false;
            }
        }

        private static long SnapTime(long timeMs)
        {
            long snap = Event.current != null && Event.current.shift ? 1 : 10;
            long snapped = (timeMs + snap / 2) / snap * snap;
            return Math.Max(0, snapped);
        }

        private static long XToTime(float x, float left, float width, long duration)
        {
            float ratio = (x - left) / width;
            if (ratio < 0f) ratio = 0f;
            if (ratio > 1f) ratio = 1f;
            return (long)(ratio * duration);
        }

        private void DrawInspectorPanel()
        {
            GUILayout.BeginVertical(GUILayout.Width(SidePanelWidth));
            EditorGUILayout.LabelField("检查器", EditorStyles.boldLabel);
            _inspectorScroll = EditorGUILayout.BeginScrollView(_inspectorScroll,
                GUILayout.ExpandHeight(true));
            if (string.IsNullOrEmpty(_selectedId) || _document.FindNode(_selectedId) == null)
            {
                DrawDocumentInspector();
            }
            else
            {
                DrawFrameInspector(_document.FindNode(_selectedId));
            }
            EditorGUILayout.EndScrollView();
            GUILayout.EndVertical();
        }

        private void DrawDocumentInspector()
        {
            EditorGUILayout.LabelField("技能属性", EditorStyles.boldLabel);
            EditorGUI.BeginChangeCheck();
            string id = EditorGUILayout.DelayedTextField("技能 ID", _document.Id);
            string title = EditorGUILayout.DelayedTextField("标题", _document.Title);
            if (EditorGUI.EndChangeCheck())
            {
                _undo.Record(_document);
                _document.Id = id;
                _document.Title = title;
                SetDirty(true);
            }
            EditorGUILayout.LabelField("帧数", MfSkillTimeline.Frames(_document).Count.ToString());
            EditorGUILayout.LabelField("时长", MfSkillTimeline.DurationMs(_document) + "ms");
            EditorGUILayout.HelpBox(
                "点击时间轴添加/选择帧；拖动标记修改时间；拖动黄色播放头预览。",
                MessageType.None);
        }

        private void DrawFrameInspector(MfGraphNode frame)
        {
            EditorGUILayout.LabelField("技能帧", EditorStyles.boldLabel);
            EditorGUILayout.LabelField("节点 ID", frame.Id);

            EditorGUI.BeginChangeCheck();
            long time = EditorGUILayout.LongField("时间 (ms)", MfSkillTimeline.TimeMs(frame));
            string eventType = PropString(frame, "event");
            int index = Array.IndexOf(MfSkillTimeline.EventTypes, eventType);
            if (index < 0) index = 0;
            index = EditorGUILayout.Popup("事件", index, MfSkillTimeline.EventTypes);
            string target = EditorGUILayout.DelayedTextField("目标", PropString(frame, "target"));
            long amount = EditorGUILayout.LongField("数值",
                frame.Props.ContainsKey("amount") ? frame.Props["amount"].AsInt() : 0);
            string asset = EditorGUILayout.DelayedTextField("资源", PropString(frame, "asset"));
            if (EditorGUI.EndChangeCheck())
            {
                _undo.Record(_document);
                MfSkillTimeline.SetTimeMs(frame, time);
                frame.Props["event"] = MfJsonValue.String(MfSkillTimeline.EventTypes[index]);
                frame.Props["target"] = MfJsonValue.String(target);
                frame.Props["amount"] = MfJsonValue.Number(amount);
                frame.Props["asset"] = MfJsonValue.String(asset);
                MfSkillTimeline.RebuildChain(_document);
                SetDirty(true);
            }

            EditorGUILayout.Space();
            if (GUILayout.Button("把时间设为播放头"))
            {
                _undo.Record(_document);
                MfSkillTimeline.MoveFrame(_document, frame.Id, _playheadMs);
                SetDirty(true);
            }
            if (GUILayout.Button("删除帧"))
            {
                if (EditorUtility.DisplayDialog("MythForge", "删除帧 " + frame.Id + "？", "删除", "取消"))
                {
                    _undo.Record(_document);
                    MfSkillTimeline.RemoveFrame(_document, frame.Id);
                    _selectedId = null;
                    SetDirty(true);
                }
            }
        }

        private void DrawOutputPanel()
        {
            if (_diagnostics.Count == 0 && string.IsNullOrEmpty(_output)) return;
            GUILayout.BeginVertical(GUILayout.Height(96f));
            EditorGUILayout.LabelField("诊断 / 预览输出", EditorStyles.boldLabel);
            _outputScroll = EditorGUILayout.BeginScrollView(_outputScroll, GUILayout.Height(70f));
            for (int i = 0; i < _diagnostics.Count; i++)
            {
                EditorGUILayout.LabelField(_diagnostics[i].ToDisplayString());
            }
            if (!string.IsNullOrEmpty(_output))
            {
                EditorGUILayout.LabelField(_output);
            }
            EditorGUILayout.EndScrollView();
            GUILayout.EndVertical();
        }

        private void DrawStatusBar()
        {
            GUILayout.BeginHorizontal(EditorStyles.helpBox);
            GUILayout.Label(_status, EditorStyles.miniLabel);
            GUILayout.FlexibleSpace();
            long duration = _document != null ? MfSkillTimeline.DurationMs(_document) : 0;
            GUILayout.Label("播放头 " + _playheadMs + "ms / 时长 " + duration + "ms",
                EditorStyles.miniLabel);
            GUILayout.EndHorizontal();
        }

        private void AddFrameAtPlayhead()
        {
            if (_document == null) return;
            _undo.Record(_document);
            MfGraphNode frame = MfSkillTimeline.AddFrame(_document, _playheadMs);
            if (frame == null) return;
            _selectedId = frame.Id;
            SetDirty(true);
            SetStatus("已在 " + _playheadMs + "ms 添加帧");
        }

        private void Undo()
        {
            MfGraphDocument restored = _undo.Undo(_document);
            if (restored == null)
            {
                SetStatus("没有可撤销的操作");
                return;
            }
            _document = restored;
            _selectedId = null;
            SetDirty(true);
            SetStatus("已撤销");
        }

        private void Redo()
        {
            MfGraphDocument restored = _undo.Redo(_document);
            if (restored == null)
            {
                SetStatus("没有可重做的操作");
                return;
            }
            _document = restored;
            _selectedId = null;
            SetDirty(true);
            SetStatus("已重做");
        }

        private bool ConfirmDiscard()
        {
            if (!_dirty) return true;
            return EditorUtility.DisplayDialog("MythForge",
                "当前技能有未保存的修改，确定要放弃吗？", "放弃", "取消");
        }

        private void NewDocument()
        {
            if (!ConfirmDiscard()) return;
            _document = MfSkillTimeline.CreateDocument(null);
            _path = null;
            _undo.Clear();
            _diagnostics.Clear();
            _output = string.Empty;
            _selectedId = null;
            _playheadMs = 0;
            SetDirty(true);
            SetStatus("已新建技能");
        }

        private void OpenDocument()
        {
            if (!ConfirmDiscard()) return;
            string selected = EditorUtility.OpenFilePanel("打开技能图", DefaultSkillFolder(), "json");
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
                SetStatus("读取失败: " + exception.Message);
                return;
            }
            string error;
            MfGraphDocument document = MfGraphDocument.FromJson(text, out error);
            if (document == null)
            {
                SetStatus("打开失败: " + error);
                return;
            }
            if (!string.Equals(document.Kind, "skill", StringComparison.Ordinal))
            {
                SetStatus("该文件不是技能图: " + document.Kind);
                return;
            }
            _document = document;
            _path = path;
            _undo.Clear();
            _diagnostics.Clear();
            _output = string.Empty;
            _selectedId = null;
            _playheadMs = 0;
            EditorPrefs.SetString(LastPathKey, path);
            SetDirty(false);
            SetStatus("已打开 " + path);
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
            string path = EditorUtility.SaveFilePanel("保存技能图", DefaultSkillFolder(),
                _document.Id + ".json", "json");
            if (string.IsNullOrEmpty(path)) return false;
            if (!WriteDocument(path)) return false;
            _path = path;
            EditorPrefs.SetString(LastPathKey, path);
            return true;
        }

        private bool WriteDocument(string path)
        {
            try
            {
                string directory = Path.GetDirectoryName(path);
                if (!string.IsNullOrEmpty(directory) && !Directory.Exists(directory))
                {
                    Directory.CreateDirectory(directory);
                }
                MfSkillTimeline.RebuildChain(_document);
                File.WriteAllText(path, _document.ToJson(), new UTF8Encoding(false));
            }
            catch (Exception exception)
            {
                EditorUtility.DisplayDialog("MythForge", "保存失败:\n" + exception.Message, "确定");
                SetStatus("保存失败: " + exception.Message);
                return false;
            }
            string full = Path.GetFullPath(path).Replace('\\', '/');
            string data = Path.GetFullPath(Application.dataPath).Replace('\\', '/');
            if (full.StartsWith(data, StringComparison.OrdinalIgnoreCase))
            {
                AssetDatabase.ImportAsset("Assets" + full.Substring(data.Length));
            }
            SetDirty(false);
            SetStatus("已保存 " + path);
            return true;
        }

        private void ValidateDocument()
        {
            if (_document == null) return;
            _diagnostics.Clear();
            _diagnostics.AddRange(_document.Validate());
            if (!SaveDocument())
            {
                SetStatus("未保存，只运行了本地检查");
                return;
            }
            MfCheckResult result = MfCli.Check(_path);
            if (!string.IsNullOrEmpty(result.Error))
            {
                SetStatus("校验失败: " + result.Error);
                return;
            }
            _diagnostics.AddRange(result.Diagnostics);
            int errors = 0;
            for (int i = 0; i < _diagnostics.Count; i++)
            {
                if (_diagnostics[i].Severity == MfSeverity.Error) errors++;
            }
            SetStatus(errors == 0
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
                SetStatus("无法定位内容根目录");
                return;
            }
            MfProcessResult result = MfCli.Build(contentRoot);
            string output = result.StdOut + result.StdErr;
            Debug.Log("[MythForge] mf build\n" + output);
            _output = output.Trim();
            string summary = _output;
            int newline = summary.LastIndexOf('\n');
            if (newline >= 0) summary = summary.Substring(newline + 1);
            SetStatus("构建完成: " + summary);
        }

        private void RunAgentFix()
        {
            if (_document == null) return;
            if (!SaveDocument())
            {
                SetStatus("请先保存内容，再运行 Agent 修复");
                return;
            }
            MfAgentResult result = MfCli.AgentFix(_path);
            if (!string.IsNullOrEmpty(result.Error))
            {
                SetStatus("Agent 失败: " + result.Error);
                return;
            }
            if (result.Changed) LoadFile(_path);
            SetStatus("Agent 修复 " + result.FixedCount + " 项，剩余 " +
                      result.RemainingCount + " 个错误");
        }

        private void RunPreview()
        {
            if (_document == null) return;
            if (!SaveDocument()) return;
            MfSimResult result = MfCli.Simulate(_path, "{\"events\":[]}");
            var builder = new StringBuilder();
            if (!string.IsNullOrEmpty(result.Error)) builder.AppendLine("错误: " + result.Error);
            for (int i = 0; i < result.Emits.Count; i++) builder.AppendLine("emit " + result.Emits[i]);
            for (int i = 0; i < result.Calls.Count; i++) builder.AppendLine("call " + result.Calls[i]);
            _output = builder.ToString().Trim();
            SetStatus(result.Ok ? "预览运行完成" : "预览运行失败");
        }

        private void SetDirty(bool dirty)
        {
            _dirty = dirty;
            titleContent = new GUIContent(WindowTitleText);
            Repaint();
        }

        private void SetStatus(string message)
        {
            _status = message;
            Repaint();
        }

        private static string PropString(MfGraphNode node, string name)
        {
            MfJsonValue value;
            if (node.Props.TryGetValue(name, out value) && value.IsString) return value.AsString();
            return string.Empty;
        }
    }
}
