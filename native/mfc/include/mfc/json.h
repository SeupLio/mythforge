#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace mfc::json {

// Minimal JSON value used for content graphs, schema files, manifests and
// test cases. Objects use std::map so serialization is byte-stable, which is a
// prerequisite for clean Git diffs and structural three-way merge.
class Value {
 public:
  enum class Kind { Null, Bool, Number, String, Array, Object };

  Value() = default;

  static Value null() { return Value(); }
  static Value boolean(bool b);
  static Value integer(int64_t i);
  static Value number(double d);
  static Value string(std::string s);
  static Value array();
  static Value object();

  Kind kind() const { return kind_; }
  bool isNull() const { return kind_ == Kind::Null; }
  bool isBool() const { return kind_ == Kind::Bool; }
  bool isNumber() const { return kind_ == Kind::Number; }
  bool isString() const { return kind_ == Kind::String; }
  bool isArray() const { return kind_ == Kind::Array; }
  bool isObject() const { return kind_ == Kind::Object; }

  bool asBool() const { return bool_; }
  int64_t asInt() const { return isInt_ ? int_ : static_cast<int64_t>(double_); }
  double asDouble() const { return isInt_ ? static_cast<double>(int_) : double_; }
  const std::string& asString() const { return string_; }
  const std::vector<Value>& items() const { return array_; }
  const std::map<std::string, Value>& members() const { return object_; }

  size_t size() const {
    if (kind_ == Kind::Array) return array_.size();
    if (kind_ == Kind::Object) return object_.size();
    return 0;
  }

  const Value* find(const std::string& key) const;
  Value& operator[](const std::string& key);  // inserts object member
  void push(Value v);                          // appends array element

  bool getString(const std::string& key, std::string* out) const;
  bool getInt(const std::string& key, int64_t* out) const;
  bool getDouble(const std::string& key, double* out) const;
  bool getBool(const std::string& key, bool* out) const;

 private:
  Kind kind_ = Kind::Null;
  bool bool_ = false;
  bool isInt_ = false;
  int64_t int_ = 0;
  double double_ = 0.0;
  std::string string_;
  std::vector<Value> array_;
  std::map<std::string, Value> object_;
};

bool parse(const std::string& text, Value& out, std::string* error);
std::string dump(const Value& value, int indent = 2);

}  // namespace mfc::json
