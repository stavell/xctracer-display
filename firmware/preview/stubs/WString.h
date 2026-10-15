#pragma once
#include <string>
class String {
 public:
  String(const char *s = "") : s_(s) {}
  const char *c_str() const { return s_.c_str(); }
  unsigned length() const { return (unsigned)s_.size(); }
  char charAt(unsigned i) const { return s_[i]; }
 private:
  std::string s_;
};
