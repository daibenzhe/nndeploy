#ifndef _NNDEPLOY_EGRAPH_SUBST_H_
#define _NNDEPLOY_EGRAPH_SUBST_H_

#include <cstdint>
#include <ostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "nndeploy/egraph/egraph.h"

namespace nndeploy {
namespace egraph {

class Var {
 public:
  enum class Kind { kSymbol, kNumeric };

  Var() : kind_(Kind::kSymbol), symbol_("?"), number_(0) {}

  static Var fromU32(std::uint32_t value) {
    Var var;
    var.kind_ = Kind::kNumeric;
    var.number_ = value;
    var.symbol_.clear();
    return var;
  }

  static Var fromString(const std::string &value) {
    if (value.size() < 2 || value[0] != '?') {
      throw std::invalid_argument("variable must start with '?'");
    }
    if (value[1] == '#') {
      if (value.size() == 2) {
        throw std::invalid_argument("numeric variable is malformed");
      }
      std::size_t parsed = 0;
      try {
        parsed = std::stoul(value.substr(2));
      } catch (...) {
        throw std::invalid_argument("numeric variable is malformed");
      }
      return fromU32(static_cast<std::uint32_t>(parsed));
    }
    Var var;
    var.kind_ = Kind::kSymbol;
    var.symbol_ = value;
    return var;
  }

  Kind kind() const { return kind_; }

  bool isNumeric() const { return kind_ == Kind::kNumeric; }

  const std::string &symbol() const {
    if (kind_ != Kind::kSymbol) {
      throw std::logic_error("variable is not symbolic");
    }
    return symbol_;
  }

  std::uint32_t number() const {
    if (kind_ != Kind::kNumeric) {
      throw std::logic_error("variable is not numeric");
    }
    return number_;
  }

  std::string toString() const {
    if (kind_ == Kind::kNumeric) {
      return "?#" + std::to_string(number_);
    }
    return symbol_;
  }

  bool operator==(const Var &other) const {
    return kind_ == other.kind_ && symbol_ == other.symbol_ &&
           number_ == other.number_;
  }

  bool operator!=(const Var &other) const { return !(*this == other); }

  bool operator<(const Var &other) const {
    if (kind_ != other.kind_) {
      return kind_ < other.kind_;
    }
    return kind_ == Kind::kNumeric ? number_ < other.number_
                                   : symbol_ < other.symbol_;
  }

 private:
  Kind kind_;
  std::string symbol_;
  std::uint32_t number_;
};

struct VarHash {
  std::size_t operator()(const Var &var) const {
    return std::hash<std::string>{}(var.toString());
  }
};

inline std::ostream &operator<<(std::ostream &os, const Var &var) {
  os << var.toString();
  return os;
}

class Subst {
 public:
  Subst() = default;

  explicit Subst(std::size_t capacity) { vec_.reserve(capacity); }

  std::size_t size() const { return vec_.size(); }

  bool empty() const { return vec_.empty(); }

  const Id *get(const Var &var) const {
    for (const auto &pair : vec_) {
      if (pair.first == var) {
        return &pair.second;
      }
    }
    return nullptr;
  }

  Id *get(const Var &var) {
    for (auto &pair : vec_) {
      if (pair.first == var) {
        return &pair.second;
      }
    }
    return nullptr;
  }

  const Id &at(const Var &var) const {
    const Id *id = get(var);
    if (id == nullptr) {
      throw std::out_of_range("variable not found in substitution");
    }
    return *id;
  }

  Id insert(const Var &var, Id id) {
    Id *existing = get(var);
    if (existing != nullptr) {
      Id old = *existing;
      *existing = id;
      return old;
    }
    vec_.push_back({var, id});
    return Id();
  }

  const std::vector<std::pair<Var, Id>> &pairs() const { return vec_; }

 private:
  std::vector<std::pair<Var, Id>> vec_;
};

}  // namespace egraph
}  // namespace nndeploy

#endif
