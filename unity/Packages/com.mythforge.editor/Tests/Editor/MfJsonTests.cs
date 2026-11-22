using NUnit.Framework;

namespace MythForge.Editor.Tests
{
    public class MfJsonTests
    {
        [Test]
        public void ParsesNestedObjectsAndArrays()
        {
            const string text = "{\"a\":1,\"b\":[true,\"x\",null],\"c\":{\"d\":2.5}}";
            MfJsonValue value;
            string error;
            Assert.IsTrue(MfJsonValue.Parse(text, out value, out error), error);
            Assert.AreEqual(MfJsonKind.Object, value.Kind);
            Assert.AreEqual(1, value.GetInt("a"));
            MfJsonValue b = value["b"];
            Assert.AreEqual(3, b.Items.Count);
            Assert.IsTrue(b.Items[0].AsBool());
            Assert.AreEqual("x", b.Items[1].AsString());
            Assert.IsTrue(b.Items[2].IsNull);
            Assert.AreEqual(2.5, value["c"].GetDouble("d"), 1e-9);
        }

        [Test]
        public void RoundTripIsByteStable()
        {
            MfJsonValue root = MfJsonValue.Object();
            root["b"] = MfJsonValue.Number(2);
            root["a"] = MfJsonValue.String("hello");
            MfJsonValue array = MfJsonValue.Array();
            array.Add(MfJsonValue.Number(1.5));
            array.Add(MfJsonValue.Bool(false));
            root["list"] = array;

            string first = root.ToJson();
            MfJsonValue reparsed;
            string error;
            Assert.IsTrue(MfJsonValue.Parse(first, out reparsed, out error), error);
            Assert.AreEqual(first, reparsed.ToJson());
        }

        [Test]
        public void KeysAreSortedForCleanDiffs()
        {
            MfJsonValue root = MfJsonValue.Object();
            root["z"] = MfJsonValue.Number(1);
            root["a"] = MfJsonValue.Number(2);
            root["m"] = MfJsonValue.Number(3);
            string json = root.ToJson();
            Assert.Less(json.IndexOf("\"a\"", System.StringComparison.Ordinal),
                json.IndexOf("\"m\"", System.StringComparison.Ordinal));
            Assert.Less(json.IndexOf("\"m\"", System.StringComparison.Ordinal),
                json.IndexOf("\"z\"", System.StringComparison.Ordinal));
        }

        [Test]
        public void HandlesEscapesAndUnicodeEscapes()
        {
            MfJsonValue value;
            string error;
            Assert.IsTrue(MfJsonValue.Parse("{\"t\":\"\\u72fc \\n \\\"q\\\"\"}", out value, out error), error);
            Assert.AreEqual("狼 \n \"q\"", value.GetString("t"));
        }

        [Test]
        public void ReportsParseErrorsWithPosition()
        {
            MfJsonValue value;
            string error;
            Assert.IsFalse(MfJsonValue.Parse("{\"a\":1,}", out value, out error));
            StringAssert.Contains("line", error);
        }

        [Test]
        public void DeepEqualsComparesContent()
        {
            MfJsonValue a;
            MfJsonValue b;
            string error;
            Assert.IsTrue(MfJsonValue.Parse("{\"x\":[1,2,{\"y\":\"z\"}]}", out a, out error), error);
            Assert.IsTrue(MfJsonValue.Parse("{\"x\":[1,2,{\"y\":\"z\"}]}", out b, out error), error);
            Assert.IsTrue(a.DeepEquals(b));
            b["x"].Items[2]["y"] = MfJsonValue.String("other");
            Assert.IsFalse(a.DeepEquals(b));
        }

        [Test]
        public void CloneIsIndependent()
        {
            MfJsonValue original = MfJsonValue.Object();
            original["list"] = MfJsonValue.Array();
            original["list"].Add(MfJsonValue.String("a"));
            MfJsonValue copy = original.Clone();
            copy["list"].Add(MfJsonValue.String("b"));
            Assert.AreEqual(1, original["list"].Items.Count);
            Assert.AreEqual(2, copy["list"].Items.Count);
        }

        [Test]
        public void DistinguishesIntegersAndFloats()
        {
            MfJsonValue value;
            string error;
            Assert.IsTrue(MfJsonValue.Parse("{\"i\":3,\"f\":3.5}", out value, out error), error);
            Assert.AreEqual(3, value.GetInt("i"));
            Assert.AreEqual(3.5, value.GetDouble("f"), 1e-9);
            StringAssert.Contains("3", value.ToJson());
        }
    }
}
