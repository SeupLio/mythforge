using System;
using System.Collections.Generic;
using System.IO;
using System.Text;
using MythForge.Runtime;
using UnityEditor;
using UnityEngine;
using UnityEngine.UIElements;

namespace MythForge.Editor
{
    /// <summary>
    /// M6 live preview: loads a content JSON, ensures its .mfb artifact is
    /// fresh, runs it through the native SDK and highlights the node that just
    /// emitted. Timers are driven by the editor clock; events are injected from
    /// the side panel. Every trace line can be mirrored to JSONL telemetry (M10).
    /// </summary>
    public sealed class PlayWindow : EditorWindow
    {
        private const string LastPathKey = "MythForge.LastPlay";

        private MfGraphDocument _document;
        private string _path;
        private string _artifact;
        private MfPlaySession _session;
        private MfTelemetryWriter _telemetry;
        private MfGraphView _graphView;

        private VisualElement _graphHost;
        private VisualElement _quickRow;
        private Label _status;
        private Label _counters;
        private Label _telemetryPath;
        private Label _log;
        private ScrollView _logScroll;
        private TextField _eventName;
        private TextField _eventFields;
        private Button _playButton;
        private Toggle _followToggle;
        private Toggle _telemetryToggle;
        private bool _playing;
        private double _lastTick;
        private string _highlightedId;

        [MenuItem("MythForge/实时预览 %#p")]
        public static void Open()
        {
            var window = GetWindow<PlayWindow>();
            window.titleContent = new GUIContent("MythForge Play");
            window.minSize = new Vector2(1000, 600);
            window.Show();
        }

        [MenuItem("MythForge/预览示例任务")]
        public static void OpenDemo()
        {
            Open();
            PlayWindow window = GetWindow<PlayWindow>();
            window.LoadDefault();
        }

        private void OnEnable()
        {
            EditorApplication.update += OnEditorUpdate;
            titleContent = new GUIContent("MythForge Play");
        }

        private void OnDisable()
        {
            EditorApplication.update -= OnEditorUpdate;
            StopSession();
        }

        private void CreateGUI()
        {
            VisualElement root = rootVisualElement;
            root.style.flexDirection = FlexDirection.Column;

            var toolbar = new VisualElement();
            toolbar.style.flexDirection = FlexDirection.Row;
            toolbar.style.paddingLeft = 6;
            toolbar.style.paddingRight = 6;
            toolbar.style.paddingTop = 4;
            toolbar.style.paddingBottom = 4;
            toolbar.style.borderBottomWidth = 1;
            toolbar.style.borderBottomColor = new Color(0.15f, 0.15f, 0.15f);
            toolbar.Add(MakeButton("打开内容", OpenContent));
            toolbar.Add(MakeButton("重新编译", BuildArtifact));
            toolbar.Add(MakeSeparator());

            _playButton = MakeButton("播放", TogglePlay);
            toolbar.Add(_playButton);
            toolbar.Add(MakeButton("步进 100ms", () => Step(100000)));
            toolbar.Add(MakeButton("步进 1s", () => Step(1000000)));
            toolbar.Add(MakeButton("重置", Reload));
            toolbar.Add(MakeSeparator());
            toolbar.Add(MakeButton("在图编辑器中定位", OpenInEditor));
            _followToggle = new Toggle("跟随节点") { value = true };
            _followToggle.style.marginLeft = 8;
            _followToggle.style.unityTextAlign = TextAnchor.MiddleLeft;
            toolbar.Add(_followToggle);
            root.Add(toolbar);

            var body = new VisualElement();
            body.style.flexDirection = FlexDirection.Row;
            body.style.flexGrow = 1;
            body.style.minHeight = 300;

            _graphHost = new VisualElement();
            _graphHost.style.flexGrow = 1;
            body.Add(_graphHost);

            var side = new VisualElement();
            side.style.width = 380;
            side.style.borderLeftWidth = 1;
            side.style.borderLeftColor = new Color(0.15f, 0.15f, 0.15f);
            side.style.paddingLeft = 6;
            side.style.paddingRight = 6;
            side.style.flexDirection = FlexDirection.Column;

            var runtimeTitle = new Label("运行时");
            runtimeTitle.style.unityFontStyleAndWeight = FontStyle.Bold;
            side.Add(runtimeTitle);
            _counters = new Label("尚未加载");
            _counters.style.fontSize = 11;
            _counters.style.whiteSpace = WhiteSpace.Normal;
            side.Add(_counters);

            var eventTitle = new Label("事件注入");
            eventTitle.style.unityFontStyleAndWeight = FontStyle.Bold;
            eventTitle.style.marginTop = 6;
            side.Add(eventTitle);
            _quickRow = new VisualElement();
            _quickRow.style.flexDirection = FlexDirection.Row;
            _quickRow.style.flexWrap = Wrap.Wrap;
            side.Add(_quickRow);

            var eventRow = new VisualElement();
            eventRow.style.flexDirection = FlexDirection.Row;
            _eventName = new TextField();
            _eventName.style.width = 120;
            _eventName.value = "Kill";
            eventRow.Add(_eventName);
            _eventFields = new TextField();
            _eventFields.style.flexGrow = 1;
            _eventFields.value = "target=Wolf";
            eventRow.Add(_eventFields);
            eventRow.Add(MakeButton("发送", SendCustomEvent));
            side.Add(eventRow);

            var logTitle = new Label("执行日志");
            logTitle.style.unityFontStyleAndWeight = FontStyle.Bold;
            logTitle.style.marginTop = 6;
            side.Add(logTitle);
            _logScroll = new ScrollView();
            _logScroll.style.flexGrow = 1;
            _logScroll.style.minHeight = 180;
            _log = new Label("等待加载…");
            _log.style.whiteSpace = WhiteSpace.Normal;
            _log.style.fontSize = 11;
            _logScroll.Add(_log);
            side.Add(_logScroll);

            var telemetryRow = new VisualElement();
            telemetryRow.style.flexDirection = FlexDirection.Row;
            _telemetryToggle = new Toggle("记录遥测") { value = true };
            _telemetryToggle.style.marginTop = 4;
            telemetryRow.Add(_telemetryToggle);
            side.Add(telemetryRow);
            _telemetryPath = new Label(string.Empty);
            _telemetryPath.style.fontSize = 10;
            _telemetryPath.style.whiteSpace = WhiteSpace.Normal;
            side.Add(_telemetryPath);

            body.Add(side);
            root.Add(body);

            _status = new Label("就绪");
            _status.style.paddingLeft = 6;
            _status.style.paddingTop = 2;
            _status.style.paddingBottom = 2;
            _status.style.fontSize = 11;
            _status.style.color = new Color(0.7f, 0.7f, 0.7f);
            root.Add(_status);

            LoadDefault();
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

        private void LoadDefault()
        {
            string last = EditorPrefs.GetString(LastPathKey, string.Empty);
            if (!string.IsNullOrEmpty(last) && File.Exists(last))
            {
                LoadContent(last);
                return;
            }
            string demo = Path.Combine(Application.dataPath, "MythForgeContent", "quests",
                "q_forest_wolves.json");
            if (File.Exists(demo))
            {
                LoadContent(demo);
            }
            else if (_status != null)
            {
                _status.text = "请选择一个内容 JSON（任务/剧情/技能）";
            }
        }

        private void OpenContent()
        {
            string folder = Path.Combine(Application.dataPath, "MythForgeContent");
            string selected = EditorUtility.OpenFilePanel("打开内容", folder, "json");
            if (!string.IsNullOrEmpty(selected)) LoadContent(selected);
        }

        private void LoadContent(string path)
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
                SetStatus("无法解析内容: " + error);
                return;
            }
            _document = document;
            _path = path;
            EditorPrefs.SetString(LastPathKey, path);
            if (!EnsureArtifact()) return;
            RebuildGraph();
            RebuildQuickButtons();
            if (!StartSession()) return;
            SetStatus("已加载 " + document.Kind + " " + document.Id);
        }

        private bool EnsureArtifact()
        {
            string contentRoot = MfCli.FindContentRoot(_path);
            if (string.IsNullOrEmpty(contentRoot))
            {
                SetStatus("无法定位内容根目录");
                return false;
            }
            _artifact = Path.Combine(contentRoot, "out", _document.Id + ".mfb");
            bool stale = !File.Exists(_artifact) ||
                         File.GetLastWriteTimeUtc(_artifact) < File.GetLastWriteTimeUtc(_path);
            if (!stale) return true;
            MfProcessResult result = MfCli.Build(contentRoot);
            Debug.Log("[MythForge] mf build\n" + result.StdOut + result.StdErr);
            if (!File.Exists(_artifact))
            {
                SetStatus("编译失败，未生成 " + _artifact + "\n" + result.StdOut + result.StdErr);
                return false;
            }
            return true;
        }

        private void BuildArtifact()
        {
            if (_document == null)
            {
                SetStatus("尚未加载内容");
                return;
            }
            string contentRoot = MfCli.FindContentRoot(_path);
            if (string.IsNullOrEmpty(contentRoot)) return;
            MfProcessResult result = MfCli.Build(contentRoot);
            Debug.Log("[MythForge] mf build\n" + result.StdOut + result.StdErr);
            SetStatus(result.ExitCode == 0 ? "构建完成" : "构建失败，见 Console");
            if (result.ExitCode == 0 && _document != null)
            {
                _artifact = Path.Combine(contentRoot, "out", _document.Id + ".mfb");
                StartSession();
            }
        }

        private void RebuildGraph()
        {
            if (_graphHost == null || _document == null) return;
            _graphHost.Clear();
            _graphView = new MfGraphView(_document.Catalog);
            _graphView.Rebuild(_document);
            _graphHost.Add(_graphView);
            _highlightedId = null;
        }

        private void RebuildQuickButtons()
        {
            if (_quickRow == null) return;
            _quickRow.Clear();
            string kind = _document != null ? _document.Kind : "quest";
            if (kind == "quest")
            {
                AddQuick("击杀狼", "Kill", "target=Wolf");
                AddQuick("与长老对话", "Talk", "npc=Elder");
                AddQuick("到达山顶", "Reach", "area=Hilltop");
                AddQuick("收集羊毛", "Collect", "item=Wool");
                if (_eventName != null) _eventName.value = "Kill";
                if (_eventFields != null) _eventFields.value = "target=Wolf";
            }
            else if (kind == "dialogue")
            {
                AddQuick("下一步", "DialogueAdvance", string.Empty);
                AddQuick("选择选项", "DialogueChoiceSelected", string.Empty);
                string asset = FindTimelineAsset();
                AddQuick("Timeline 完成", "TimelineFinished", "asset=" + Quote(asset));
                if (_eventName != null) _eventName.value = "DialogueAdvance";
                if (_eventFields != null) _eventFields.value = string.Empty;
            }
            else
            {
                var hint = new Label("技能由虚拟时钟驱动，用“播放/步进”推进。");
                hint.style.fontSize = 11;
                hint.style.whiteSpace = WhiteSpace.Normal;
                _quickRow.Add(hint);
                if (_eventName != null) _eventName.value = "SkillCast";
                if (_eventFields != null) _eventFields.value = string.Empty;
            }
        }

        private void AddQuick(string label, string eventName, string fields)
        {
            var button = new Button(() => SendEvent(eventName, fields)) { text = label };
            button.style.marginRight = 3;
            button.style.marginBottom = 3;
            _quickRow.Add(button);
        }

        private string FindTimelineAsset()
        {
            if (_document == null) return string.Empty;
            for (int i = 0; i < _document.Nodes.Count; i++)
            {
                MfGraphNode node = _document.Nodes[i];
                if (node.Type != "dialogue.timeline") continue;
                MfJsonValue value;
                if (node.Props.TryGetValue("asset", out value) && value.IsString) return value.AsString();
            }
            return string.Empty;
        }

        private bool StartSession()
        {
            StopSession();
            if (_document == null || string.IsNullOrEmpty(_artifact) || !File.Exists(_artifact))
            {
                SetStatus("找不到编译产物: " + (_artifact ?? "<none>"));
                return false;
            }
            _session = new MfPlaySession();
            if (_telemetryToggle != null && _telemetryToggle.value)
            {
                _telemetry = new MfTelemetryWriter(BuildTelemetryPath());
                _session.Telemetry = _telemetry;
                if (_telemetryPath != null) _telemetryPath.text = "遥测: " + _telemetry.Path;
            }
            else if (_telemetryPath != null)
            {
                _telemetryPath.text = "遥测已关闭";
            }
            string error;
            if (!_session.Load(_artifact, out error))
            {
                SetStatus("运行时加载失败: " + error);
                StopSession();
                return false;
            }
            if (_log != null) _log.text = string.Empty;
            RefreshRuntimeUi();
            return true;
        }

        private void StopSession()
        {
            _playing = false;
            if (_playButton != null) _playButton.text = "播放";
            if (_session != null)
            {
                _session.Dispose();
                _session = null;
            }
            if (_telemetry != null)
            {
                _telemetry.Dispose();
                _telemetry = null;
            }
            ClearHighlight();
        }

        private string BuildTelemetryPath()
        {
            string projectRoot = Path.GetDirectoryName(Application.dataPath);
            string name = (_document != null ? _document.Id : "session") + "-" +
                          DateTime.Now.ToString("yyyyMMdd-HHmmss") + ".jsonl";
            return Path.Combine(projectRoot ?? string.Empty, "Logs", "MythForge", name);
        }

        private void TogglePlay()
        {
            if (_session == null) return;
            _playing = !_playing;
            _lastTick = EditorApplication.timeSinceStartup;
            if (_playButton != null) _playButton.text = _playing ? "暂停" : "播放";
            SetStatus(_playing ? "运行中（虚拟时钟）" : "已暂停");
        }

        private void Step(long micros)
        {
            if (_session == null) return;
            _session.Advance(micros);
            RefreshRuntimeUi();
            if (!_session.Running && _playButton != null)
            {
                _playing = false;
                _playButton.text = "播放";
            }
        }

        private void Reload()
        {
            if (!string.IsNullOrEmpty(_path) && File.Exists(_path))
            {
                LoadContent(_path);
            }
        }

        private void SendCustomEvent()
        {
            SendEvent(_eventName.value, _eventFields.value);
        }

        private void SendEvent(string name, string fieldsText)
        {
            if (_session == null) return;
            string fieldError;
            Dictionary<string, object> fields = ParseFields(fieldsText, out fieldError);
            if (fields == null)
            {
                SetStatus("字段格式错误: " + fieldError);
                return;
            }
            bool ok = _session.Emit(name, fields);
            RefreshRuntimeUi();
            SetStatus(ok ? "已发送 " + name : "发送失败，见运行日志");
        }

        private void OnEditorUpdate()
        {
            if (!_playing || _session == null) return;
            double now = EditorApplication.timeSinceStartup;
            long delta = (long)((now - _lastTick) * 1000000.0);
            _lastTick = now;
            if (delta <= 0) return;
            _session.Advance(Math.Min(delta, 100000));
            RefreshRuntimeUi();
            if (!_session.Running)
            {
                _playing = false;
                if (_playButton != null) _playButton.text = "播放";
                SetStatus("运行结束");
            }
        }

        private void RefreshRuntimeUi()
        {
            if (_session == null) return;
            if (_counters != null)
            {
                _counters.text = string.Format(
                    "emit {0}   call {1}   wait {2}   fault {3}\n虚拟时间 {4}ms  运行中: {5}",
                    _session.EmitCount, _session.CallCount, _session.WaitCount, _session.FaultCount,
                    _session.NowMicros / 1000, _session.Running ? "是" : "否");
            }
            if (_log != null)
            {
                _log.text = _session.RecentText(150);
                if (_logScroll != null) _logScroll.ScrollTo(_log);
            }
            if (!string.IsNullOrEmpty(_session.LastError))
            {
                SetStatus("VM 错误: " + _session.LastError);
            }
            Highlight(_session.LastNodeId);
        }

        private void Highlight(string nodeId)
        {
            if (_graphView == null || nodeId == _highlightedId) return;
            MfNodeView previous;
            if (!string.IsNullOrEmpty(_highlightedId) &&
                _graphView.TryGetNodeView(_highlightedId, out previous))
            {
                previous.SetHighlighted(false);
            }
            _highlightedId = nodeId;
            MfNodeView view;
            if (string.IsNullOrEmpty(nodeId) || !_graphView.TryGetNodeView(nodeId, out view)) return;
            view.SetHighlighted(true);
            if (_followToggle != null && _followToggle.value)
            {
                _graphView.FocusNode(nodeId);
            }
        }

        private void ClearHighlight()
        {
            MfNodeView view;
            if (_graphView != null && !string.IsNullOrEmpty(_highlightedId) &&
                _graphView.TryGetNodeView(_highlightedId, out view))
            {
                view.SetHighlighted(false);
            }
            _highlightedId = null;
        }

        private void OpenInEditor()
        {
            if (_document == null || string.IsNullOrEmpty(_path)) return;
            if (_document.Kind == "dialogue")
            {
                DialogueGraphWindow window = DialogueGraphWindow.OpenFile(_path);
                if (_session != null) window.FocusNode(_session.LastNodeId);
            }
            else if (_document.Kind == "skill")
            {
                SkillTimelineWindow.OpenFile(_path);
            }
            else
            {
                QuestGraphWindow window = QuestGraphWindow.OpenFile(_path);
                if (_session != null) window.FocusNode(_session.LastNodeId);
            }
        }

        private void SetStatus(string message)
        {
            if (_status != null) _status.text = message;
        }

        // ----- field parsing -------------------------------------------------

        private static Dictionary<string, object> ParseFields(string text, out string error)
        {
            error = null;
            var fields = new Dictionary<string, object>(StringComparer.Ordinal);
            if (string.IsNullOrEmpty(text)) return fields;
            List<string> tokens = Tokenize(text);
            for (int i = 0; i < tokens.Count; i++)
            {
                string token = tokens[i];
                if (token.Length == 0) continue;
                int equals = token.IndexOf('=');
                if (equals <= 0)
                {
                    error = "字段必须是 key=value: " + token;
                    return null;
                }
                string key = token.Substring(0, equals);
                string raw = token.Substring(equals + 1);
                fields[key] = ConvertValue(raw);
            }
            return fields;
        }

        private static List<string> Tokenize(string line)
        {
            var tokens = new List<string>();
            var builder = new StringBuilder();
            bool inQuotes = false;
            bool any = false;
            for (int i = 0; i < line.Length; i++)
            {
                char c = line[i];
                if (c == '"')
                {
                    inQuotes = !inQuotes;
                    any = true;
                    continue;
                }
                if (!inQuotes && (c == ' ' || c == '\t'))
                {
                    if (any)
                    {
                        tokens.Add(builder.ToString());
                        builder.Length = 0;
                        any = false;
                    }
                    continue;
                }
                builder.Append(c);
                any = true;
            }
            if (any) tokens.Add(builder.ToString());
            return tokens;
        }

        private static object ConvertValue(string raw)
        {
            long integer;
            if (long.TryParse(raw, out integer)) return integer;
            double real;
            if (double.TryParse(raw, System.Globalization.NumberStyles.Float,
                    System.Globalization.CultureInfo.InvariantCulture, out real))
            {
                return real;
            }
            if (string.Equals(raw, "true", StringComparison.OrdinalIgnoreCase)) return true;
            if (string.Equals(raw, "false", StringComparison.OrdinalIgnoreCase)) return false;
            return raw;
        }

        private static string Quote(string value)
        {
            return value == null ? "\"\"" : "\"" + value + "\"";
        }
    }
}
