using System.Collections.Generic;
using UnityEditor.Experimental.GraphView;
using UnityEngine;
using UnityEngine.UIElements;

namespace MythForge.Editor
{
    /// <summary>Marker type for control-flow ports (one edge per port in v1).</summary>
    public sealed class MfFlow
    {
    }

    public sealed class MfEdgeRef
    {
        public string From;
        public string To;

        public MfEdgeRef(string from, string to)
        {
            From = from;
            To = to;
        }
    }

    public sealed class MfNodeView : Node
    {
        private readonly Label _summary;
        private readonly Label _badge;
        private readonly string _baseTitle;
        private bool _highlighted;

        public MfGraphNode Data { get; private set; }
        public MfNodeTypeSpec Spec { get; private set; }
        public Port InputPort { get; private set; }
        public Port OutputPort { get; private set; }

        public MfNodeView(MfGraphNode data, MfNodeTypeSpec spec)
        {
            Data = data;
            Spec = spec;
            _baseTitle = spec.Display;
            title = spec.Display;
            viewDataKey = data.Id;
            tooltip = spec.Description + "\n节点 ID: " + data.Id;
            capabilities |= Capabilities.Selectable | Capabilities.Movable | Capabilities.Deletable;
            style.width = 210;

            titleContainer.style.backgroundColor = spec.Accent;

            if (spec.MaxInputs > 0)
            {
                InputPort = Port.Create<Edge>(Orientation.Horizontal, Direction.Input,
                    Port.Capacity.Single, typeof(MfFlow));
                InputPort.portName = "in";
                InputPort.tooltip = "上游";
                inputContainer.Add(InputPort);
            }
            if (spec.MaxOutputs > 0)
            {
                OutputPort = Port.Create<Edge>(Orientation.Horizontal, Direction.Output,
                    Port.Capacity.Single, typeof(MfFlow));
                OutputPort.portName = "out";
                OutputPort.tooltip = "下游";
                outputContainer.Add(OutputPort);
            }

            _summary = new Label();
            _summary.style.paddingLeft = 6;
            _summary.style.paddingRight = 6;
            _summary.style.paddingTop = 4;
            _summary.style.paddingBottom = 6;
            _summary.style.fontSize = 11;
            _summary.style.color = new Color(0.78f, 0.80f, 0.84f);
            _summary.style.unityTextAlign = TextAnchor.MiddleLeft;
            extensionContainer.Add(_summary);

            _badge = new Label("!");
            _badge.style.backgroundColor = new Color(0.78f, 0.22f, 0.22f);
            _badge.style.color = Color.white;
            _badge.style.paddingLeft = 5;
            _badge.style.paddingRight = 5;
            _badge.style.paddingTop = 0;
            _badge.style.paddingBottom = 0;
            _badge.style.marginLeft = 4;
            _badge.style.display = DisplayStyle.None;
            titleButtonContainer.Add(_badge);

            RefreshSummary();
            RefreshExpandedState();
            SetPosition(new Rect(data.X, data.Y, 210, 110));
        }

        public void RefreshSummary()
        {
            _summary.text = Spec.Summarize(Data);
            tooltip = Spec.Description + "\n节点 ID: " + Data.Id;
        }

        public void SetDiagnostics(IReadOnlyList<MfDiagnostic> diagnostics)
        {
            int errors = 0;
            string firstMessage = string.Empty;
            for (int i = 0; i < diagnostics.Count; i++)
            {
                if (diagnostics[i].Severity == MfSeverity.Error) errors++;
                if (firstMessage.Length == 0) firstMessage = diagnostics[i].Message;
            }
            if (diagnostics.Count == 0)
            {
                _badge.style.display = DisplayStyle.None;
                title = _baseTitle;
                return;
            }
            _badge.style.display = DisplayStyle.Flex;
            _badge.text = errors > 0 ? "E" : "W";
            _badge.style.backgroundColor = errors > 0
                ? new Color(0.78f, 0.22f, 0.22f)
                : new Color(0.78f, 0.60f, 0.15f);
            title = _baseTitle + "  ⚠";
            tooltip = firstMessage + (diagnostics.Count > 1
                ? "\n(还有 " + (diagnostics.Count - 1) + " 条诊断)"
                : string.Empty) + "\n节点 ID: " + Data.Id;
        }

        public void ApplyPositionFromView()
        {
            Vector2 position = GetPosition().position;
            Data.X = position.x;
            Data.Y = position.y;
        }

        /// <summary>Runtime-preview highlight; never modifies the document.</summary>
        public void SetHighlighted(bool highlighted)
        {
            if (_highlighted == highlighted) return;
            _highlighted = highlighted;
            style.borderLeftWidth = highlighted ? 3f : 0f;
            style.borderTopWidth = highlighted ? 3f : 0f;
            style.borderRightWidth = highlighted ? 3f : 0f;
            style.borderBottomWidth = highlighted ? 3f : 0f;
            Color color = new Color(0.98f, 0.78f, 0.22f);
            style.borderLeftColor = color;
            style.borderTopColor = color;
            style.borderRightColor = color;
            style.borderBottomColor = color;
        }

        public bool Highlighted
        {
            get { return _highlighted; }
        }
    }
}
