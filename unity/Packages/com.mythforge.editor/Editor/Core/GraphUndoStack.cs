using System;
using System.Collections.Generic;

namespace MythForge.Editor
{
    /// <summary>
    /// Snapshot-based undo/redo. Each entry is the JSON of the document before an
    /// edit. Snapshots are cheap enough for 2000-node graphs and make every edit
    /// path (move, connect, delete, property change) use one code path.
    /// </summary>
    public sealed class MfGraphUndoStack
    {
        private readonly List<string> _undo = new List<string>();
        private readonly List<string> _redo = new List<string>();
        private readonly int _capacity;

        public MfGraphUndoStack(int capacity = 100)
        {
            _capacity = Math.Max(2, capacity);
        }

        public bool CanUndo { get { return _undo.Count > 0; } }
        public bool CanRedo { get { return _redo.Count > 0; } }
        public int UndoCount { get { return _undo.Count; } }
        public int RedoCount { get { return _redo.Count; } }

        public void Clear()
        {
            _undo.Clear();
            _redo.Clear();
        }

        public void Record(MfGraphDocument document)
        {
            if (document == null) return;
            RecordJson(document.ToJson());
        }

        public void RecordJson(string snapshot)
        {
            if (string.IsNullOrEmpty(snapshot)) return;
            _undo.Add(snapshot);
            if (_undo.Count > _capacity) _undo.RemoveAt(0);
            _redo.Clear();
        }

        public MfGraphDocument Undo(MfGraphDocument current)
        {
            if (!CanUndo || current == null) return null;
            string snapshot = _undo[_undo.Count - 1];
            _undo.RemoveAt(_undo.Count - 1);
            _redo.Add(current.ToJson());
            if (_redo.Count > _capacity) _redo.RemoveAt(0);
            string error;
            return MfGraphDocument.FromJson(snapshot, out error);
        }

        public MfGraphDocument Redo(MfGraphDocument current)
        {
            if (!CanRedo || current == null) return null;
            string snapshot = _redo[_redo.Count - 1];
            _redo.RemoveAt(_redo.Count - 1);
            _undo.Add(current.ToJson());
            if (_undo.Count > _capacity) _undo.RemoveAt(0);
            string error;
            return MfGraphDocument.FromJson(snapshot, out error);
        }
    }
}
