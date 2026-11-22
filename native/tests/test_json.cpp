#include "mf_test.h"

#include "mfc/json.h"

using mfc::json::Value;

MF_TEST(json_parses_nested_documents) {
  const std::string text = R"({
    "schemaVersion": 1,
    "kind": "quest",
    "id": "q_demo",
    "nodes": [{"id": "n1", "type": "quest.start", "props": {}}],
    "flag": true,
    "ratio": 0.5,
    "none": null
  })";
  Value root;
  std::string error;
  MF_CHECK(mfc::json::parse(text, root, &error));
  MF_CHECK(error.empty());
  MF_CHECK(root.isObject());
  MF_CHECK_EQ(root.size(), static_cast<size_t>(7));
  std::string id;
  MF_CHECK(root.getString("id", &id));
  MF_CHECK_STR_EQ(id, "q_demo");
  const Value* nodes = root.find("nodes");
  MF_CHECK(nodes != nullptr);
  MF_CHECK(nodes->isArray());
  MF_CHECK_EQ(nodes->size(), static_cast<size_t>(1));
  bool flag = false;
  MF_CHECK(root.getBool("flag", &flag));
  MF_CHECK(flag);
  const Value* none = root.find("none");
  MF_CHECK(none != nullptr && none->isNull());
}

MF_TEST(json_roundtrip_is_byte_stable) {
  Value root = Value::object();
  root["b"] = Value::integer(2);
  root["a"] = Value::string("hello");
  Value array = Value::array();
  array.push(Value::integer(1));
  array.push(Value::number(2.5));
  array.push(Value::boolean(false));
  root["list"] = std::move(array);

  const std::string first = mfc::json::dump(root);
  Value reparsed;
  std::string error;
  MF_CHECK(mfc::json::parse(first, reparsed, &error));
  const std::string second = mfc::json::dump(reparsed);
  MF_CHECK_STR_EQ(first, second);
  // std::map ordering keeps keys sorted, which keeps Git diffs clean.
  MF_CHECK(first.find("\"a\"") < first.find("\"b\""));
}

MF_TEST(json_handles_escapes_and_unicode) {
  Value root;
  std::string error;
  MF_CHECK(mfc::json::parse("{\"text\": \"\\u72fc \\n \\\"x\\\"\"}", root, &error));
  std::string text;
  MF_CHECK(root.getString("text", &text));
  MF_CHECK_STR_EQ(text, "\xE7\x8B\xBC \n \"x\"");
}

MF_TEST(json_reports_errors_with_position) {
  Value root;
  std::string error;
  MF_CHECK(!mfc::json::parse("{\"a\": 1,,}", root, &error));
  MF_CHECK(error.find("line") != std::string::npos);
}
