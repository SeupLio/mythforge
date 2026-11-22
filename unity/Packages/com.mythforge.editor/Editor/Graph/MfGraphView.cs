using System;
using System.Collections.Generic;
using UnityEditor.Experimental.GraphView;
using UnityEngine;
using UnityEngine.UIElements;

namespace MythForge.Editor
{
    /// <summary>
    /// Generic content graph canvas: typed flow ports, linear-chain connection
    /// rules, contextual node creation and undo-friendly change notifications.
    /// The node catalog selects the quest/dialogue/skill type set.
    /// </summary>
    public sealed class MfGraphView : GraphView
    {
        private readonly Dictionary<string, MfNodeView> _nodeViews =
            new Dictionary<string, MfNodeView>(StringComparer.Ordinal);
        private readonly MfNodeCatalog _catalog;
        private bool _rebuilding;

        public event Action<GraphViewChange> GraphChanged;
        public event Action CreateRequested;

        public MfNodeCatalog Catalog
        {
            get { return _catalog; }
        }

        public MfGraphView(MfNodeCatalog catalog = null)
        {
            _catalog = catalog ?? MfQuestNodeCatalog.Catalog;
            style.flexGrow = 1;
            SetupZoom(ContentZoomer.DefaultMinScale, ContentZoomer.DefaultMaxScale);
            this.AddManipulator(new ContentDragger());
            this.AddManipulator(new SelectionDragger());
            this.AddManipulator(new RectangleSelector());

            var grid = new GridBackground();
            Insert(0, grid);
            grid.StretchToParentSize();

            graphViewChanged += change =>
            {
                if (!_rebuilding && GraphChanged != null) GraphChanged(change);
                return change;
            };
        }

        public override List<Port> GetCompatiblePorts(Port startPort, NodeAdapter nodeAdapter)
        {
            var compatible = new List<Port>();
            ports.ForEach(port =>
            {
                if (port == startPort) return;
                if (port.direction == startPort.direction) return;
                if (port.node == startPort.node) return;
                if (port.portType != startPort.portType) return;
                compatible.Add(port);
            });
            return compatible;
        }

        public override void BuildContextualMenu(ContextualMenuPopulateEvent evt)
        {
            base.BuildContextualMenu(evt);
            Vector2 graphPosition = contentViewContainer.WorldToLocal(evt.mousePosition);
            List<string> categories = _catalog.Categories();
            for (int c = 0; c < categories.Count; c++)
            {
                IReadOnlyList<MfNodeTypeSpec> types = _catalog.All;
                for (int i = 0; i < types.Count; i++)
                {
                    if (types[i].Category != categories[c]) continue;
                    MfNodeTypeSpec spec = types[i];
                    evt.menu.AppendAction("创建节点/" + spec.Category + "/" + spec.Display,
                        action => RequestCreate(spec, graphPosition));
                }
            }
        }

        private void RequestCreate(MfNodeTypeSpec spec, Vector2 position)
        {
            pendingCreateSpec = spec;
            pendingCreatePosition = position;
            if (CreateRequested != null) CreateRequested();
        }

        private MfNodeTypeSpec pendingCreateSpec;
        private Vector2 pendingCreatePosition;

        public MfNodeTypeSpec ConsumeCreateSpec(out Vector2 position)
        {
            MfNodeTypeSpec spec = pendingCreateSpec;
            position = pendingCreatePosition;
            pendingCreateSpec = null;
            return spec;
        }

        public void Rebuild(MfGraphDocument document)
        {
            _rebuilding = true;
            try
            {
                DeleteElements(graphElements.ToList());
                _nodeViews.Clear();
                if (document == null) return;

                for (int i = 0; i < document.Nodes.Count; i++)
                {
                    MfGraphNode node = document.Nodes[i];
                    MfNodeTypeSpec spec = _catalog.Find(node.Type);
                    if (spec == null) continue;
                    var view = new MfNodeView(node, spec);
                    AddElement(view);
                    _nodeViews[node.Id] = view;
                }

                for (int i = 0; i < document.Edges.Count; i++)
                {
                    MfGraphEdge edge = document.Edges[i];
                    MfNodeView from;
                    MfNodeView to;
                    if (!_nodeViews.TryGetValue(edge.From, out from)) continue;
                    if (!_nodeViews.TryGetValue(edge.To, out to)) continue;
                    if (from.OutputPort == null || to.InputPort == null) continue;
                    var edgeView = new Edge
                    {
                        output = from.OutputPort,
                        input = to.InputPort,
                        userData = new MfEdgeRef(edge.From, edge.To)
                    };
                    from.OutputPort.Connect(edgeView);
                    to.InputPort.Connect(edgeView);
                    AddElement(edgeView);
                }
            }
            finally
            {
                _rebuilding = false;
            }
        }

        public MfNodeView AddNodeView(MfGraphNode node)
        {
            MfNodeTypeSpec spec = _catalog.Find(node.Type);
            if (spec == null) return null;
            var view = new MfNodeView(node, spec);
            AddElement(view);
            _nodeViews[node.Id] = view;
            return view;
        }

        public bool TryGetNodeView(string nodeId, out MfNodeView view)
        {
            return _nodeViews.TryGetValue(nodeId, out view);
        }

        public IEnumerable<MfNodeView> NodeViews
        {
            get { return _nodeViews.Values; }
        }

        public void FocusNode(string nodeId)
        {
            MfNodeView view;
            if (!_nodeViews.TryGetValue(nodeId, out view)) return;
            ClearSelection();
            AddToSelection(view);
            FrameSelection();
        }

        public MfNodeView SelectedNodeView()
        {
            foreach (ISelectable selectable in selection)
            {
                var node = selectable as MfNodeView;
                if (node != null) return node;
            }
            return null;
        }
    }
}
