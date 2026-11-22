using NUnit.Framework;

namespace MythForge.Editor.Tests
{
    public class UndoStackTests
    {
        [Test]
        public void UndoRestoresPreviousSnapshot()
        {
            var stack = new MfGraphUndoStack();
            MfGraphDocument document = MfGraphDocument.CreateNew("q_one");
            stack.Record(document);
            document.Id = "q_two";
            MfGraphDocument restored = stack.Undo(document);
            Assert.IsNotNull(restored);
            Assert.AreEqual("q_one", restored.Id);
            Assert.IsTrue(stack.CanRedo);
        }

        [Test]
        public void RedoRestoresNewerSnapshot()
        {
            var stack = new MfGraphUndoStack();
            MfGraphDocument document = MfGraphDocument.CreateNew("q_one");
            stack.Record(document);
            document.Id = "q_two";
            MfGraphDocument restored = stack.Undo(document);
            MfGraphDocument redone = stack.Redo(restored);
            Assert.IsNotNull(redone);
            Assert.AreEqual("q_two", redone.Id);
        }

        [Test]
        public void NewRecordClearsRedo()
        {
            var stack = new MfGraphUndoStack();
            MfGraphDocument document = MfGraphDocument.CreateNew("q_one");
            stack.Record(document);
            document.Id = "q_two";
            stack.Undo(document);
            Assert.IsTrue(stack.CanRedo);
            stack.Record(document);
            Assert.IsFalse(stack.CanRedo);
        }

        [Test]
        public void CapacityDropsOldestSnapshots()
        {
            var stack = new MfGraphUndoStack(3);
            var document = new MfGraphDocument();
            for (int i = 0; i < 5; i++)
            {
                document.Id = "q_" + i;
                stack.Record(document);
            }
            document.Id = "q_final";
            Assert.AreEqual(3, stack.UndoCount);
            Assert.AreEqual("q_4", stack.Undo(document).Id);
            Assert.AreEqual("q_3", stack.Undo(document).Id);
            Assert.AreEqual("q_2", stack.Undo(document).Id);
            Assert.IsFalse(stack.CanUndo);
        }

        [Test]
        public void FreshStackHasNoHistory()
        {
            var stack = new MfGraphUndoStack();
            Assert.IsFalse(stack.CanUndo);
            Assert.IsFalse(stack.CanRedo);
            Assert.IsNull(stack.Undo(MfGraphDocument.CreateNew("q")));
            Assert.IsNull(stack.Redo(MfGraphDocument.CreateNew("q")));
        }

        [Test]
        public void ClearRemovesBothStacks()
        {
            var stack = new MfGraphUndoStack();
            MfGraphDocument document = MfGraphDocument.CreateNew("q");
            stack.Record(document);
            document.Id = "q2";
            stack.Undo(document);
            stack.Clear();
            Assert.IsFalse(stack.CanUndo);
            Assert.IsFalse(stack.CanRedo);
        }
    }
}
