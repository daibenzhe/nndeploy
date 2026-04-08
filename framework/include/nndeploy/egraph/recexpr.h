#ifndef _NNDEPLOY_EGRAPH_RECEXPR_H_
#define _NNDEPLOY_EGRAPH_RECEXPR_H_

#include <cstddef>
#include <initializer_list>
#include <stdexcept>
#include <utility>
#include <vector>

#include "nndeploy/egraph/egraph.h"

namespace nndeploy {
namespace egraph {

template <typename Op>
class RecExpr {
 public:
  using Node = ENode<Op>;

  RecExpr() = default;

  explicit RecExpr(std::vector<Node> nodes) : nodes_(std::move(nodes)) {
    validate();
  }

  Id add(const Node &node) {
    validateNode(node, nodes_.size());
    nodes_.push_back(node);
    return Id(nodes_.size() - 1);
  }

  const Node &operator[](Id id) const {
    if (!id.valid() || id.value >= nodes_.size()) {
      throw std::out_of_range("recexpr id out of range");
    }
    return nodes_[id.value];
  }

  const Node &at(std::size_t index) const {
    if (index >= nodes_.size()) {
      throw std::out_of_range("recexpr index out of range");
    }
    return nodes_[index];
  }

  const Node &root() const {
    if (nodes_.empty()) {
      throw std::out_of_range("recexpr is empty");
    }
    return nodes_.back();
  }

  Id rootId() const {
    if (nodes_.empty()) {
      throw std::out_of_range("recexpr is empty");
    }
    return Id(nodes_.size() - 1);
  }

  std::size_t size() const { return nodes_.size(); }

  bool empty() const { return nodes_.empty(); }

  const std::vector<Node> &nodes() const { return nodes_; }

 private:
  static void validateNode(const Node &node, std::size_t upper_bound) {
    for (const auto &child : node.children) {
      if (!child.valid() || child.value >= upper_bound) {
        throw std::out_of_range("recexpr child id out of range");
      }
    }
  }

  void validate() const {
    for (std::size_t i = 0; i < nodes_.size(); ++i) {
      validateNode(nodes_[i], i);
    }
  }

  std::vector<Node> nodes_;
};

template <typename Op>
class RecExprBuilder {
 public:
  using Node = ENode<Op>;

  RecExprBuilder() = default;

  Id addLeaf(const Op &op) { return expr_.add(Node{op, {}}); }

  Id addNode(const Op &op, std::initializer_list<Id> children) {
    return addNode(op, std::vector<Id>(children));
  }

  Id addNode(const Op &op, std::vector<Id> children) {
    return expr_.add(Node{op, std::move(children)});
  }

  const RecExpr<Op> &expr() const & { return expr_; }

  RecExpr<Op> build() && { return std::move(expr_); }

  std::size_t size() const { return expr_.size(); }

  bool empty() const { return expr_.empty(); }

 private:
  RecExpr<Op> expr_;
};

}  // namespace egraph
}  // namespace nndeploy

#endif
