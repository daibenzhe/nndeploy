#include "nndeploy/egraph/egraph.h"

#include <gtest/gtest.h>

#include <set>
#include <string>
#include <unordered_set>

#include "nndeploy/egraph/explain.h"
#include "nndeploy/egraph/extract.h"
#include "nndeploy/egraph/multipattern.h"
#include "nndeploy/egraph/pattern.h"
#include "nndeploy/egraph/recexpr.h"
#include "nndeploy/egraph/rewrite.h"
#include "nndeploy/egraph/runner.h"
#include "nndeploy/egraph/searcher.h"
#include "nndeploy/egraph/subst.h"

namespace {

using nndeploy::egraph::Applier;
using nndeploy::egraph::AstDepth;
using nndeploy::egraph::BackoffScheduler;
using nndeploy::egraph::EGraph;
using nndeploy::egraph::ENode;
using nndeploy::egraph::ENodeOrVar;
using nndeploy::egraph::explainEquivalence;
using nndeploy::egraph::explainIdEquivalence;
using nndeploy::egraph::Extractor;
using nndeploy::egraph::FlatTerm;
using nndeploy::egraph::Id;
using nndeploy::egraph::makePatternNode;
using nndeploy::egraph::makePatternVar;
using nndeploy::egraph::MultiPattern;
using nndeploy::egraph::MultiPatternSearcher;
using nndeploy::egraph::NoAnalysis;
using nndeploy::egraph::Pattern;
using nndeploy::egraph::PatternApplier;
using nndeploy::egraph::PatternAst;
using nndeploy::egraph::PatternBuilder;
using nndeploy::egraph::PatternSearcher;
using nndeploy::egraph::RecExpr;
using nndeploy::egraph::RecExprBuilder;
using nndeploy::egraph::Report;
using nndeploy::egraph::Rewrite;
using nndeploy::egraph::Runner;
using nndeploy::egraph::Searcher;
using nndeploy::egraph::SearchMatches;
using nndeploy::egraph::SimpleScheduler;
using nndeploy::egraph::StopReason;
using nndeploy::egraph::StopReasonKind;
using nndeploy::egraph::Subst;
using nndeploy::egraph::Var;

struct Symbol {
  std::string value;

  bool operator==(const Symbol &other) const { return value == other.value; }
};

struct SymbolHash {
  std::size_t operator()(const Symbol &symbol) const {
    return std::hash<std::string>{}(symbol.value);
  }
};

struct NodeCountAnalysis {
  struct Data {
    std::size_t count = 0;
  };

  Data make(const ENode<Symbol> &) const { return Data{1}; }

  void merge(Data &dst, const Data &src) const { dst.count += src.count; }
};

ENode<Symbol> makeNode(const std::string &op,
                       std::initializer_list<Id> children = {}) {
  return ENode<Symbol>{Symbol{op}, std::vector<Id>(children)};
}

using PatNode = ENode<ENodeOrVar<Symbol>>;

PatNode makePatENode(const std::string &op,
                     std::initializer_list<Id> children = {}) {
  PatNode pn;
  pn.op = ENodeOrVar<Symbol>::fromENode(makeNode(op));
  pn.children = std::vector<Id>(children);
  return pn;
}

PatNode makePatVar(const std::string &var_name) {
  PatNode pn;
  pn.op = ENodeOrVar<Symbol>::fromVar(Var::fromString(var_name));
  return pn;
}

struct BinaryGraph {
  EGraph<Symbol> graph;
  Id left;
  Id right;
  Id root;
};

BinaryGraph makeBinaryGraph(const std::string &left, const std::string &right,
                            const std::string &op) {
  BinaryGraph setup;
  setup.left = setup.graph.add(makeNode(left));
  setup.right = setup.graph.add(makeNode(right));
  setup.root = setup.graph.add(makeNode(op, {setup.left, setup.right}));
  return setup;
}

BinaryGraph makePlusGraph(const std::string &left, const std::string &right) {
  return makeBinaryGraph(left, right, "+");
}

RecExpr<Symbol> makeBinaryExpr(const std::string &left,
                               const std::string &right,
                               const std::string &op) {
  RecExpr<Symbol> expr;
  Id l = expr.add(makeNode(left));
  Id r = expr.add(makeNode(right));
  expr.add(makeNode(op, {l, r}));
  return expr;
}

PatternAst<Symbol> makeLeafPatternAst(const std::string &var_name) {
  PatternAst<Symbol> ast;
  ast.add(makePatVar(var_name));
  return ast;
}

PatternAst<Symbol> makeBinaryPatternAst(const std::string &op,
                                        const std::string &left_var,
                                        const std::string &right_var) {
  PatternAst<Symbol> ast;
  Id left = ast.add(makePatVar(left_var));
  Id right = ast.add(makePatVar(right_var));
  ast.add(makePatENode(op, {left, right}));
  return ast;
}

PatternAst<Symbol> makeRepeatedVarPatternAst(const std::string &op,
                                             const std::string &var_name) {
  PatternAst<Symbol> ast;
  Id left = ast.add(makePatVar(var_name));
  Id right = ast.add(makePatVar(var_name));
  ast.add(makePatENode(op, {left, right}));
  return ast;
}

void expectPatternAstEquivalent(const PatternAst<Symbol> &lhs,
                                const PatternAst<Symbol> &rhs) {
  ASSERT_EQ(lhs.size(), rhs.size());
  for (std::size_t i = 0; i < lhs.size(); ++i) {
    EXPECT_EQ(lhs.at(i).op.isVar(), rhs.at(i).op.isVar());
    if (lhs.at(i).op.isENode() && rhs.at(i).op.isENode()) {
      EXPECT_EQ(lhs.at(i).op.enode().op, rhs.at(i).op.enode().op);
    }
    EXPECT_EQ(lhs.at(i).children, rhs.at(i).children);
  }
}

}  // namespace

namespace std {
template <>
struct hash<Symbol> {
  std::size_t operator()(const Symbol &symbol) const {
    return SymbolHash{}(symbol);
  }
};
}  // namespace std

TEST(EGraphTest, AddDeduplicatesEquivalentNodes) {
  EGraph<Symbol> graph;

  Id a0 = graph.add(makeNode("a"));
  Id a1 = graph.add(makeNode("a"));

  EXPECT_EQ(graph.find(a0), graph.find(a1));
  EXPECT_EQ(graph.classCount(), 1U);
  EXPECT_EQ(graph.memoSize(), 1U);
}

TEST(EGraphTest, MergeAndRebuildDeduplicateCongruentParents) {
  EGraph<Symbol> graph;

  Id x = graph.add(makeNode("x"));
  Id y = graph.add(makeNode("y"));
  Id fx = graph.add(makeNode("f", {x}));
  Id fy = graph.add(makeNode("f", {y}));

  EXPECT_NE(graph.find(fx), graph.find(fy));

  graph.merge(x, y);
  graph.rebuild();

  EXPECT_EQ(graph.find(x), graph.find(y));
  EXPECT_EQ(graph.find(fx), graph.find(fy));
  EXPECT_EQ(graph.classCount(), 2U);
}

TEST(EGraphTest, RebuildCanonicalizesNestedParents) {
  EGraph<Symbol> graph;

  Id a = graph.add(makeNode("a"));
  Id b = graph.add(makeNode("b"));
  Id fa = graph.add(makeNode("f", {a}));
  Id fb = graph.add(makeNode("f", {b}));
  Id gfa = graph.add(makeNode("g", {fa}));
  Id gfb = graph.add(makeNode("g", {fb}));

  graph.merge(a, b);
  graph.rebuild();

  EXPECT_EQ(graph.find(fa), graph.find(fb));
  EXPECT_EQ(graph.find(gfa), graph.find(gfb));
}

TEST(EGraphTest, AnalysisDataMergesAcrossClasses) {
  EGraph<Symbol, NodeCountAnalysis> graph;

  Id a = graph.add(makeNode("a"));
  Id b = graph.add(makeNode("b"));
  graph.merge(a, b);
  graph.rebuild();

  const auto *klass = graph.getEClass(a);
  ASSERT_NE(klass, nullptr);
  EXPECT_EQ(klass->data.count, 2U);
}

TEST(EGraphTest, ParentsAreTrackedAfterRebuild) {
  EGraph<Symbol> graph;

  Id a = graph.add(makeNode("a"));
  Id b = graph.add(makeNode("b"));
  Id fa = graph.add(makeNode("f", {a}));
  Id gb = graph.add(makeNode("g", {b}));

  graph.merge(a, b);
  graph.rebuild();

  const auto *klass = graph.getEClass(a);
  ASSERT_NE(klass, nullptr);

  std::unordered_set<std::size_t> parent_ids;
  for (auto parent : klass->parents) {
    parent_ids.insert(graph.find(parent).value);
  }

  EXPECT_EQ(parent_ids.count(graph.find(fa).value), 1U);
  EXPECT_EQ(parent_ids.count(graph.find(gb).value), 1U);
}

TEST(EGraphTest, MergeIsIdempotent) {
  EGraph<Symbol> graph;

  Id a = graph.add(makeNode("a"));
  Id b = graph.add(makeNode("b"));
  Id first = graph.merge(a, b);
  Id second = graph.merge(a, b);

  EXPECT_EQ(graph.find(first), graph.find(second));
  graph.rebuild();
  EXPECT_EQ(graph.classCount(), 1U);
}

TEST(EGraphTest, RebuildIsIdempotent) {
  EGraph<Symbol> graph;

  Id a = graph.add(makeNode("a"));
  Id b = graph.add(makeNode("b"));
  Id fa = graph.add(makeNode("f", {a}));
  Id fb = graph.add(makeNode("f", {b}));

  graph.merge(a, b);
  graph.rebuild();

  const Id merged_once = graph.find(fa);
  const std::size_t class_count = graph.classCount();
  const std::size_t memo_size = graph.memoSize();

  graph.rebuild();

  EXPECT_EQ(graph.find(fa), merged_once);
  EXPECT_EQ(graph.find(fa), graph.find(fb));
  EXPECT_EQ(graph.classCount(), class_count);
  EXPECT_EQ(graph.memoSize(), memo_size);
}

TEST(EGraphTest, AddAfterMergeBeforeRebuildCanonicalizesChildren) {
  EGraph<Symbol> graph;

  Id a = graph.add(makeNode("a"));
  Id b = graph.add(makeNode("b"));
  graph.merge(a, b);

  Id fa = graph.add(makeNode("f", {a}));
  Id fb = graph.add(makeNode("f", {b}));

  EXPECT_EQ(graph.find(fa), graph.find(fb));
}

TEST(EGraphTest, InvalidIdsThrow) {
  EGraph<Symbol> graph;
  Id a = graph.add(makeNode("a"));
  (void)a;

  const Id invalid_default;
  const Id invalid_foreign(123456);

  EXPECT_THROW(graph.find(invalid_default), std::out_of_range);
  EXPECT_THROW(graph.find(invalid_foreign), std::out_of_range);
  EXPECT_THROW(graph.merge(invalid_default, Id(0)), std::out_of_range);
  EXPECT_THROW(graph.merge(Id(0), invalid_foreign), std::out_of_range);
  EXPECT_THROW(graph.add(makeNode("f", {invalid_default})), std::out_of_range);
  EXPECT_THROW(graph.add(makeNode("f", {invalid_foreign})), std::out_of_range);
}

TEST(EGraphTest, RecExprStoresTopologicallyOrderedNodes) {
  RecExpr<Symbol> expr;

  Id a = expr.add(makeNode("a"));
  Id b = expr.add(makeNode("b"));
  Id plus = expr.add(makeNode("+", {a, b}));

  EXPECT_EQ(expr.size(), 3U);
  EXPECT_EQ(expr.rootId(), plus);
  EXPECT_EQ(expr.root().op.value, "+");
  EXPECT_EQ(expr[plus].children[0], a);
  EXPECT_EQ(expr[plus].children[1], b);
}

TEST(EGraphTest, RecExprRejectsForwardReferences) {
  RecExpr<Symbol> expr;
  expr.add(makeNode("a"));

  EXPECT_THROW(expr.add(makeNode("f", {Id(5)})), std::out_of_range);
  EXPECT_THROW(RecExpr<Symbol>(std::vector<ENode<Symbol>>{
                   makeNode("f", {Id(1)}), makeNode("a")}),
               std::out_of_range);
}

TEST(RecExprErgonomicsTest, BuildsTopologicalTreeWithExplicitChildren) {
  RecExprBuilder<Symbol> builder;

  Id a = builder.addLeaf(Symbol{"a"});
  Id b = builder.addLeaf(Symbol{"b"});
  Id plus = builder.addNode(Symbol{"+"}, {a, b});
  Id root = builder.addNode(Symbol{"f"}, {plus});

  const auto &expr = builder.expr();
  EXPECT_EQ(expr.size(), 4U);
  EXPECT_EQ(expr.rootId(), root);
  EXPECT_EQ(expr.root().op.value, "f");
  EXPECT_EQ(expr[root].children[0], plus);
  EXPECT_EQ(expr[plus].children[0], a);
  EXPECT_EQ(expr[plus].children[1], b);
}

TEST(RecExprErgonomicsTest, RejectsForwardReferences) {
  RecExprBuilder<Symbol> builder;
  builder.addLeaf(Symbol{"a"});

  EXPECT_THROW(builder.addNode(Symbol{"f"}, {Id(5)}), std::out_of_range);
  EXPECT_THROW(builder.addNode(Symbol{"g"}, {Id(1)}), std::out_of_range);
}

TEST(RecExprErgonomicsTest, RoundTripMatchesManualRecExpr) {
  RecExprBuilder<Symbol> builder;

  Id a = builder.addLeaf(Symbol{"a"});
  Id b = builder.addLeaf(Symbol{"b"});
  builder.addNode(Symbol{"+"}, {a, b});

  RecExpr<Symbol> built = std::move(builder).build();
  RecExpr<Symbol> manual = makeBinaryExpr("a", "b", "+");

  EGraph<Symbol> graph;
  Id built_root = graph.addExpr(built);
  Id manual_root = graph.addExpr(manual);

  EXPECT_EQ(graph.find(built_root), graph.find(manual_root));
  EXPECT_EQ(graph.classCount(), 3U);
}

TEST(EGraphTest, VarParsesSymbolicAndNumericForms) {
  Var symbol = Var::fromString("?x");
  Var numeric = Var::fromString("?#12");

  EXPECT_EQ(symbol.toString(), "?x");
  EXPECT_EQ(numeric.toString(), "?#12");
  EXPECT_FALSE(symbol.isNumeric());
  EXPECT_TRUE(numeric.isNumeric());
  EXPECT_EQ(numeric.number(), 12U);

  EXPECT_THROW(Var::fromString("x"), std::invalid_argument);
  EXPECT_THROW(Var::fromString("?"), std::invalid_argument);
  EXPECT_THROW(Var::fromString("?#"), std::invalid_argument);
}

TEST(EGraphTest, SubstInsertAndLookupWork) {
  Subst subst;
  Var x = Var::fromString("?x");
  Var y = Var::fromU32(7);

  Id old = subst.insert(x, Id(3));
  EXPECT_FALSE(old.valid());
  ASSERT_NE(subst.get(x), nullptr);
  EXPECT_EQ(*subst.get(x), Id(3));

  old = subst.insert(x, Id(5));
  EXPECT_EQ(old, Id(3));
  EXPECT_EQ(subst.at(x), Id(5));

  subst.insert(y, Id(9));
  EXPECT_EQ(subst.size(), 2U);
  EXPECT_EQ(subst.at(y), Id(9));
  EXPECT_THROW(subst.at(Var::fromString("?missing")), std::out_of_range);
}

// ===================================================================
// Pattern tests
// ===================================================================

TEST(PatternTest, VariableOnlyPattern) {
  Pattern<Symbol> pat(makeLeafPatternAst("?x"));
  EXPECT_EQ(pat.vars().size(), 1U);
  EXPECT_EQ(pat.vars()[0], Var::fromString("?x"));
}

TEST(PatternTest, RepeatedVarsDeduplicatedInVarsList) {
  // Pattern: (+ ?a ?a)
  Pattern<Symbol> pat(makeRepeatedVarPatternAst("+", "?a"));
  // ?a appears twice in the AST but should be listed once in vars().
  EXPECT_EQ(pat.vars().size(), 1U);
  EXPECT_EQ(pat.vars()[0], Var::fromString("?a"));
}

TEST(PatternTest, NestedOperatorPattern) {
  // Pattern: (f (g ?x))
  PatternAst<Symbol> ast;
  Id x = ast.add(makePatVar("?x"));
  Id g = ast.add(makePatENode("g", {x}));
  ast.add(makePatENode("f", {g}));

  Pattern<Symbol> pat(std::move(ast));
  EXPECT_EQ(pat.vars().size(), 1U);
  EXPECT_EQ(pat.ast().size(), 3U);
}

TEST(PatternTest, VariableWithChildrenRejected) {
  // A Var node that has children is malformed.
  PatternAst<Symbol> ast;
  Id leaf = ast.add(makePatVar("?x"));

  // Manually build a Var node with children — should be rejected.
  PatNode bad;
  bad.op = ENodeOrVar<Symbol>::fromVar(Var::fromString("?y"));
  bad.children = {leaf};

  EXPECT_THROW(ast.add(bad);
               Pattern<Symbol> pat(std::move(ast)), std::invalid_argument);
}

TEST(PatternTest, ConversionFromRecExpr) {
  // Build a plain RecExpr: (+ a b)
  RecExpr<Symbol> expr;
  Id a = expr.add(makeNode("a"));
  Id b = expr.add(makeNode("b"));
  expr.add(makeNode("+", {a, b}));

  Pattern<Symbol> pat = Pattern<Symbol>::fromExpr(expr);
  // No variables in a concrete expression.
  EXPECT_TRUE(pat.vars().empty());
  EXPECT_EQ(pat.ast().size(), 3U);
}

// ===================================================================
// Pattern ergonomics tests
// ===================================================================

TEST(PatternErgonomicsTest, BuilderConstructsNestedPattern) {
  EGraph<Symbol> graph;
  Id a = graph.add(makeNode("a"));
  Id ga = graph.add(makeNode("g", {a}));
  graph.add(makeNode("f", {ga}));

  PatternBuilder<Symbol> builder;
  Id x = builder.append(makePatternVar<Symbol>("?x"));
  Id g = builder.append(makePatternNode<Symbol>(Symbol{"g"}, {x}));
  builder.append(makePatternNode<Symbol>(Symbol{"f"}, {g}));

  Pattern<Symbol> pat = std::move(builder).buildPattern();
  auto matches = pat.search(graph);

  ASSERT_EQ(matches.size(), 1U);
  ASSERT_EQ(matches[0].substs.size(), 1U);
  ASSERT_NE(matches[0].substs[0].get(Var::fromString("?x")), nullptr);
  EXPECT_EQ(graph.find(*matches[0].substs[0].get(Var::fromString("?x"))),
            graph.find(a));
}

TEST(PatternErgonomicsTest, BuilderPreservesRepeatedVariableSemantics) {
  EGraph<Symbol> graph;
  Id a = graph.add(makeNode("a"));
  Id b = graph.add(makeNode("b"));
  graph.add(makeNode("+", {a, a}));
  graph.add(makeNode("+", {a, b}));

  PatternBuilder<Symbol> builder;
  Id left = builder.appendVar("?x");
  Id right = builder.appendVar("?x");
  builder.appendNode(Symbol{"+"}, {left, right});

  Pattern<Symbol> pat = std::move(builder).buildPattern();
  auto matches = pat.search(graph);

  std::size_t total_substs = 0;
  for (const auto &m : matches) {
    total_substs += m.substs.size();
  }
  EXPECT_EQ(total_substs, 1U);
}

TEST(PatternErgonomicsTest, BuilderRejectsVariableWithChildren) {
  PatternBuilder<Symbol> builder;
  Id leaf = builder.appendVar("?x");
  EXPECT_THROW(builder.append(makePatternVar<Symbol>("?y"), {leaf}),
               std::invalid_argument);
}

TEST(PatternErgonomicsTest, BuilderRoundTripsToManualPattern) {
  PatternAst<Symbol> manual_ast;
  Id mx = manual_ast.add(makePatVar("?x"));
  Id mg = manual_ast.add(makePatENode("g", {mx}));
  manual_ast.add(makePatENode("f", {mg}));
  Pattern<Symbol> manual(std::move(manual_ast));

  PatternBuilder<Symbol> builder;
  Id bx = builder.appendVar("?x");
  Id bg = builder.appendNode(Symbol{"g"}, {bx});
  builder.appendNode(Symbol{"f"}, {bg});
  Pattern<Symbol> ergonomic = std::move(builder).buildPattern();

  EXPECT_EQ(ergonomic.vars(), manual.vars());
  expectPatternAstEquivalent(ergonomic.ast(), manual.ast());

  EGraph<Symbol> graph;
  Id a = graph.add(makeNode("a"));
  Id ga = graph.add(makeNode("g", {a}));
  graph.add(makeNode("f", {ga}));

  auto manual_matches = manual.search(graph);
  auto ergonomic_matches = ergonomic.search(graph);
  ASSERT_EQ(manual_matches.size(), ergonomic_matches.size());
  ASSERT_EQ(manual_matches.size(), 1U);
  ASSERT_EQ(manual_matches[0].substs.size(),
            ergonomic_matches[0].substs.size());
  ASSERT_EQ(manual_matches[0].substs.size(), 1U);
  EXPECT_EQ(graph.find(*manual_matches[0].substs[0].get(Var::fromString("?x"))),
            graph.find(a));
  EXPECT_EQ(
      graph.find(*ergonomic_matches[0].substs[0].get(Var::fromString("?x"))),
      graph.find(a));
}

// ===================================================================
// Matcher tests
// ===================================================================

TEST(MatcherTest, SearchInOneEclass) {
  // Graph: a, b, (+ a b)
  // Pattern: ?x (variable-only) — should match every e-class.
  auto setup = makePlusGraph("a", "b");
  Pattern<Symbol> pat(makeLeafPatternAst("?x"));

  auto matches = pat.search(setup.graph);
  // 3 classes, each should have exactly 1 substitution.
  EXPECT_EQ(matches.size(), 3U);
  for (const auto &m : matches) {
    EXPECT_EQ(m.substs.size(), 1U);
  }
}

TEST(MatcherTest, WholeGraphSearchFindsOperator) {
  // Graph: a, b, (+ a b)
  // Pattern: (+ ?x ?y)
  auto setup = makePlusGraph("a", "b");
  Pattern<Symbol> pat(makeBinaryPatternAst("+", "?x", "?y"));

  auto matches = pat.search(setup.graph);
  // Only the (+ a b) class matches.
  EXPECT_EQ(matches.size(), 1U);
  EXPECT_EQ(matches[0].substs.size(), 1U);
  EXPECT_EQ(setup.graph.find(*matches[0].substs[0].get(Var::fromString("?x"))),
            setup.graph.find(setup.left));
  EXPECT_EQ(setup.graph.find(*matches[0].substs[0].get(Var::fromString("?y"))),
            setup.graph.find(setup.right));
}

TEST(MatcherTest, RepeatedVarEqualityConstraint) {
  // Graph: a, (+ a a), (+ a b)
  // Pattern: (+ ?a ?a) — only matches when both children are the same class.
  EGraph<Symbol> graph;
  Id a = graph.add(makeNode("a"));
  Id b = graph.add(makeNode("b"));
  graph.add(makeNode("+", {a, a}));
  graph.add(makeNode("+", {a, b}));

  Pattern<Symbol> pat(makeRepeatedVarPatternAst("+", "?a"));

  auto matches = pat.search(graph);
  // Should find a match in the class of (+ a a), but not (+ a b).
  std::size_t total_substs = 0;
  for (const auto &m : matches) {
    total_substs += m.substs.size();
  }
  EXPECT_EQ(total_substs, 1U);
}

TEST(MatcherTest, NoMatchCase) {
  // Graph: a, b, (+ a b)
  // Pattern: (f ?x) — f is not in the graph.
  EGraph<Symbol> graph;
  Id a = graph.add(makeNode("a"));
  Id b = graph.add(makeNode("b"));
  graph.add(makeNode("+", {a, b}));

  PatternAst<Symbol> past;
  Id px = past.add(makePatVar("?x"));
  past.add(makePatENode("f", {px}));
  Pattern<Symbol> pat(std::move(past));

  auto matches = pat.search(graph);
  // No class contains an "f" node.
  std::size_t total_substs = 0;
  for (const auto &m : matches) {
    total_substs += m.substs.size();
  }
  EXPECT_EQ(total_substs, 0U);
}

TEST(MatcherTest, SearchAfterMerge) {
  // After merging a and b, pattern (+ ?x ?x) should match (+ a b)
  // because a and b are now in the same class.
  EGraph<Symbol> graph;
  Id a = graph.add(makeNode("a"));
  Id b = graph.add(makeNode("b"));
  graph.add(makeNode("+", {a, b}));

  graph.merge(a, b);
  graph.rebuild();

  Pattern<Symbol> pat(makeRepeatedVarPatternAst("+", "?x"));

  auto matches = pat.search(graph);
  std::size_t total_substs = 0;
  for (const auto &m : matches) {
    total_substs += m.substs.size();
  }
  EXPECT_GE(total_substs, 1U);
}

// ===================================================================
// Rewrite tests
// ===================================================================

TEST(RewriteTest, RhsUnboundVariableRejected) {
  // LHS: (+ ?a ?b), RHS: ?c — ?c is not in LHS.
  PatternAst<Symbol> lhs_ast = makeBinaryPatternAst("+", "?a", "?b");

  PatternAst<Symbol> rhs_ast;
  rhs_ast.add(makePatVar("?c"));

  EXPECT_THROW((Rewrite<Symbol>("bad", Pattern<Symbol>(std::move(lhs_ast)),
                                Pattern<Symbol>(std::move(rhs_ast)))),
               std::invalid_argument);
}

TEST(RewriteTest, AdditiveIdentityRewrite) {
  // Rule: (+ ?a 0) => ?a
  // Graph: a, 0, (+ a 0)

  EGraph<Symbol> graph;
  Id a = graph.add(makeNode("a"));
  Id zero = graph.add(makeNode("0"));
  Id plus = graph.add(makeNode("+", {a, zero}));

  // Before rewrite: (+ a 0) and a are in different classes.
  EXPECT_NE(graph.find(plus), graph.find(a));

  // Build LHS: (+ ?a 0)
  PatternAst<Symbol> lhs_ast;
  Id la = lhs_ast.add(makePatVar("?a"));
  Id l0 = lhs_ast.add(makePatENode("0"));
  lhs_ast.add(makePatENode("+", {la, l0}));

  // Build RHS: ?a
  PatternAst<Symbol> rhs_ast;
  rhs_ast.add(makePatVar("?a"));

  Rewrite<Symbol> rule("plus-zero", Pattern<Symbol>(std::move(lhs_ast)),
                       Pattern<Symbol>(std::move(rhs_ast)));

  std::size_t merges = rule.run(graph);
  EXPECT_GE(merges, 1U);

  graph.rebuild();

  // After rewrite: (+ a 0) and a are in the same class.
  EXPECT_EQ(graph.find(plus), graph.find(a));
}

TEST(RewriteTest, CommutativityRewrite) {
  // Rule: (+ ?a ?b) => (+ ?b ?a)
  // Graph: a, b, (+ a b)

  EGraph<Symbol> graph;
  Id a = graph.add(makeNode("a"));
  Id b = graph.add(makeNode("b"));
  Id plus_ab = graph.add(makeNode("+", {a, b}));

  // (+ b a) is not in the graph yet.
  Id plus_ba_before = graph.add(makeNode("+", {b, a}));
  EXPECT_NE(graph.find(plus_ab), graph.find(plus_ba_before));

  // Build LHS: (+ ?a ?b)
  PatternAst<Symbol> lhs_ast = makeBinaryPatternAst("+", "?a", "?b");

  // Build RHS: (+ ?b ?a)
  PatternAst<Symbol> rhs_ast = makeBinaryPatternAst("+", "?b", "?a");

  Rewrite<Symbol> rule("plus-comm", Pattern<Symbol>(std::move(lhs_ast)),
                       Pattern<Symbol>(std::move(rhs_ast)));

  rule.run(graph);
  graph.rebuild();

  // After rewrite: (+ a b) and (+ b a) are in the same class.
  EXPECT_EQ(graph.find(plus_ab), graph.find(plus_ba_before));
}

TEST(RewriteTest, IdempotentRepeatedApplication) {
  // Applying the same rewrite twice (with rebuild in between) should be
  // idempotent — the second application should produce no new merges.

  EGraph<Symbol> graph;
  Id a = graph.add(makeNode("a"));
  Id zero = graph.add(makeNode("0"));
  graph.add(makeNode("+", {a, zero}));

  // Build LHS: (+ ?a 0)
  auto makeLhs = []() {
    PatternAst<Symbol> ast;
    Id la = ast.add(makePatVar("?a"));
    Id l0 = ast.add(makePatENode("0"));
    ast.add(makePatENode("+", {la, l0}));
    return Pattern<Symbol>(std::move(ast));
  };
  auto makeRhs = []() {
    PatternAst<Symbol> ast;
    ast.add(makePatVar("?a"));
    return Pattern<Symbol>(std::move(ast));
  };

  Rewrite<Symbol> rule("plus-zero", makeLhs(), makeRhs());

  rule.run(graph);
  graph.rebuild();

  std::size_t classes_after_first = graph.classCount();
  std::size_t memo_after_first = graph.memoSize();

  std::size_t merges2 = rule.run(graph);
  graph.rebuild();

  EXPECT_EQ(merges2, 0U);
  EXPECT_EQ(graph.classCount(), classes_after_first);
  EXPECT_EQ(graph.memoSize(), memo_after_first);
}

TEST(RewriteTest, AddExprFromRecExpr) {
  // Test the new addExpr(const RecExpr<Op>&) overload.
  EGraph<Symbol> graph;

  RecExpr<Symbol> expr;
  Id ea = expr.add(makeNode("a"));
  Id eb = expr.add(makeNode("b"));
  expr.add(makeNode("+", {ea, eb}));

  Id root = graph.addExpr(expr);
  EXPECT_TRUE(graph.contains(root));

  // Adding the same expression again should return the same class.
  Id root2 = graph.addExpr(expr);
  EXPECT_EQ(graph.find(root), graph.find(root2));
  EXPECT_EQ(graph.classCount(), 3U);  // a, b, (+ a b)
}

// ===================================================================
// Extractor tests
// ===================================================================

TEST(ExtractorTest, SimpleLeafExtraction) {
  // Graph with a single leaf node "a".
  // Extracting should return cost=1, expr=["a"].
  EGraph<Symbol> graph;
  Id a = graph.add(makeNode("a"));

  Extractor<Symbol> extractor(graph);
  auto [cost, expr] = extractor.findBest(a);

  EXPECT_EQ(cost, 1U);
  EXPECT_EQ(expr.size(), 1U);
  EXPECT_EQ(expr.root().op.value, "a");
  EXPECT_TRUE(expr.root().children.empty());
}

TEST(ExtractorTest, ExtractSmallerTermFromEquivalentClass) {
  // Add "a" and "(f a)", then merge them.
  // The extractor should pick "a" (cost 1) over "(f a)" (cost 2).
  EGraph<Symbol> graph;
  Id a = graph.add(makeNode("a"));
  Id fa = graph.add(makeNode("f", {a}));

  graph.merge(a, fa);
  graph.rebuild();

  Extractor<Symbol> extractor(graph);
  auto [cost, expr] = extractor.findBest(a);

  EXPECT_EQ(cost, 1U);
  EXPECT_EQ(expr.size(), 1U);
  EXPECT_EQ(expr.root().op.value, "a");
}

TEST(ExtractorTest, ExtractAfterRewrite) {
  // Build (+ a 0), apply rule (+ ?a 0) => ?a, then extract.
  // Should get "a" (cost 1) instead of "(+ a 0)" (cost 3).
  EGraph<Symbol> graph;
  Id a = graph.add(makeNode("a"));
  Id zero = graph.add(makeNode("0"));
  Id plus = graph.add(makeNode("+", {a, zero}));

  // Build and apply rewrite: (+ ?a 0) => ?a
  PatternAst<Symbol> lhs_ast;
  Id la = lhs_ast.add(makePatVar("?a"));
  Id l0 = lhs_ast.add(makePatENode("0"));
  lhs_ast.add(makePatENode("+", {la, l0}));

  PatternAst<Symbol> rhs_ast;
  rhs_ast.add(makePatVar("?a"));

  Rewrite<Symbol> rule("plus-zero", Pattern<Symbol>(std::move(lhs_ast)),
                       Pattern<Symbol>(std::move(rhs_ast)));
  rule.run(graph);
  graph.rebuild();

  Extractor<Symbol> extractor(graph);
  auto [cost, expr] = extractor.findBest(plus);

  EXPECT_EQ(cost, 1U);
  EXPECT_EQ(expr.size(), 1U);
  EXPECT_EQ(expr.root().op.value, "a");
}

TEST(ExtractorTest, NestedReconstruction) {
  // Build (f (g a)).  Extract should reconstruct the full tree.
  EGraph<Symbol> graph;
  Id a = graph.add(makeNode("a"));
  Id ga = graph.add(makeNode("g", {a}));
  Id fga = graph.add(makeNode("f", {ga}));

  Extractor<Symbol> extractor(graph);
  auto [cost, expr] = extractor.findBest(fga);

  // Cost: a=1, g(a)=2, f(g(a))=3
  EXPECT_EQ(cost, 3U);
  EXPECT_EQ(expr.size(), 3U);
  EXPECT_EQ(expr.root().op.value, "f");
  EXPECT_EQ(expr.root().children.size(), 1U);

  // The child of f should point to the g node.
  const auto &g_node = expr[expr.root().children[0]];
  EXPECT_EQ(g_node.op.value, "g");
  EXPECT_EQ(g_node.children.size(), 1U);

  // The child of g should be "a".
  const auto &a_node = expr[g_node.children[0]];
  EXPECT_EQ(a_node.op.value, "a");
  EXPECT_TRUE(a_node.children.empty());
}

TEST(ExtractorTest, FindBestCostAndNode) {
  // Test findBestCost and findBestNode separately.
  EGraph<Symbol> graph;
  Id a = graph.add(makeNode("a"));
  Id b = graph.add(makeNode("b"));
  Id plus = graph.add(makeNode("+", {a, b}));

  Extractor<Symbol> extractor(graph);

  EXPECT_EQ(extractor.findBestCost(a), 1U);
  EXPECT_EQ(extractor.findBestCost(plus), 3U);
  EXPECT_EQ(extractor.findBestNode(a).op.value, "a");
  EXPECT_EQ(extractor.findBestNode(plus).op.value, "+");
}

TEST(ExtractorTest, AstDepthCostFunction) {
  // Test with AstDepth instead of AstSize.
  // (f (g a)) should have depth 3, not size 3.
  EGraph<Symbol> graph;
  Id a = graph.add(makeNode("a"));
  Id ga = graph.add(makeNode("g", {a}));
  Id fga = graph.add(makeNode("f", {ga}));

  // (+ a a) has depth 2.
  Id plus_aa = graph.add(makeNode("+", {a, a}));

  Extractor<Symbol, nndeploy::egraph::NoAnalysis, AstDepth> extractor(graph);

  EXPECT_EQ(extractor.findBestCost(a), 1U);
  EXPECT_EQ(extractor.findBestCost(ga), 2U);
  EXPECT_EQ(extractor.findBestCost(fga), 3U);
  EXPECT_EQ(extractor.findBestCost(plus_aa), 2U);
}

TEST(ExtractorTest, SharedChildDeduplication) {
  // Build (+ a a).  The extracted RecExpr should share the "a" node.
  EGraph<Symbol> graph;
  Id a = graph.add(makeNode("a"));
  Id plus_aa = graph.add(makeNode("+", {a, a}));

  Extractor<Symbol> extractor(graph);
  auto [cost, expr] = extractor.findBest(plus_aa);

  // Cost: a=1, +(a,a) = 1+1+1 = 3
  EXPECT_EQ(cost, 3U);

  // The RecExpr should have 2 nodes: "a" and "+".
  // Because both children of "+" point to the same e-class, the builder
  // should deduplicate them into one RecExpr node.
  EXPECT_EQ(expr.size(), 2U);
  EXPECT_EQ(expr.root().op.value, "+");
  // Both children should point to the same RecExpr id.
  EXPECT_EQ(expr.root().children[0], expr.root().children[1]);
}

TEST(ExtractorTest, ChooseBestAmongMultipleEquivalents) {
  // Build a, b, (+ a b).  Merge a and (+ a b).
  // Extractor should pick "a" (cost 1) over "(+ a b)" (cost 3).
  EGraph<Symbol> graph;
  Id a = graph.add(makeNode("a"));
  Id b = graph.add(makeNode("b"));
  Id plus = graph.add(makeNode("+", {a, b}));

  graph.merge(a, plus);
  graph.rebuild();

  Extractor<Symbol> extractor(graph);
  auto [cost, expr] = extractor.findBest(plus);

  EXPECT_EQ(cost, 1U);
  EXPECT_EQ(expr.root().op.value, "a");
}

TEST(ExtractorTest, InvalidIdThrows) {
  EGraph<Symbol> graph;
  graph.add(makeNode("a"));

  Extractor<Symbol> extractor(graph);
  EXPECT_THROW(extractor.findBest(Id(99999)), std::out_of_range);
  EXPECT_THROW(extractor.findBestCost(Id(99999)), std::out_of_range);
  EXPECT_THROW(extractor.findBestNode(Id(99999)), std::out_of_range);
}

// ===================================================================
// Runner helper: build rewrite rules used in multiple tests.
// ===================================================================

// Build LHS pattern: (+ ?a 0) and RHS pattern: ?a
Rewrite<Symbol> makePlusZeroRule() {
  PatternAst<Symbol> lhs_ast;
  Id la = lhs_ast.add(makePatVar("?a"));
  Id l0 = lhs_ast.add(makePatENode("0"));
  lhs_ast.add(makePatENode("+", {la, l0}));

  PatternAst<Symbol> rhs_ast;
  rhs_ast.add(makePatVar("?a"));

  return Rewrite<Symbol>("plus-zero", Pattern<Symbol>(std::move(lhs_ast)),
                         Pattern<Symbol>(std::move(rhs_ast)));
}

// Build commutativity rule: (+ ?a ?b) => (+ ?b ?a)
Rewrite<Symbol> makeCommRule() {
  PatternAst<Symbol> lhs_ast;
  Id la = lhs_ast.add(makePatVar("?a"));
  Id lb = lhs_ast.add(makePatVar("?b"));
  lhs_ast.add(makePatENode("+", {la, lb}));

  PatternAst<Symbol> rhs_ast;
  Id rb = rhs_ast.add(makePatVar("?b"));
  Id ra = rhs_ast.add(makePatVar("?a"));
  rhs_ast.add(makePatENode("+", {rb, ra}));

  return Rewrite<Symbol>("plus-comm", Pattern<Symbol>(std::move(lhs_ast)),
                         Pattern<Symbol>(std::move(rhs_ast)));
}

// Build associativity rule: (+ (+ ?a ?b) ?c) => (+ ?a (+ ?b ?c))
Rewrite<Symbol> makeAssocRule() {
  PatternAst<Symbol> lhs_ast;
  Id la = lhs_ast.add(makePatVar("?a"));
  Id lb = lhs_ast.add(makePatVar("?b"));
  Id lp = lhs_ast.add(makePatENode("+", {la, lb}));
  Id lc = lhs_ast.add(makePatVar("?c"));
  lhs_ast.add(makePatENode("+", {lp, lc}));

  PatternAst<Symbol> rhs_ast;
  Id rb = rhs_ast.add(makePatVar("?b"));
  Id rc = rhs_ast.add(makePatVar("?c"));
  Id rp = rhs_ast.add(makePatENode("+", {rb, rc}));
  Id ra = rhs_ast.add(makePatVar("?a"));
  rhs_ast.add(makePatENode("+", {ra, rp}));

  return Rewrite<Symbol>("plus-assoc", Pattern<Symbol>(std::move(lhs_ast)),
                         Pattern<Symbol>(std::move(rhs_ast)));
}

// ===================================================================
// Runner tests
// ===================================================================

TEST(RunnerTest, SaturatesWithAdditiveIdentity) {
  // (+ a 0) should be simplified to a.
  Runner<Symbol> runner;

  RecExpr<Symbol> expr;
  Id ea = expr.add(makeNode("a"));
  Id e0 = expr.add(makeNode("0"));
  expr.add(makeNode("+", {ea, e0}));
  Id root = runner.addExpr(expr);

  std::vector<Rewrite<Symbol>> rules;
  rules.push_back(makePlusZeroRule());
  runner.run(rules);

  ASSERT_TRUE(runner.has_stop_reason);
  EXPECT_EQ(runner.stop_reason.kind, StopReasonKind::Saturated);

  // After saturation, (+ a 0) and a should be in the same class.
  Id a_id = runner.egraph.find(root);
  Extractor<Symbol> extractor(runner.egraph);
  auto [cost, best_expr] = extractor.findBest(a_id);
  EXPECT_EQ(cost, 1U);
  EXPECT_EQ(best_expr.root().op.value, "a");
}

TEST(RunnerTest, IterationLimitStopsRunner) {
  // Commutativity applied to (+ a b) saturates in 1 iteration, but if we
  // combine with associativity on a deep expression, we can hit iter limit.
  Runner<Symbol> runner;
  runner.withIterLimit(3);

  // Build: (+ (+ a b) (+ c d))
  RecExpr<Symbol> expr;
  Id ea = expr.add(makeNode("a"));
  Id eb = expr.add(makeNode("b"));
  Id ec = expr.add(makeNode("c"));
  Id ed = expr.add(makeNode("d"));
  Id eab = expr.add(makeNode("+", {ea, eb}));
  Id ecd = expr.add(makeNode("+", {ec, ed}));
  expr.add(makeNode("+", {eab, ecd}));
  runner.addExpr(expr);

  std::vector<Rewrite<Symbol>> rules;
  rules.push_back(makeCommRule());
  rules.push_back(makeAssocRule());
  runner.run(rules);

  ASSERT_TRUE(runner.has_stop_reason);
  // Should stop due to iteration limit, not saturation.
  EXPECT_EQ(runner.stop_reason.kind, StopReasonKind::IterationLimit);
  // 3 productive iterations + 1 that triggers the limit check at the start.
  EXPECT_EQ(runner.iterations.size(), 4U);
}

TEST(RunnerTest, NodeLimitStopsRunner) {
  Runner<Symbol> runner;
  runner.withIterLimit(1000).withNodeLimit(20);

  // Build: (+ (+ a b) (+ c d))
  RecExpr<Symbol> expr;
  Id ea = expr.add(makeNode("a"));
  Id eb = expr.add(makeNode("b"));
  Id ec = expr.add(makeNode("c"));
  Id ed = expr.add(makeNode("d"));
  Id eab = expr.add(makeNode("+", {ea, eb}));
  Id ecd = expr.add(makeNode("+", {ec, ed}));
  expr.add(makeNode("+", {eab, ecd}));
  runner.addExpr(expr);

  std::vector<Rewrite<Symbol>> rules;
  rules.push_back(makeCommRule());
  rules.push_back(makeAssocRule());
  runner.run(rules);

  ASSERT_TRUE(runner.has_stop_reason);
  EXPECT_EQ(runner.stop_reason.kind, StopReasonKind::NodeLimit);
}

TEST(RunnerTest, ReportContainsCorrectData) {
  Runner<Symbol> runner;

  RecExpr<Symbol> expr;
  Id ea = expr.add(makeNode("a"));
  Id e0 = expr.add(makeNode("0"));
  expr.add(makeNode("+", {ea, e0}));
  runner.addExpr(expr);

  std::vector<Rewrite<Symbol>> rules;
  rules.push_back(makePlusZeroRule());
  runner.run(rules);

  Report report = runner.report();
  EXPECT_EQ(report.stop_reason.kind, StopReasonKind::Saturated);
  EXPECT_GT(report.iterations, 0U);
  EXPECT_GT(report.egraph_nodes, 0U);
  EXPECT_GT(report.egraph_classes, 0U);
  EXPECT_GE(report.total_time, 0.0);
  // toString should not crash.
  EXPECT_FALSE(report.toString().empty());
}

TEST(RunnerTest, IterationDataTracksAppliedRules) {
  Runner<Symbol> runner;

  RecExpr<Symbol> expr;
  Id ea = expr.add(makeNode("a"));
  Id e0 = expr.add(makeNode("0"));
  expr.add(makeNode("+", {ea, e0}));
  runner.addExpr(expr);

  std::vector<Rewrite<Symbol>> rules;
  rules.push_back(makePlusZeroRule());
  runner.run(rules);

  // The first iteration should have applied "plus-zero".
  ASSERT_FALSE(runner.iterations.empty());
  const auto &first = runner.iterations[0];
  EXPECT_GT(first.applied.count("plus-zero"), 0U);
  EXPECT_GT(first.applied.at("plus-zero"), 0U);

  // The last iteration (saturation) should have no applications.
  const auto &last = runner.iterations.back();
  EXPECT_TRUE(last.applied.empty());
}

TEST(RunnerTest, HookCanStopRunner) {
  Runner<Symbol> runner;
  runner.withIterLimit(100);

  RecExpr<Symbol> expr;
  Id ea = expr.add(makeNode("a"));
  Id e0 = expr.add(makeNode("0"));
  expr.add(makeNode("+", {ea, e0}));
  runner.addExpr(expr);

  // Hook that stops after the 2nd iteration.
  std::size_t hook_calls = 0;
  runner.withHook(
      [&hook_calls](Runner<Symbol> & /*r*/, std::string &msg) -> bool {
        ++hook_calls;
        if (hook_calls >= 2) {
          msg = "stopped by hook";
          return false;
        }
        return true;
      });

  std::vector<Rewrite<Symbol>> rules;
  rules.push_back(makePlusZeroRule());
  runner.run(rules);

  ASSERT_TRUE(runner.has_stop_reason);
  EXPECT_EQ(runner.stop_reason.kind, StopReasonKind::Other);
  EXPECT_EQ(runner.stop_reason.message, "stopped by hook");
}

TEST(RunnerTest, MultipleRulesSaturate) {
  // Apply both plus-zero and commutativity.
  // (+ 0 a) should be simplified to a via commutativity then plus-zero.
  Runner<Symbol> runner;

  RecExpr<Symbol> expr;
  Id e0 = expr.add(makeNode("0"));
  Id ea = expr.add(makeNode("a"));
  expr.add(makeNode("+", {e0, ea}));
  Id root = runner.addExpr(expr);

  std::vector<Rewrite<Symbol>> rules;
  rules.push_back(makePlusZeroRule());
  rules.push_back(makeCommRule());
  runner.run(rules);

  ASSERT_TRUE(runner.has_stop_reason);
  EXPECT_EQ(runner.stop_reason.kind, StopReasonKind::Saturated);

  Extractor<Symbol> extractor(runner.egraph);
  auto [cost, best_expr] = extractor.findBest(runner.egraph.find(root));
  EXPECT_EQ(cost, 1U);
  EXPECT_EQ(best_expr.root().op.value, "a");
}

TEST(RunnerTest, EmptyRulesSaturateImmediately) {
  Runner<Symbol> runner;

  RecExpr<Symbol> expr;
  expr.add(makeNode("a"));
  runner.addExpr(expr);

  std::vector<Rewrite<Symbol>> rules;  // no rules
  runner.run(rules);

  ASSERT_TRUE(runner.has_stop_reason);
  EXPECT_EQ(runner.stop_reason.kind, StopReasonKind::Saturated);
  EXPECT_EQ(runner.iterations.size(), 1U);
}

TEST(RunnerTest, RootsAreTracked) {
  Runner<Symbol> runner;

  RecExpr<Symbol> expr1;
  expr1.add(makeNode("a"));
  Id r1 = runner.addExpr(expr1);

  RecExpr<Symbol> expr2;
  expr2.add(makeNode("b"));
  Id r2 = runner.addExpr(expr2);

  EXPECT_EQ(runner.roots.size(), 2U);
  EXPECT_EQ(runner.roots[0], r1);
  EXPECT_EQ(runner.roots[1], r2);
}

TEST(RunnerTest, BackoffSchedulerBansExplosiveRule) {
  // Use a very low match limit so the commutativity rule gets banned.
  auto scheduler = std::make_unique<BackoffScheduler<Symbol>>(
      /* initial_match_limit = */ 1, /* initial_ban_length = */ 2);

  Runner<Symbol> runner;
  runner.withIterLimit(20).withScheduler(std::move(scheduler));

  // Build: (+ a b) — commutativity produces 2 matches initially, exceeding
  // the limit of 1.
  RecExpr<Symbol> expr;
  Id ea = expr.add(makeNode("a"));
  Id eb = expr.add(makeNode("b"));
  expr.add(makeNode("+", {ea, eb}));
  runner.addExpr(expr);

  std::vector<Rewrite<Symbol>> rules;
  rules.push_back(makeCommRule());
  runner.run(rules);

  ASSERT_TRUE(runner.has_stop_reason);
  // The runner should eventually saturate or hit iteration limit.
  // With a match limit of 1 and ban length of 2, the commutativity rule
  // will be banned for some iterations, then unbanned and retried.
  // It should eventually saturate (comm is self-closing once (+ b a) exists).
  EXPECT_TRUE(runner.stop_reason.kind == StopReasonKind::Saturated ||
              runner.stop_reason.kind == StopReasonKind::IterationLimit);
}

TEST(RunnerTest, SimpleSchedulerPassesThrough) {
  // Explicitly use SimpleScheduler.
  auto scheduler = std::make_unique<SimpleScheduler<Symbol>>();

  Runner<Symbol> runner;
  runner.withScheduler(std::move(scheduler));

  RecExpr<Symbol> expr;
  Id ea = expr.add(makeNode("a"));
  Id e0 = expr.add(makeNode("0"));
  expr.add(makeNode("+", {ea, e0}));
  runner.addExpr(expr);

  std::vector<Rewrite<Symbol>> rules;
  rules.push_back(makePlusZeroRule());
  runner.run(rules);

  ASSERT_TRUE(runner.has_stop_reason);
  EXPECT_EQ(runner.stop_reason.kind, StopReasonKind::Saturated);
}

TEST(RunnerTest, StopReasonToStringCoversAllVariants) {
  EXPECT_EQ(StopReason::Saturated().toString(), "Saturated");
  EXPECT_NE(StopReason::IterationLimit(30).toString().find("30"),
            std::string::npos);
  EXPECT_NE(StopReason::NodeLimit(10000).toString().find("10000"),
            std::string::npos);
  EXPECT_NE(StopReason::TimeLimit(1.5).toString().find("1.5"),
            std::string::npos);
  EXPECT_NE(StopReason::Other("test").toString().find("test"),
            std::string::npos);
}

TEST(RunnerTest, BackoffSchedulerBanDurationIsExact) {
  // Verify that a rule banned for N iterations is actually skipped for
  // exactly N iterations, not N-1.
  //
  // Setup: commutativity rule with match_limit=1, ban_length=3.
  // With two (+ ..) nodes in the graph, the first search produces 2
  // substitutions (one per node), exceeding threshold 1.  The rule is
  // banned for 3 iterations.
  auto scheduler = std::make_unique<BackoffScheduler<Symbol>>(
      /* initial_match_limit = */ 1, /* initial_ban_length = */ 3);

  Runner<Symbol> runner;
  runner.withIterLimit(10).withScheduler(std::move(scheduler));

  // Build two addition nodes so the first search produces 2 matches.
  RecExpr<Symbol> expr;
  Id ea = expr.add(makeNode("a"));
  Id eb = expr.add(makeNode("b"));
  expr.add(makeNode("+", {ea, eb}));
  runner.addExpr(expr);

  RecExpr<Symbol> expr2;
  Id ec = expr2.add(makeNode("c"));
  Id ed = expr2.add(makeNode("d"));
  expr2.add(makeNode("+", {ec, ed}));
  runner.addExpr(expr2);

  std::vector<Rewrite<Symbol>> rules;
  rules.push_back(makeCommRule());
  runner.run(rules);

  ASSERT_TRUE(runner.has_stop_reason);
  ASSERT_GE(runner.iterations.size(), 2U);

  // First iteration: 2 matches > threshold 1 → rule is banned, no merges.
  EXPECT_TRUE(runner.iterations[0].applied.empty());

  // Count consecutive iterations from the start with no "plus-comm"
  // application. There should be at least 3 such iterations (the ban length)
  // before the rule is allowed again.  canStop() blocks saturation while rules
  // are banned, so the runner continues past the ban.
  std::size_t consecutive_empty = 0;
  for (const auto &it : runner.iterations) {
    if (it.applied.count("plus-comm") == 0) {
      ++consecutive_empty;
    } else {
      break;
    }
  }
  // ban_length=3, times_banned was 0 when ban was set → effective ban = 3.
  EXPECT_GE(consecutive_empty, 3U);
}

TEST(RunnerTest, HookMutationPreventsImmediateSaturation) {
  // If a hook adds a node to the e-graph, the runner must not declare
  // saturation on that iteration even if no rules fired.
  Runner<Symbol> runner;
  runner.withIterLimit(10);

  RecExpr<Symbol> expr;
  expr.add(makeNode("a"));
  runner.addExpr(expr);

  // Hook that adds a new node on the first iteration only.
  bool added = false;
  runner.withHook([&added](Runner<Symbol> &r, std::string & /*msg*/) -> bool {
    if (!added) {
      r.egraph.add(makeNode("hook_added"));
      added = true;
    }
    return true;
  });

  std::vector<Rewrite<Symbol>> rules;  // no rules
  runner.run(rules);

  ASSERT_TRUE(runner.has_stop_reason);
  EXPECT_EQ(runner.stop_reason.kind, StopReasonKind::Saturated);
  // The runner must NOT saturate on the first iteration because the hook
  // changed the e-graph. It should take at least 2 iterations.
  EXPECT_GE(runner.iterations.size(), 2U);
}

// ===================================================================
// Searcher / Applier tests
// ===================================================================

// A custom searcher that finds all e-classes containing a leaf node with
// a specific operator name.  For each such class it produces a substitution
// binding ?x to the matched class.
class LeafSearcher : public Searcher<Symbol> {
 public:
  explicit LeafSearcher(std::string op_name) : op_name_(std::move(op_name)) {}

  std::vector<SearchMatches<Symbol>> search(
      EGraph<Symbol> &egraph) const override {
    std::vector<SearchMatches<Symbol>> results;
    for (const auto &root_id : egraph.roots()) {
      const auto *eclass = egraph.getEClass(root_id);
      if (!eclass) continue;
      for (const auto &node : eclass->nodes) {
        if (node.op.value == op_name_ && node.children.empty()) {
          SearchMatches<Symbol> sm;
          sm.eclass = egraph.find(root_id);
          Subst s;
          s.insert(Var::fromString("?x"), sm.eclass);
          sm.substs.push_back(std::move(s));
          results.push_back(std::move(sm));
          break;  // one match per class
        }
      }
    }
    return results;
  }

  std::vector<Var> vars() const override { return {Var::fromString("?x")}; }

 private:
  std::string op_name_;
};

// A custom applier that adds a new node wrapping the matched class with
// a given operator, and merges it into the matched class.
// Effectively: matched_class = op(matched_class)
class WrapApplier : public Applier<Symbol> {
 public:
  explicit WrapApplier(std::string wrap_op) : wrap_op_(std::move(wrap_op)) {}

  std::vector<Id> applyOne(EGraph<Symbol> &egraph, Id eclass,
                           const Subst &subst) const override {
    (void)subst;
    Id canonical = egraph.find(eclass);
    ENode<Symbol> wrapper;
    wrapper.op = Symbol{wrap_op_};
    wrapper.children = {canonical};
    Id new_id = egraph.add(wrapper);
    if (egraph.find(new_id) != canonical) {
      egraph.merge(canonical, new_id);
      return {egraph.find(canonical)};
    }
    return {};
  }

 private:
  std::string wrap_op_;
};

// A conditional applier that only applies when a condition is met.
// Here: only merge if the matched class has exactly one enode.
class ConditionalApplier : public Applier<Symbol> {
 public:
  explicit ConditionalApplier(std::string new_op)
      : new_op_(std::move(new_op)) {}

  std::vector<Id> applyOne(EGraph<Symbol> &egraph, Id eclass,
                           const Subst & /*subst*/) const override {
    Id canonical = egraph.find(eclass);
    const auto *klass = egraph.getEClass(canonical);
    if (!klass || klass->nodes.size() != 1) {
      return {};  // condition not met
    }
    ENode<Symbol> new_node;
    new_node.op = Symbol{new_op_};
    Id new_id = egraph.add(new_node);
    if (egraph.find(new_id) != canonical) {
      egraph.merge(canonical, new_id);
      return {egraph.find(canonical)};
    }
    return {};
  }

 private:
  std::string new_op_;
};

TEST(SearcherApplierTest, PatternSearcherDirectly) {
  // Use PatternSearcher directly (not through Rewrite).
  auto setup = makePlusGraph("a", "b");

  PatternSearcher<Symbol> searcher(
      Pattern<Symbol>(makeBinaryPatternAst("+", "?x", "?y")));
  auto matches = searcher.search(setup.graph);

  EXPECT_EQ(matches.size(), 1U);
  EXPECT_EQ(matches[0].substs.size(), 1U);

  // Check vars.
  auto vars = searcher.vars();
  EXPECT_EQ(vars.size(), 2U);

  // Access the pattern.
  EXPECT_EQ(searcher.pattern().ast().size(), 3U);
}

TEST(SearcherApplierTest, PatternApplierDirectly) {
  // Use PatternApplier directly.
  EGraph<Symbol> graph;
  Id a = graph.add(makeNode("a"));
  Id zero = graph.add(makeNode("0"));
  Id plus = graph.add(makeNode("+", {a, zero}));

  // RHS pattern: ?a
  PatternApplier<Symbol> applier(Pattern<Symbol>(makeLeafPatternAst("?a")));

  // Build a substitution manually.
  Subst subst;
  subst.insert(Var::fromString("?a"), a);

  // Apply.
  auto ids = applier.applyOne(graph, plus, subst);
  EXPECT_EQ(ids.size(), 1U);

  graph.rebuild();
  EXPECT_EQ(graph.find(plus), graph.find(a));
}

TEST(SearcherApplierTest, CustomSearcherFindsLeaves) {
  EGraph<Symbol> graph;
  Id a = graph.add(makeNode("a"));
  Id b = graph.add(makeNode("b"));
  Id c = graph.add(makeNode("a"));  // deduplicates with first "a"
  (void)c;
  graph.add(makeNode("+", {a, b}));

  LeafSearcher searcher("a");
  auto matches = searcher.search(graph);

  // Should find exactly one class containing "a".
  EXPECT_EQ(matches.size(), 1U);
  EXPECT_EQ(matches[0].substs.size(), 1U);
  EXPECT_EQ(graph.find(*matches[0].substs[0].get(Var::fromString("?x"))),
            graph.find(a));
}

TEST(SearcherApplierTest, CustomApplierWrapsNode) {
  EGraph<Symbol> graph;
  Id a = graph.add(makeNode("a"));

  WrapApplier applier("wrap");
  Subst subst;
  subst.insert(Var::fromString("?x"), a);

  auto ids = applier.applyOne(graph, a, subst);
  EXPECT_EQ(ids.size(), 1U);

  graph.rebuild();

  // After wrapping, the class of "a" now also contains "wrap(a)".
  const auto *klass = graph.getEClass(a);
  ASSERT_NE(klass, nullptr);
  EXPECT_GE(klass->nodes.size(), 2U);

  bool has_wrap = false;
  for (const auto &node : klass->nodes) {
    if (node.op.value == "wrap") {
      has_wrap = true;
      break;
    }
  }
  EXPECT_TRUE(has_wrap);
}

TEST(SearcherApplierTest, GenericRewriteConstructor) {
  // Build a rewrite using the generic (Searcher + Applier) constructor.
  auto searcher = std::make_shared<LeafSearcher>("a");
  auto applier = std::make_shared<WrapApplier>("id");

  Rewrite<Symbol> rule("wrap-a", std::shared_ptr<Searcher<Symbol>>(searcher),
                       std::shared_ptr<Applier<Symbol>>(applier));

  EXPECT_EQ(rule.name(), "wrap-a");

  // Verify that lhs()/rhs() throw for non-pattern rewrites.
  EXPECT_THROW(rule.lhs(), std::logic_error);
  EXPECT_THROW(rule.rhs(), std::logic_error);

  // Verify patternSearcher()/patternApplier() return nullptr.
  EXPECT_EQ(rule.patternSearcher(), nullptr);
  EXPECT_EQ(rule.patternApplier(), nullptr);

  // But searcher()/applier() work.
  EXPECT_NE(&rule.searcher(), nullptr);
  EXPECT_NE(&rule.applier(), nullptr);
}

TEST(SearcherApplierTest, NullSearcherRejected) {
  auto applier = std::make_shared<WrapApplier>("id");
  EXPECT_THROW(
      (Rewrite<Symbol>("bad", std::shared_ptr<Searcher<Symbol>>(nullptr),
                       std::shared_ptr<Applier<Symbol>>(applier))),
      std::invalid_argument);
}

TEST(SearcherApplierTest, NullApplierRejected) {
  auto searcher = std::make_shared<LeafSearcher>("a");
  EXPECT_THROW(
      (Rewrite<Symbol>("bad", std::shared_ptr<Searcher<Symbol>>(searcher),
                       std::shared_ptr<Applier<Symbol>>(nullptr))),
      std::invalid_argument);
}

TEST(SearcherApplierTest, GenericRewriteSearchAndApply) {
  // End-to-end: use the generic rewrite with custom searcher/applier.
  EGraph<Symbol> graph;
  Id a = graph.add(makeNode("a"));
  Id b = graph.add(makeNode("b"));
  graph.add(makeNode("+", {a, b}));

  auto searcher = std::make_shared<LeafSearcher>("a");
  auto applier = std::make_shared<WrapApplier>("id");

  Rewrite<Symbol> rule("id-a", std::shared_ptr<Searcher<Symbol>>(searcher),
                       std::shared_ptr<Applier<Symbol>>(applier));

  std::size_t merges = rule.run(graph);
  EXPECT_GE(merges, 1U);

  graph.rebuild();

  // "a" and "id(a)" should now be in the same class.
  const auto *klass = graph.getEClass(a);
  ASSERT_NE(klass, nullptr);
  bool has_id = false;
  for (const auto &node : klass->nodes) {
    if (node.op.value == "id") {
      has_id = true;
      break;
    }
  }
  EXPECT_TRUE(has_id);
}

TEST(SearcherApplierTest, GenericRewriteViaRunner) {
  // Run a custom rewrite through the Runner.
  Runner<Symbol> runner;
  runner.withIterLimit(10);

  RecExpr<Symbol> expr;
  Id ea = expr.add(makeNode("a"));
  Id eb = expr.add(makeNode("b"));
  expr.add(makeNode("+", {ea, eb}));
  runner.addExpr(expr);

  // ConditionalApplier only fires when the matched class has exactly 1 node.
  // After the first application, "a" class has 2 nodes, so it won't fire again.
  auto searcher = std::make_shared<LeafSearcher>("a");
  auto applier = std::make_shared<ConditionalApplier>("a_alias");

  Rewrite<Symbol> rule("conditional-alias",
                       std::shared_ptr<Searcher<Symbol>>(searcher),
                       std::shared_ptr<Applier<Symbol>>(applier));

  std::vector<Rewrite<Symbol>> rules;
  rules.push_back(std::move(rule));
  runner.run(rules);

  ASSERT_TRUE(runner.has_stop_reason);
  // Should saturate (the conditional applier stops firing after first merge).
  EXPECT_EQ(runner.stop_reason.kind, StopReasonKind::Saturated);

  // "a" and "a_alias" should be in the same class.
  Id a_id = runner.egraph.find(runner.roots[0]);
  // Find "a" in the graph — it's a child of the "+" root.
  const auto *root_class = runner.egraph.getEClass(a_id);
  ASSERT_NE(root_class, nullptr);

  // Find the "a" class and verify it contains "a_alias".
  bool found_alias = false;
  for (const auto &root_id : runner.egraph.roots()) {
    const auto *klass = runner.egraph.getEClass(root_id);
    if (!klass) continue;
    for (const auto &node : klass->nodes) {
      if (node.op.value == "a_alias") {
        // a_alias should be in the same class as "a"
        found_alias = true;
        break;
      }
    }
    if (found_alias) break;
  }
  EXPECT_TRUE(found_alias);
}

TEST(SearcherApplierTest, PatternRewriteLhsRhsAccessors) {
  // Pattern-based rewrites should still support lhs()/rhs() accessors.
  PatternAst<Symbol> lhs_ast;
  Id la = lhs_ast.add(makePatVar("?a"));
  Id l0 = lhs_ast.add(makePatENode("0"));
  lhs_ast.add(makePatENode("+", {la, l0}));

  PatternAst<Symbol> rhs_ast = makeLeafPatternAst("?a");

  Rewrite<Symbol> rule("plus-zero", Pattern<Symbol>(std::move(lhs_ast)),
                       Pattern<Symbol>(std::move(rhs_ast)));

  // lhs() and rhs() should work.
  EXPECT_EQ(rule.lhs().ast().size(), 3U);
  EXPECT_EQ(rule.rhs().ast().size(), 1U);
  EXPECT_EQ(rule.lhs().vars().size(), 1U);
  EXPECT_EQ(rule.rhs().vars().size(), 1U);

  // patternSearcher()/patternApplier() should return non-null.
  EXPECT_NE(rule.patternSearcher(), nullptr);
  EXPECT_NE(rule.patternApplier(), nullptr);
}

TEST(SearcherApplierTest, MixedRulesInRunner) {
  // Run a mix of pattern-based and custom rewrites together in the Runner.
  Runner<Symbol> runner;
  runner.withIterLimit(10);

  // Expression: (+ a 0)
  RecExpr<Symbol> expr;
  Id ea = expr.add(makeNode("a"));
  Id e0 = expr.add(makeNode("0"));
  expr.add(makeNode("+", {ea, e0}));
  Id root = runner.addExpr(expr);

  // Rule 1: pattern-based (+ ?a 0) => ?a
  Rewrite<Symbol> plus_zero = makePlusZeroRule();

  // Rule 2: custom — find leaves named "a", wrap with "id"
  auto searcher = std::make_shared<LeafSearcher>("a");
  auto applier = std::make_shared<WrapApplier>("id");
  Rewrite<Symbol> wrap_rule("wrap-a",
                            std::shared_ptr<Searcher<Symbol>>(searcher),
                            std::shared_ptr<Applier<Symbol>>(applier));

  std::vector<Rewrite<Symbol>> rules;
  rules.push_back(std::move(plus_zero));
  rules.push_back(std::move(wrap_rule));
  runner.run(rules);

  ASSERT_TRUE(runner.has_stop_reason);

  // After saturation: (+ a 0) should be equivalent to a.
  Extractor<Symbol> extractor(runner.egraph);
  Id final_root = runner.egraph.find(root);
  auto [cost, best_expr] = extractor.findBest(final_root);
  // The cheapest representation should be "a" (cost 1).
  EXPECT_EQ(cost, 1U);
  EXPECT_EQ(best_expr.root().op.value, "a");
}

TEST(SearcherApplierTest, CustomApplierApplyMatchesOverride) {
  // A custom applier that overrides applyMatches to count differently.
  class CountingApplier : public Applier<Symbol> {
   public:
    mutable std::size_t apply_one_calls = 0;

    std::vector<Id> applyOne(EGraph<Symbol> &egraph, Id eclass,
                             const Subst & /*subst*/) const override {
      ++apply_one_calls;
      Id canonical = egraph.find(eclass);
      ENode<Symbol> n;
      n.op = Symbol{"counted_" + std::to_string(apply_one_calls)};
      Id new_id = egraph.add(n);
      if (egraph.find(new_id) != canonical) {
        egraph.merge(canonical, new_id);
        return {egraph.find(canonical)};
      }
      return {};
    }
  };

  EGraph<Symbol> graph;
  Id a = graph.add(makeNode("a"));
  Id b = graph.add(makeNode("b"));

  // Create matches with 2 substitutions.
  SearchMatches<Symbol> sm;
  sm.eclass = graph.find(a);
  Subst s1, s2;
  s1.insert(Var::fromString("?x"), a);
  s2.insert(Var::fromString("?x"), b);
  sm.substs = {s1, s2};

  auto applier = std::make_shared<CountingApplier>();
  std::vector<SearchMatches<Symbol>> matches = {sm};

  std::size_t merges = applier->applyMatches(graph, matches);
  EXPECT_EQ(applier->apply_one_calls, 2U);
  EXPECT_EQ(merges, 2U);  // 2 new nodes merged
}

TEST(SearcherApplierTest, SearcherSearchEclassDefault) {
  // Test the default searchEclass implementation in the Searcher base.
  EGraph<Symbol> graph;
  Id a = graph.add(makeNode("a"));
  Id b = graph.add(makeNode("b"));
  graph.add(makeNode("+", {a, b}));

  LeafSearcher searcher("a");

  // searchEclass for the class of "a" should find a match.
  auto matches = searcher.searchEclass(graph, a);
  EXPECT_EQ(matches.substs.size(), 1U);

  // searchEclass for the class of "b" should find nothing.
  auto no_matches = searcher.searchEclass(graph, b);
  EXPECT_TRUE(no_matches.substs.empty());
}

// ===================================================================
// Bug-fix regression tests
// ===================================================================

TEST(RunnerTest, RunnerIsReusable) {
  // A second run() on the same Runner should work correctly, not carry
  // over stale state from the first run.
  Runner<Symbol> runner;
  runner.withIterLimit(30);

  // First run: (+ a 0) with plus-zero rule.
  RecExpr<Symbol> expr;
  Id ea = expr.add(makeNode("a"));
  Id e0 = expr.add(makeNode("0"));
  expr.add(makeNode("+", {ea, e0}));
  runner.addExpr(expr);

  std::vector<Rewrite<Symbol>> rules;
  rules.push_back(makePlusZeroRule());
  runner.run(rules);

  ASSERT_TRUE(runner.has_stop_reason);
  EXPECT_EQ(runner.stop_reason.kind, StopReasonKind::Saturated);
  std::size_t first_iters = runner.iterations.size();
  EXPECT_GT(first_iters, 0U);

  // Add a new expression and run again.
  RecExpr<Symbol> expr2;
  Id eb = expr2.add(makeNode("b"));
  Id e02 = expr2.add(makeNode("0"));
  expr2.add(makeNode("+", {eb, e02}));
  runner.addExpr(expr2);

  runner.run(rules);

  // The second run should have its own iteration history, not accumulate.
  ASSERT_TRUE(runner.has_stop_reason);
  EXPECT_EQ(runner.stop_reason.kind, StopReasonKind::Saturated);
  // iterations should reflect only the second run, not first + second.
  EXPECT_LE(runner.iterations.size(), first_iters + 2);

  // Report should be consistent.
  Report report = runner.report();
  EXPECT_EQ(report.stop_reason.kind, StopReasonKind::Saturated);
  EXPECT_EQ(report.iterations, runner.iterations.size());
}

TEST(RunnerTest, WithSchedulerNullptrRejected) {
  Runner<Symbol> runner;
  EXPECT_THROW(
      runner.withScheduler(std::unique_ptr<SimpleScheduler<Symbol>>(nullptr)),
      std::invalid_argument);
}

TEST(RunnerTest, ReportBeforeRunIsValid) {
  // Calling report() before run() should return a valid Report with
  // sensible defaults, not read uninitialized memory.
  Runner<Symbol> runner;

  RecExpr<Symbol> expr;
  expr.add(makeNode("a"));
  runner.addExpr(expr);

  Report report = runner.report();
  // No iterations have been run.
  EXPECT_EQ(report.iterations, 0U);
  // stop_reason should have a well-defined kind (default: Saturated).
  EXPECT_EQ(report.stop_reason.kind, StopReasonKind::Saturated);
  EXPECT_GE(report.egraph_nodes, 0U);
  EXPECT_GE(report.total_time, 0.0);
  // toString should not crash.
  EXPECT_FALSE(report.toString().empty());
}

// ===================================================================
// RunnerTest: diagnostic hardening (stop reasons, report consistency,
// rule-application visibility)
// ===================================================================

TEST(RunnerTest, SaturatedStopReasonHasCorrectKindAndLimitValue) {
  // When the runner saturates naturally, stop_reason.kind must be Saturated
  // and limit_value must remain 0 (not polluted by node/iteration bookkeeping).
  Runner<Symbol> runner;

  RecExpr<Symbol> expr;
  Id ea = expr.add(makeNode("a"));
  Id e0 = expr.add(makeNode("0"));
  expr.add(makeNode("+", {ea, e0}));
  runner.addExpr(expr);

  std::vector<Rewrite<Symbol>> rules;
  rules.push_back(makePlusZeroRule());
  runner.run(rules);

  ASSERT_TRUE(runner.has_stop_reason);
  EXPECT_EQ(runner.stop_reason.kind, StopReasonKind::Saturated);
  // For Saturated, limit_value carries no meaningful payload and stays 0.
  EXPECT_EQ(runner.stop_reason.limit_value, 0U);
  EXPECT_EQ(runner.stop_reason.elapsed_seconds, 0.0);
  EXPECT_TRUE(runner.stop_reason.message.empty());

  // The final iteration in the list must also carry Saturated.
  ASSERT_FALSE(runner.iterations.empty());
  const auto &last_iter = runner.iterations.back();
  ASSERT_TRUE(last_iter.has_stop_reason);
  EXPECT_EQ(last_iter.stop_reason.kind, StopReasonKind::Saturated);

  // Report mirrors runner state.
  Report report = runner.report();
  EXPECT_EQ(report.stop_reason.kind, StopReasonKind::Saturated);
}

TEST(RunnerTest, IterationLimitLimitValueMatchesConfig) {
  // When the runner hits the iteration limit, stop_reason.limit_value must
  // equal the configured limit, not the actual iteration count.
  const std::size_t configured_limit = 3;
  Runner<Symbol> runner;
  runner.withIterLimit(configured_limit);

  RecExpr<Symbol> expr;
  Id ea = expr.add(makeNode("a"));
  Id eb = expr.add(makeNode("b"));
  Id ec = expr.add(makeNode("c"));
  Id ed = expr.add(makeNode("d"));
  Id eab = expr.add(makeNode("+", {ea, eb}));
  Id ecd = expr.add(makeNode("+", {ec, ed}));
  expr.add(makeNode("+", {eab, ecd}));
  runner.addExpr(expr);

  std::vector<Rewrite<Symbol>> rules;
  rules.push_back(makeCommRule());
  rules.push_back(makeAssocRule());
  runner.run(rules);

  ASSERT_TRUE(runner.has_stop_reason);
  EXPECT_EQ(runner.stop_reason.kind, StopReasonKind::IterationLimit);
  // limit_value must echo back the configured limit.
  EXPECT_EQ(runner.stop_reason.limit_value, configured_limit);

  // The last iteration must also carry IterationLimit.
  ASSERT_FALSE(runner.iterations.empty());
  const auto &last_iter = runner.iterations.back();
  ASSERT_TRUE(last_iter.has_stop_reason);
  EXPECT_EQ(last_iter.stop_reason.kind, StopReasonKind::IterationLimit);
  EXPECT_EQ(last_iter.stop_reason.limit_value, configured_limit);

  // Report mirrors runner state.
  Report report = runner.report();
  EXPECT_EQ(report.stop_reason.kind, StopReasonKind::IterationLimit);
  EXPECT_EQ(report.stop_reason.limit_value, configured_limit);
}

TEST(RunnerTest, NodeLimitLimitValueIsCurrentNodeCount) {
  // When the runner hits the node limit, stop_reason.limit_value must be
  // the actual e-node count at violation (not the configured limit).
  const std::size_t node_limit = 15;
  Runner<Symbol> runner;
  runner.withIterLimit(1000).withNodeLimit(node_limit);

  RecExpr<Symbol> expr;
  Id ea = expr.add(makeNode("a"));
  Id eb = expr.add(makeNode("b"));
  Id ec = expr.add(makeNode("c"));
  Id ed = expr.add(makeNode("d"));
  Id eab = expr.add(makeNode("+", {ea, eb}));
  Id ecd = expr.add(makeNode("+", {ec, ed}));
  expr.add(makeNode("+", {eab, ecd}));
  runner.addExpr(expr);

  std::vector<Rewrite<Symbol>> rules;
  rules.push_back(makeCommRule());
  rules.push_back(makeAssocRule());
  runner.run(rules);

  ASSERT_TRUE(runner.has_stop_reason);
  EXPECT_EQ(runner.stop_reason.kind, StopReasonKind::NodeLimit);
  // limit_value carries the actual node count that triggered the violation —
  // it must be > node_limit (that's how the condition fires).
  EXPECT_GT(runner.stop_reason.limit_value, node_limit);
  // And it should match the e-graph size at that moment.
  EXPECT_EQ(runner.stop_reason.limit_value, runner.egraph.memoSize());

  // Report mirrors runner state.
  Report report = runner.report();
  EXPECT_EQ(report.stop_reason.kind, StopReasonKind::NodeLimit);
  EXPECT_GT(report.stop_reason.limit_value, node_limit);
}

TEST(RunnerTest, ReportIterationsConsistentWithIterationsVec) {
  // report().iterations must always equal runner.iterations.size().
  Runner<Symbol> runner;

  RecExpr<Symbol> expr;
  Id ea = expr.add(makeNode("a"));
  Id e0 = expr.add(makeNode("0"));
  expr.add(makeNode("+", {ea, e0}));
  runner.addExpr(expr);

  std::vector<Rewrite<Symbol>> rules;
  rules.push_back(makePlusZeroRule());
  runner.run(rules);

  Report report = runner.report();
  EXPECT_EQ(report.iterations, runner.iterations.size());
  EXPECT_EQ(report.stop_reason.kind, runner.stop_reason.kind);
  EXPECT_EQ(report.egraph_nodes, runner.egraph.memoSize());
  EXPECT_EQ(report.egraph_classes, runner.egraph.classCount());
  EXPECT_GE(report.total_time, 0.0);
  EXPECT_GE(report.search_time, 0.0);
  EXPECT_GE(report.apply_time, 0.0);
  EXPECT_GE(report.rebuild_time, 0.0);
  // Sanity: total >= sum of components (all >=0).
  EXPECT_GE(report.total_time, report.search_time + report.apply_time +
                                   report.rebuild_time - 1e-9);
}

TEST(RunnerTest, ReportTimingFieldsAreNonNegativeAfterIterLimit) {
  // After hitting IterationLimit the report timing fields must remain
  // non-negative (no underflow or uninitialised values).
  Runner<Symbol> runner;
  runner.withIterLimit(2);

  RecExpr<Symbol> expr;
  Id ea = expr.add(makeNode("a"));
  Id eb = expr.add(makeNode("b"));
  Id ec = expr.add(makeNode("c"));
  Id ed = expr.add(makeNode("d"));
  Id eab = expr.add(makeNode("+", {ea, eb}));
  Id ecd = expr.add(makeNode("+", {ec, ed}));
  expr.add(makeNode("+", {eab, ecd}));
  runner.addExpr(expr);

  std::vector<Rewrite<Symbol>> rules;
  rules.push_back(makeCommRule());
  rules.push_back(makeAssocRule());
  runner.run(rules);

  Report report = runner.report();
  EXPECT_GE(report.total_time, 0.0);
  EXPECT_GE(report.search_time, 0.0);
  EXPECT_GE(report.apply_time, 0.0);
  EXPECT_GE(report.rebuild_time, 0.0);
  EXPECT_FALSE(report.toString().empty());
}

TEST(RunnerTest, RuleApplicationVisibilityNamedRulesTraceable) {
  // Multiple named rules should appear in the applied map with distinct keys.
  // This verifies that rule naming flows through correctly into diagnostics.
  Runner<Symbol> runner;
  runner.withIterLimit(10);

  // (+ (+ 0 a) (+ b 0)) — both plus-zero and commutativity should fire.
  RecExpr<Symbol> expr;
  Id e0a = expr.add(makeNode("0"));
  Id ea = expr.add(makeNode("a"));
  Id e0b = expr.add(makeNode("0"));
  Id eb = expr.add(makeNode("b"));
  Id eleft = expr.add(makeNode("+", {e0a, ea}));   // 0 + a
  Id eright = expr.add(makeNode("+", {eb, e0b}));  // b + 0
  expr.add(makeNode("+", {eleft, eright}));
  runner.addExpr(expr);

  std::vector<Rewrite<Symbol>> rules;
  rules.push_back(makePlusZeroRule());  // "plus-zero"
  rules.push_back(makeCommRule());      // "plus-comm"
  runner.run(rules);

  ASSERT_TRUE(runner.has_stop_reason);

  // Accumulate all rule applications across all iterations.
  std::unordered_map<std::string, std::size_t> total_applied;
  for (const auto &iter : runner.iterations) {
    for (const auto &kv : iter.applied) {
      total_applied[kv.first] += kv.second;
    }
  }

  // Both rules must have fired at least once.
  EXPECT_GT(total_applied["plus-zero"], 0U)
      << "plus-zero rule never appeared in iteration.applied";
  EXPECT_GT(total_applied["plus-comm"], 0U)
      << "plus-comm rule never appeared in iteration.applied";
}

TEST(RunnerTest, RuleApplicationVisibilitySingleRuleCountsPositive) {
  // When a rule fires, the merge count stored in applied[name] must be > 0.
  // A merge count of 0 would indicate the entry should not appear.
  Runner<Symbol> runner;

  RecExpr<Symbol> expr;
  Id ea = expr.add(makeNode("a"));
  Id e0 = expr.add(makeNode("0"));
  expr.add(makeNode("+", {ea, e0}));
  runner.addExpr(expr);

  std::vector<Rewrite<Symbol>> rules;
  rules.push_back(makePlusZeroRule());
  runner.run(rules);

  bool found_positive_merge = false;
  for (const auto &iter : runner.iterations) {
    auto it = iter.applied.find("plus-zero");
    if (it != iter.applied.end()) {
      EXPECT_GT(it->second, 0U)
          << "applied[\"plus-zero\"] entry has zero merge count";
      found_positive_merge = true;
    }
  }
  EXPECT_TRUE(found_positive_merge)
      << "plus-zero rule was never recorded in any iteration's applied map";
}

TEST(RunnerTest, StopReasonKindMatchesRunnerAndReport) {
  // For each possible stop reason, verify that runner.stop_reason.kind,
  // the last iteration's stop_reason.kind, and report().stop_reason.kind
  // all agree.
  //
  // Case: NodeLimit
  {
    Runner<Symbol> runner;
    runner.withIterLimit(1000).withNodeLimit(10);

    RecExpr<Symbol> expr;
    Id ea = expr.add(makeNode("a"));
    Id eb = expr.add(makeNode("b"));
    Id ec = expr.add(makeNode("c"));
    Id ed = expr.add(makeNode("d"));
    Id eab = expr.add(makeNode("+", {ea, eb}));
    Id ecd = expr.add(makeNode("+", {ec, ed}));
    expr.add(makeNode("+", {eab, ecd}));
    runner.addExpr(expr);

    std::vector<Rewrite<Symbol>> rules;
    rules.push_back(makeCommRule());
    rules.push_back(makeAssocRule());
    runner.run(rules);

    ASSERT_TRUE(runner.has_stop_reason);
    StopReasonKind expected = runner.stop_reason.kind;

    const auto &last_iter = runner.iterations.back();
    ASSERT_TRUE(last_iter.has_stop_reason);
    EXPECT_EQ(last_iter.stop_reason.kind, expected);

    Report report = runner.report();
    EXPECT_EQ(report.stop_reason.kind, expected);
  }

  // Case: IterationLimit
  {
    Runner<Symbol> runner;
    runner.withIterLimit(2);

    RecExpr<Symbol> expr;
    Id ea = expr.add(makeNode("a"));
    Id eb = expr.add(makeNode("b"));
    Id ec = expr.add(makeNode("c"));
    Id ed = expr.add(makeNode("d"));
    Id eab = expr.add(makeNode("+", {ea, eb}));
    Id ecd = expr.add(makeNode("+", {ec, ed}));
    expr.add(makeNode("+", {eab, ecd}));
    runner.addExpr(expr);

    std::vector<Rewrite<Symbol>> rules;
    rules.push_back(makeCommRule());
    rules.push_back(makeAssocRule());
    runner.run(rules);

    ASSERT_TRUE(runner.has_stop_reason);
    StopReasonKind expected = runner.stop_reason.kind;

    const auto &last_iter = runner.iterations.back();
    ASSERT_TRUE(last_iter.has_stop_reason);
    EXPECT_EQ(last_iter.stop_reason.kind, expected);

    Report report = runner.report();
    EXPECT_EQ(report.stop_reason.kind, expected);
  }
}

TEST(RunnerTest, ReportToStringContainsAllFields) {
  // report().toString() must include all key labels so log output is
  // actionable without reading the source code.
  Runner<Symbol> runner;

  RecExpr<Symbol> expr;
  Id ea = expr.add(makeNode("a"));
  Id e0 = expr.add(makeNode("0"));
  expr.add(makeNode("+", {ea, e0}));
  runner.addExpr(expr);

  std::vector<Rewrite<Symbol>> rules;
  rules.push_back(makePlusZeroRule());
  runner.run(rules);

  std::string s = runner.report().toString();
  EXPECT_NE(s.find("Stop reason"), std::string::npos)
      << "missing 'Stop reason' in: " << s;
  EXPECT_NE(s.find("Iterations"), std::string::npos)
      << "missing 'Iterations' in: " << s;
  EXPECT_NE(s.find("Egraph nodes"), std::string::npos)
      << "missing 'Egraph nodes' in: " << s;
  EXPECT_NE(s.find("Egraph classes"), std::string::npos)
      << "missing 'Egraph classes' in: " << s;
  EXPECT_NE(s.find("Total time"), std::string::npos)
      << "missing 'Total time' in: " << s;
  EXPECT_NE(s.find("Search time"), std::string::npos)
      << "missing 'Search time' in: " << s;
  EXPECT_NE(s.find("Apply time"), std::string::npos)
      << "missing 'Apply time' in: " << s;
  EXPECT_NE(s.find("Rebuild time"), std::string::npos)
      << "missing 'Rebuild time' in: " << s;
}

// ===================================================================
// MultiPattern tests
// ===================================================================

TEST(MultiPatternTest, EmptyPatternsRejected) {
  EXPECT_THROW(MultiPattern<Symbol>(std::vector<Pattern<Symbol>>{}),
               std::invalid_argument);
}

TEST(MultiPatternTest, SinglePatternDelegatesToPattern) {
  // A MultiPattern with one pattern should behave like a regular Pattern.
  // Graph: a, b, (+ a b)
  EGraph<Symbol> graph;
  Id a = graph.add(makeNode("a"));
  Id b = graph.add(makeNode("b"));
  graph.add(makeNode("+", {a, b}));

  // Pattern: (+ ?x ?y)
  PatternAst<Symbol> past;
  Id px = past.add(makePatVar("?x"));
  Id py = past.add(makePatVar("?y"));
  past.add(makePatENode("+", {px, py}));

  std::vector<Pattern<Symbol>> pats;
  pats.push_back(Pattern<Symbol>(std::move(past)));

  MultiPattern<Symbol> mp(std::move(pats));
  EXPECT_EQ(mp.size(), 1U);
  EXPECT_EQ(mp.vars().size(), 2U);

  auto matches = mp.search(graph);
  EXPECT_EQ(matches.size(), 1U);
  EXPECT_EQ(matches[0].substs.size(), 1U);
}

TEST(MultiPatternTest, TwoPatternsSharedVariables) {
  // Graph: a, b, (+ a b), (* a b)
  // MultiPattern:
  //   Pattern 0: (+ ?x ?y)
  //   Pattern 1: (* ?x ?y)
  // Both share ?x and ?y. Should find a match where ?x=a, ?y=b.
  EGraph<Symbol> graph;
  Id a = graph.add(makeNode("a"));
  Id b = graph.add(makeNode("b"));
  graph.add(makeNode("+", {a, b}));
  graph.add(makeNode("*", {a, b}));

  // Pattern 0: (+ ?x ?y)
  PatternAst<Symbol> p0_ast;
  Id p0x = p0_ast.add(makePatVar("?x"));
  Id p0y = p0_ast.add(makePatVar("?y"));
  p0_ast.add(makePatENode("+", {p0x, p0y}));

  // Pattern 1: (* ?x ?y)
  PatternAst<Symbol> p1_ast;
  Id p1x = p1_ast.add(makePatVar("?x"));
  Id p1y = p1_ast.add(makePatVar("?y"));
  p1_ast.add(makePatENode("*", {p1x, p1y}));

  std::vector<Pattern<Symbol>> pats;
  pats.push_back(Pattern<Symbol>(std::move(p0_ast)));
  pats.push_back(Pattern<Symbol>(std::move(p1_ast)));

  MultiPattern<Symbol> mp(std::move(pats));
  EXPECT_EQ(mp.vars().size(), 2U);

  auto matches = mp.search(graph);
  // Should find exactly 1 result (anchored at the "+" class).
  EXPECT_EQ(matches.size(), 1U);

  // The substitution should bind ?x->a and ?y->b.
  ASSERT_EQ(matches[0].substs.size(), 1U);
  const auto &subst = matches[0].substs[0];
  EXPECT_EQ(graph.find(*subst.get(Var::fromString("?x"))), graph.find(a));
  EXPECT_EQ(graph.find(*subst.get(Var::fromString("?y"))), graph.find(b));
}

TEST(MultiPatternTest, NoMatchWhenSecondPatternFails) {
  // Graph: a, b, (+ a b) -- no (*) node.
  // MultiPattern:
  //   Pattern 0: (+ ?x ?y)
  //   Pattern 1: (* ?x ?y)
  // Pattern 1 has no match, so the multi-pattern should find nothing.
  EGraph<Symbol> graph;
  Id a = graph.add(makeNode("a"));
  Id b = graph.add(makeNode("b"));
  graph.add(makeNode("+", {a, b}));

  PatternAst<Symbol> p0_ast;
  Id p0x = p0_ast.add(makePatVar("?x"));
  Id p0y = p0_ast.add(makePatVar("?y"));
  p0_ast.add(makePatENode("+", {p0x, p0y}));

  PatternAst<Symbol> p1_ast;
  Id p1x = p1_ast.add(makePatVar("?x"));
  Id p1y = p1_ast.add(makePatVar("?y"));
  p1_ast.add(makePatENode("*", {p1x, p1y}));

  std::vector<Pattern<Symbol>> pats;
  pats.push_back(Pattern<Symbol>(std::move(p0_ast)));
  pats.push_back(Pattern<Symbol>(std::move(p1_ast)));

  MultiPattern<Symbol> mp(std::move(pats));
  auto matches = mp.search(graph);

  // No multi-match should exist.
  std::size_t total_substs = 0;
  for (const auto &sm : matches) {
    total_substs += sm.substs.size();
  }
  EXPECT_EQ(total_substs, 0U);
}

TEST(MultiPatternTest, SharedVariablesConstrainBindings) {
  // Graph: a, b, c, (+ a b), (* a c)
  // MultiPattern:
  //   Pattern 0: (+ ?x ?y)
  //   Pattern 1: (* ?x ?y)
  // Pattern 0 binds ?x=a, ?y=b.
  // Pattern 1 must also bind ?x=a, ?y=b.
  // But (* a c) has ?y=c, not b, so no consistent match.
  EGraph<Symbol> graph;
  Id a = graph.add(makeNode("a"));
  Id b = graph.add(makeNode("b"));
  Id c = graph.add(makeNode("c"));
  graph.add(makeNode("+", {a, b}));
  graph.add(makeNode("*", {a, c}));

  PatternAst<Symbol> p0_ast;
  Id p0x = p0_ast.add(makePatVar("?x"));
  Id p0y = p0_ast.add(makePatVar("?y"));
  p0_ast.add(makePatENode("+", {p0x, p0y}));

  PatternAst<Symbol> p1_ast;
  Id p1x = p1_ast.add(makePatVar("?x"));
  Id p1y = p1_ast.add(makePatVar("?y"));
  p1_ast.add(makePatENode("*", {p1x, p1y}));

  std::vector<Pattern<Symbol>> pats;
  pats.push_back(Pattern<Symbol>(std::move(p0_ast)));
  pats.push_back(Pattern<Symbol>(std::move(p1_ast)));

  MultiPattern<Symbol> mp(std::move(pats));
  auto matches = mp.search(graph);

  std::size_t total_substs = 0;
  for (const auto &sm : matches) {
    total_substs += sm.substs.size();
  }
  EXPECT_EQ(total_substs, 0U);
}

TEST(MultiPatternTest, MatchAfterMerge) {
  // Graph: a, b, c, (+ a b), (* a c)
  // After merging b and c:
  //   Pattern 0: (+ ?x ?y) -> ?x=a, ?y=b=c
  //   Pattern 1: (* ?x ?y) -> ?x=a, ?y=c=b
  // Now both match consistently.
  EGraph<Symbol> graph;
  Id a = graph.add(makeNode("a"));
  Id b = graph.add(makeNode("b"));
  Id c = graph.add(makeNode("c"));
  graph.add(makeNode("+", {a, b}));
  graph.add(makeNode("*", {a, c}));

  graph.merge(b, c);
  graph.rebuild();

  PatternAst<Symbol> p0_ast;
  Id p0x = p0_ast.add(makePatVar("?x"));
  Id p0y = p0_ast.add(makePatVar("?y"));
  p0_ast.add(makePatENode("+", {p0x, p0y}));

  PatternAst<Symbol> p1_ast;
  Id p1x = p1_ast.add(makePatVar("?x"));
  Id p1y = p1_ast.add(makePatVar("?y"));
  p1_ast.add(makePatENode("*", {p1x, p1y}));

  std::vector<Pattern<Symbol>> pats;
  pats.push_back(Pattern<Symbol>(std::move(p0_ast)));
  pats.push_back(Pattern<Symbol>(std::move(p1_ast)));

  MultiPattern<Symbol> mp(std::move(pats));
  auto matches = mp.search(graph);

  std::size_t total_substs = 0;
  for (const auto &sm : matches) {
    total_substs += sm.substs.size();
  }
  EXPECT_GE(total_substs, 1U);
}

TEST(MultiPatternTest, ThreePatterns) {
  // Graph: a, b, (+ a b), (* a b), (- a b)
  // MultiPattern with 3 patterns sharing ?x, ?y.
  EGraph<Symbol> graph;
  Id a = graph.add(makeNode("a"));
  Id b = graph.add(makeNode("b"));
  graph.add(makeNode("+", {a, b}));
  graph.add(makeNode("*", {a, b}));
  graph.add(makeNode("-", {a, b}));

  PatternAst<Symbol> p0_ast;
  Id p0x = p0_ast.add(makePatVar("?x"));
  Id p0y = p0_ast.add(makePatVar("?y"));
  p0_ast.add(makePatENode("+", {p0x, p0y}));

  PatternAst<Symbol> p1_ast;
  Id p1x = p1_ast.add(makePatVar("?x"));
  Id p1y = p1_ast.add(makePatVar("?y"));
  p1_ast.add(makePatENode("*", {p1x, p1y}));

  PatternAst<Symbol> p2_ast;
  Id p2x = p2_ast.add(makePatVar("?x"));
  Id p2y = p2_ast.add(makePatVar("?y"));
  p2_ast.add(makePatENode("-", {p2x, p2y}));

  std::vector<Pattern<Symbol>> pats;
  pats.push_back(Pattern<Symbol>(std::move(p0_ast)));
  pats.push_back(Pattern<Symbol>(std::move(p1_ast)));
  pats.push_back(Pattern<Symbol>(std::move(p2_ast)));

  MultiPattern<Symbol> mp(std::move(pats));
  EXPECT_EQ(mp.size(), 3U);

  auto matches = mp.search(graph);

  std::size_t total_substs = 0;
  for (const auto &sm : matches) {
    total_substs += sm.substs.size();
  }
  EXPECT_GE(total_substs, 1U);
}

TEST(MultiPatternTest, DisjointVariables) {
  // Patterns can have non-overlapping variable sets.
  // Pattern 0: (f ?x)
  // Pattern 1: (g ?y)
  // The multi-pattern finds substitutions where both exist somewhere.
  EGraph<Symbol> graph;
  Id a = graph.add(makeNode("a"));
  Id b = graph.add(makeNode("b"));
  graph.add(makeNode("f", {a}));
  graph.add(makeNode("g", {b}));

  PatternAst<Symbol> p0_ast;
  Id p0x = p0_ast.add(makePatVar("?x"));
  p0_ast.add(makePatENode("f", {p0x}));

  PatternAst<Symbol> p1_ast;
  Id p1y = p1_ast.add(makePatVar("?y"));
  p1_ast.add(makePatENode("g", {p1y}));

  std::vector<Pattern<Symbol>> pats;
  pats.push_back(Pattern<Symbol>(std::move(p0_ast)));
  pats.push_back(Pattern<Symbol>(std::move(p1_ast)));

  MultiPattern<Symbol> mp(std::move(pats));
  EXPECT_EQ(mp.vars().size(), 2U);

  auto matches = mp.search(graph);

  std::size_t total_substs = 0;
  for (const auto &sm : matches) {
    total_substs += sm.substs.size();
  }
  // With disjoint variables, the cross-product should produce matches.
  EXPECT_GE(total_substs, 1U);
}

TEST(MultiPatternTest, SearchEclassMethod) {
  // Test the searchEclass method directly.
  EGraph<Symbol> graph;
  Id a = graph.add(makeNode("a"));
  Id b = graph.add(makeNode("b"));
  Id plus = graph.add(makeNode("+", {a, b}));
  graph.add(makeNode("*", {a, b}));

  PatternAst<Symbol> p0_ast;
  Id p0x = p0_ast.add(makePatVar("?x"));
  Id p0y = p0_ast.add(makePatVar("?y"));
  p0_ast.add(makePatENode("+", {p0x, p0y}));

  PatternAst<Symbol> p1_ast;
  Id p1x = p1_ast.add(makePatVar("?x"));
  Id p1y = p1_ast.add(makePatVar("?y"));
  p1_ast.add(makePatENode("*", {p1x, p1y}));

  std::vector<Pattern<Symbol>> pats;
  pats.push_back(Pattern<Symbol>(std::move(p0_ast)));
  pats.push_back(Pattern<Symbol>(std::move(p1_ast)));

  MultiPattern<Symbol> mp(std::move(pats));

  // Search anchored at the "+" class should succeed.
  auto sm = mp.searchEclass(graph, plus);
  EXPECT_EQ(sm.substs.size(), 1U);

  // Search anchored at the "a" class should fail (no "+" node there).
  auto sm2 = mp.searchEclass(graph, a);
  EXPECT_TRUE(sm2.substs.empty());
}

TEST(MultiPatternTest, MultiPatternSearcherInRewrite) {
  // Use MultiPatternSearcher through a Rewrite with custom applier.
  // Rule: if (+ ?x ?y) AND (* ?x ?y) both exist, merge them.
  EGraph<Symbol> graph;
  Id a = graph.add(makeNode("a"));
  Id b = graph.add(makeNode("b"));
  Id plus = graph.add(makeNode("+", {a, b}));
  Id mult = graph.add(makeNode("*", {a, b}));

  // Before: + and * are in different classes.
  EXPECT_NE(graph.find(plus), graph.find(mult));

  // Build multi-pattern searcher.
  PatternAst<Symbol> p0_ast;
  Id p0x = p0_ast.add(makePatVar("?x"));
  Id p0y = p0_ast.add(makePatVar("?y"));
  p0_ast.add(makePatENode("+", {p0x, p0y}));

  PatternAst<Symbol> p1_ast;
  Id p1x = p1_ast.add(makePatVar("?x"));
  Id p1y = p1_ast.add(makePatVar("?y"));
  p1_ast.add(makePatENode("*", {p1x, p1y}));

  std::vector<Pattern<Symbol>> pats;
  pats.push_back(Pattern<Symbol>(std::move(p0_ast)));
  pats.push_back(Pattern<Symbol>(std::move(p1_ast)));

  auto searcher = std::make_shared<MultiPatternSearcher<Symbol>>(
      MultiPattern<Symbol>(std::move(pats)));

  // Build applier: merge the matched (+ ?x ?y) class with (* ?x ?y).
  class MergeMultApplier : public Applier<Symbol> {
   public:
    std::vector<Id> applyOne(EGraph<Symbol> &egraph, Id eclass,
                             const Subst &subst) const override {
      Id x = egraph.find(subst.at(Var::fromString("?x")));
      Id y = egraph.find(subst.at(Var::fromString("?y")));

      ENode<Symbol> mult_node;
      mult_node.op = Symbol{"*"};
      mult_node.children = {x, y};
      Id mult_id = egraph.add(mult_node);

      Id plus_id = egraph.find(eclass);
      if (egraph.find(mult_id) != plus_id) {
        egraph.merge(plus_id, mult_id);
        return {egraph.find(plus_id)};
      }
      return {};
    }
  };

  auto applier = std::make_shared<MergeMultApplier>();

  Rewrite<Symbol> rule("merge-plus-mult",
                       std::shared_ptr<Searcher<Symbol>>(searcher),
                       std::shared_ptr<Applier<Symbol>>(applier));

  auto merges = rule.run(graph);
  graph.rebuild();

  EXPECT_GE(merges, 1U);
  EXPECT_EQ(graph.find(plus), graph.find(mult));
}

TEST(MultiPatternTest, VarsUnionIsCorrect) {
  // Pattern 0 has ?a, ?b; Pattern 1 has ?b, ?c.
  // Union should be ?a, ?b, ?c.
  PatternAst<Symbol> p0_ast;
  Id p0a = p0_ast.add(makePatVar("?a"));
  Id p0b = p0_ast.add(makePatVar("?b"));
  p0_ast.add(makePatENode("+", {p0a, p0b}));

  PatternAst<Symbol> p1_ast;
  Id p1b = p1_ast.add(makePatVar("?b"));
  Id p1c = p1_ast.add(makePatVar("?c"));
  p1_ast.add(makePatENode("*", {p1b, p1c}));

  std::vector<Pattern<Symbol>> pats;
  pats.push_back(Pattern<Symbol>(std::move(p0_ast)));
  pats.push_back(Pattern<Symbol>(std::move(p1_ast)));

  MultiPattern<Symbol> mp(std::move(pats));
  EXPECT_EQ(mp.vars().size(), 3U);

  // Check that all three vars are present.
  auto vars = mp.vars();
  auto has = [&vars](const std::string &name) {
    Var v = Var::fromString(name);
    for (const auto &var : vars) {
      if (var == v) return true;
    }
    return false;
  };
  EXPECT_TRUE(has("?a"));
  EXPECT_TRUE(has("?b"));
  EXPECT_TRUE(has("?c"));
}

TEST(MultiPatternTest, MultiPatternSearcherViaRunner) {
  // End-to-end test: use MultiPatternSearcher in a Runner.
  Runner<Symbol> runner;
  runner.withIterLimit(10);

  // Build expression: (+ a b) and (* a b)
  RecExpr<Symbol> expr1;
  Id ea1 = expr1.add(makeNode("a"));
  Id eb1 = expr1.add(makeNode("b"));
  expr1.add(makeNode("+", {ea1, eb1}));
  Id root1 = runner.addExpr(expr1);

  RecExpr<Symbol> expr2;
  Id ea2 = expr2.add(makeNode("a"));
  Id eb2 = expr2.add(makeNode("b"));
  expr2.add(makeNode("*", {ea2, eb2}));
  runner.addExpr(expr2);

  // Multi-pattern rule: if (+ ?x ?y) and (* ?x ?y) exist, add "combined".
  PatternAst<Symbol> p0_ast;
  Id p0x = p0_ast.add(makePatVar("?x"));
  Id p0y = p0_ast.add(makePatVar("?y"));
  p0_ast.add(makePatENode("+", {p0x, p0y}));

  PatternAst<Symbol> p1_ast;
  Id p1x = p1_ast.add(makePatVar("?x"));
  Id p1y = p1_ast.add(makePatVar("?y"));
  p1_ast.add(makePatENode("*", {p1x, p1y}));

  std::vector<Pattern<Symbol>> pats;
  pats.push_back(Pattern<Symbol>(std::move(p0_ast)));
  pats.push_back(Pattern<Symbol>(std::move(p1_ast)));

  auto searcher = std::make_shared<MultiPatternSearcher<Symbol>>(
      MultiPattern<Symbol>(std::move(pats)));

  // Applier: add a "combined" node and merge it with the "+" class.
  class CombinedApplier : public Applier<Symbol> {
   public:
    std::vector<Id> applyOne(EGraph<Symbol> &egraph, Id eclass,
                             const Subst & /*subst*/) const override {
      Id canonical = egraph.find(eclass);
      ENode<Symbol> node;
      node.op = Symbol{"combined"};
      Id new_id = egraph.add(node);
      if (egraph.find(new_id) != canonical) {
        egraph.merge(canonical, new_id);
        return {egraph.find(canonical)};
      }
      return {};
    }
  };

  auto applier = std::make_shared<CombinedApplier>();

  Rewrite<Symbol> rule("combine-plus-mult",
                       std::shared_ptr<Searcher<Symbol>>(searcher),
                       std::shared_ptr<Applier<Symbol>>(applier));

  std::vector<Rewrite<Symbol>> rules;
  rules.push_back(std::move(rule));
  runner.run(rules);

  ASSERT_TRUE(runner.has_stop_reason);
  EXPECT_EQ(runner.stop_reason.kind, StopReasonKind::Saturated);

  // "combined" should be in the same class as the "+" expression.
  const auto *klass = runner.egraph.getEClass(root1);
  ASSERT_NE(klass, nullptr);
  bool found_combined = false;
  for (const auto &node : klass->nodes) {
    if (node.op.value == "combined") {
      found_combined = true;
      break;
    }
  }
  EXPECT_TRUE(found_combined);
}

// ===================================================================
// Explanation tests
// ===================================================================

TEST(ExplanationTest, EnableExplanationsAfterAddingThrows) {
  EGraph<Symbol> graph;
  graph.add(makeNode("a"));

  // Enabling explanations after adding nodes should throw.
  EXPECT_THROW(graph.withExplanationsEnabled(), std::logic_error);
}

TEST(ExplanationTest, ExplainWithoutEnablingThrows) {
  EGraph<Symbol> graph;
  Id a = graph.add(makeNode("a"));

  // Calling explain() without enabling should throw.
  EXPECT_THROW(graph.explain(), std::logic_error);

  // The free function should also throw.
  EXPECT_THROW(explainIdEquivalence(graph, a, a), std::logic_error);
}

TEST(ExplanationTest, TrivialSameNodeExplanation) {
  EGraph<Symbol> graph;
  graph.withExplanationsEnabled();

  Id a = graph.add(makeNode("a"));

  // Explaining equivalence of a node with itself should produce a trivial
  // explanation (0 or 1 steps).
  auto expl = explainIdEquivalence(graph, a, a);
  // The tree should contain just the single starting node (no rewrite steps).
  EXPECT_LE(expl.treeSize(), 1U);
}

TEST(ExplanationTest, DirectMergeWithNamedRule) {
  EGraph<Symbol> graph;
  graph.withExplanationsEnabled();

  Id a = graph.add(makeNode("a"));
  Id b = graph.add(makeNode("b"));

  // Merge with a named rule.
  graph.unionTrusted(a, b, "my-rule");
  graph.rebuild();

  auto expl = explainIdEquivalence(graph, a, b);

  // Should have at least 2 tree steps: the start node + one rewrite step.
  EXPECT_GE(expl.treeSize(), 2U);

  // Check that the rule name appears in the proof.
  const auto &trees = expl.trees();
  bool found_rule = false;
  for (const auto &step : trees) {
    if (step->forward_rule == "my-rule" || step->backward_rule == "my-rule") {
      found_rule = true;
      break;
    }
  }
  EXPECT_TRUE(found_rule);
}

TEST(ExplanationTest, CongruenceMergeExplanation) {
  EGraph<Symbol> graph;
  graph.withExplanationsEnabled();

  Id a = graph.add(makeNode("a"));
  Id b = graph.add(makeNode("b"));
  Id fa = graph.add(makeNode("f", {a}));
  Id fb = graph.add(makeNode("f", {b}));

  // Merge a and b; after rebuild, f(a) and f(b) become congruent.
  graph.unionTrusted(a, b, "base-eq");
  graph.rebuild();

  EXPECT_EQ(graph.find(fa), graph.find(fb));

  // Explain why f(a) == f(b).
  auto expl = explainIdEquivalence(graph, fa, fb);

  // The proof should have at least 2 steps.
  EXPECT_GE(expl.treeSize(), 2U);

  // There should be a congruence step (no rule name, but child proofs).
  const auto &trees = expl.trees();
  bool found_congruence = false;
  for (const auto &step : trees) {
    if (!step->hasForwardRule() && !step->hasBackwardRule() &&
        !step->child_proofs.empty()) {
      found_congruence = true;
      break;
    }
  }
  EXPECT_TRUE(found_congruence);
}

TEST(ExplanationTest, ExplainAfterRewriteRule) {
  // Use the plus-zero rule and explain the resulting equivalence.
  EGraph<Symbol> graph;
  graph.withExplanationsEnabled();

  Id a = graph.add(makeNode("a"));
  Id zero = graph.add(makeNode("0"));
  Id plus = graph.add(makeNode("+", {a, zero}));

  // Apply the rewrite: (+ ?a 0) => ?a
  PatternAst<Symbol> lhs_ast;
  Id la = lhs_ast.add(makePatVar("?a"));
  Id l0 = lhs_ast.add(makePatENode("0"));
  lhs_ast.add(makePatENode("+", {la, l0}));

  PatternAst<Symbol> rhs_ast;
  rhs_ast.add(makePatVar("?a"));

  Rewrite<Symbol> rule("plus-zero", Pattern<Symbol>(std::move(lhs_ast)),
                       Pattern<Symbol>(std::move(rhs_ast)));

  rule.run(graph);
  graph.rebuild();

  EXPECT_EQ(graph.find(plus), graph.find(a));

  // Explain why (+ a 0) == a.
  auto expl = explainIdEquivalence(graph, plus, a);
  EXPECT_GE(expl.treeSize(), 2U);
}

TEST(ExplanationTest, ProofDirectionIsRecorded) {
  EGraph<Symbol> graph;
  graph.withExplanationsEnabled();

  Id a = graph.add(makeNode("a"));
  Id b = graph.add(makeNode("b"));

  graph.unionTrusted(a, b, "fwd-rule");
  graph.rebuild();

  // Explain a -> b: should have forward_rule set.
  auto expl_ab = explainIdEquivalence(graph, a, b);
  const auto &trees_ab = expl_ab.trees();

  bool has_forward = false;
  bool has_backward = false;
  for (const auto &step : trees_ab) {
    if (step->hasForwardRule()) has_forward = true;
    if (step->hasBackwardRule()) has_backward = true;
  }
  // At least one direction should be present.
  EXPECT_TRUE(has_forward || has_backward);

  // Explain b -> a: direction should be reversed.
  auto expl_ba = explainIdEquivalence(graph, b, a);
  const auto &trees_ba = expl_ba.trees();

  bool ba_has_forward = false;
  bool ba_has_backward = false;
  for (const auto &step : trees_ba) {
    if (step->hasForwardRule()) ba_has_forward = true;
    if (step->hasBackwardRule()) ba_has_backward = true;
  }
  EXPECT_TRUE(ba_has_forward || ba_has_backward);
}

TEST(ExplanationTest, FlatExplanationConversion) {
  EGraph<Symbol> graph;
  graph.withExplanationsEnabled();

  Id a = graph.add(makeNode("a"));
  Id b = graph.add(makeNode("b"));

  graph.unionTrusted(a, b, "eq-rule");
  graph.rebuild();

  auto expl = explainIdEquivalence(graph, a, b);

  // Access the flat explanation (lazy conversion from tree form).
  const auto &flat = expl.flatExplanation();
  EXPECT_GE(flat.size(), 2U);

  // Each FlatTerm should have a valid node.
  for (const auto &ft : flat) {
    // The node op should be non-empty (all our test nodes have string names).
    EXPECT_FALSE(ft.node.op.value.empty());
  }
}

TEST(ExplanationTest, FlatTermGetRecExpr) {
  // Build a FlatTerm for f(a) and verify getRecExpr() produces a valid
  // RecExpr.
  EGraph<Symbol> graph;
  graph.withExplanationsEnabled();

  Id a = graph.add(makeNode("a"));
  Id fa = graph.add(makeNode("f", {a}));
  Id b = graph.add(makeNode("b"));
  Id fb = graph.add(makeNode("f", {b}));

  graph.unionTrusted(a, b, "ab-rule");
  graph.rebuild();

  EXPECT_EQ(graph.find(fa), graph.find(fb));

  auto expl = explainIdEquivalence(graph, fa, fb);
  const auto &flat = expl.flatExplanation();

  // Convert each flat term to a RecExpr and verify it's valid.
  for (const auto &ft : flat) {
    auto rec = ft.getRecExpr();
    EXPECT_GE(rec.size(), 1U);
  }
}

TEST(ExplanationTest, NonEquivalentIdsThrow) {
  EGraph<Symbol> graph;
  graph.withExplanationsEnabled();

  Id a = graph.add(makeNode("a"));
  Id b = graph.add(makeNode("b"));

  // a and b are not equivalent — should throw.
  EXPECT_THROW(explainIdEquivalence(graph, a, b), std::invalid_argument);
}

TEST(ExplanationTest, ExplainEquivalenceViaRecExpr) {
  EGraph<Symbol> graph;
  graph.withExplanationsEnabled();

  // Build and add expression: a
  RecExpr<Symbol> expr_a;
  expr_a.add(makeNode("a"));
  Id a_id = graph.addExpr(expr_a);

  // Build and add expression: b
  RecExpr<Symbol> expr_b;
  expr_b.add(makeNode("b"));
  Id b_id = graph.addExpr(expr_b);

  graph.unionTrusted(a_id, b_id, "ab-equiv");
  graph.rebuild();

  // Use the RecExpr-based free function.
  auto expl = explainEquivalence(graph, expr_a, expr_b);
  EXPECT_GE(expl.treeSize(), 2U);
}

TEST(ExplanationTest, AlternateRewriteForAlreadyEqual) {
  // When two nodes are already equal and we merge them again with a
  // different rule, an alternate rewrite (shortcut edge) should be recorded.
  EGraph<Symbol> graph;
  graph.withExplanationsEnabled();

  Id a = graph.add(makeNode("a"));
  Id b = graph.add(makeNode("b"));

  graph.unionTrusted(a, b, "rule-1");
  graph.rebuild();

  // Merge again with a different rule (they're already equivalent).
  graph.unionTrusted(a, b, "rule-2");
  graph.rebuild();

  // Should still be able to explain the equivalence.
  auto expl = explainIdEquivalence(graph, a, b);
  EXPECT_GE(expl.treeSize(), 2U);
}

TEST(ExplanationTest, MultiStepTransitiveProof) {
  // a == b == c via two separate rules.
  // Explaining a == c requires a multi-step transitive proof.
  EGraph<Symbol> graph;
  graph.withExplanationsEnabled();

  Id a = graph.add(makeNode("a"));
  Id b = graph.add(makeNode("b"));
  Id c = graph.add(makeNode("c"));

  graph.unionTrusted(a, b, "step-1");
  graph.rebuild();
  graph.unionTrusted(b, c, "step-2");
  graph.rebuild();

  EXPECT_EQ(graph.find(a), graph.find(c));

  auto expl = explainIdEquivalence(graph, a, c);
  // Should have at least 3 steps: a, b, c.
  EXPECT_GE(expl.treeSize(), 3U);

  // Both rule names should appear somewhere in the proof.
  const auto &trees = expl.trees();
  bool found_step1 = false;
  bool found_step2 = false;
  for (const auto &step : trees) {
    if (step->forward_rule == "step-1" || step->backward_rule == "step-1") {
      found_step1 = true;
    }
    if (step->forward_rule == "step-2" || step->backward_rule == "step-2") {
      found_step2 = true;
    }
  }
  EXPECT_TRUE(found_step1);
  EXPECT_TRUE(found_step2);
}

TEST(ExplanationTest, ExplanationsEnabledAccessor) {
  EGraph<Symbol> graph1;
  EXPECT_FALSE(graph1.explanationsEnabled());

  EGraph<Symbol> graph2;
  graph2.withExplanationsEnabled();
  EXPECT_TRUE(graph2.explanationsEnabled());
}

TEST(ExplanationTest, NodeStorageGrowsWithAdds) {
  EGraph<Symbol> graph;
  graph.withExplanationsEnabled();

  EXPECT_TRUE(graph.nodeStorage().empty());

  Id a = graph.add(makeNode("a"));
  (void)a;
  EXPECT_GE(graph.nodeStorage().size(), 1U);

  Id b = graph.add(makeNode("b"));
  (void)b;
  EXPECT_GE(graph.nodeStorage().size(), 2U);
}

TEST(ExplanationTest, DuplicateAddCreatesNewIdWhenExplaining) {
  // With explanations enabled, adding the same node twice should still
  // return the same canonical class, but internally create a new Id.
  EGraph<Symbol> graph;
  graph.withExplanationsEnabled();

  Id a1 = graph.add(makeNode("a"));
  std::size_t nodes_before = graph.nodeStorage().size();

  Id a2 = graph.add(makeNode("a"));
  std::size_t nodes_after = graph.nodeStorage().size();

  // Both return the same canonical class.
  EXPECT_EQ(graph.find(a1), graph.find(a2));

  // But a new node was allocated internally for the proof forest.
  EXPECT_GT(nodes_after, nodes_before);
}

TEST(ExplanationTest, ExistingTestsStillPassWithExplanations) {
  // Verify that enabling explanations doesn't break basic e-graph
  // operations: add, merge, rebuild, congruence.
  EGraph<Symbol> graph;
  graph.withExplanationsEnabled();

  Id x = graph.add(makeNode("x"));
  Id y = graph.add(makeNode("y"));
  Id fx = graph.add(makeNode("f", {x}));
  Id fy = graph.add(makeNode("f", {y}));

  EXPECT_NE(graph.find(fx), graph.find(fy));

  graph.unionTrusted(x, y, "xy-eq");
  graph.rebuild();

  EXPECT_EQ(graph.find(x), graph.find(y));
  EXPECT_EQ(graph.find(fx), graph.find(fy));
  EXPECT_EQ(graph.classCount(), 2U);
}

// ===================================================================
// Additional hardening tests for explanation usability (Task 4)
// ===================================================================

// Direct merge: rule name is present in the proof in at least one direction.
TEST(ExplanationTest, DirectMergeRuleNamePresentInProof) {
  EGraph<Symbol> graph;
  graph.withExplanationsEnabled();

  Id a = graph.add(makeNode("a"));
  Id b = graph.add(makeNode("b"));

  graph.unionTrusted(a, b, "my-fwd-rule");
  graph.rebuild();

  auto expl = explainIdEquivalence(graph, a, b);
  EXPECT_GE(expl.treeSize(), 2U);

  const auto &trees = expl.trees();
  bool found = false;
  for (const auto &step : trees) {
    if (step->forward_rule == "my-fwd-rule" ||
        step->backward_rule == "my-fwd-rule") {
      found = true;
      break;
    }
  }
  EXPECT_TRUE(found) << "Rule name must appear in the proof";
}

// Direct merge: explaining in the reverse direction (b->a) also produces
// a proof with the rule name.
TEST(ExplanationTest, DirectMergeReverseDirectionHasRuleAnnotation) {
  EGraph<Symbol> graph;
  graph.withExplanationsEnabled();

  Id a = graph.add(makeNode("a"));
  Id b = graph.add(makeNode("b"));

  graph.unionTrusted(a, b, "rev-rule");
  graph.rebuild();

  auto expl = explainIdEquivalence(graph, b, a);
  EXPECT_GE(expl.treeSize(), 2U);

  const auto &trees = expl.trees();
  bool found = false;
  for (const auto &step : trees) {
    if (step->forward_rule == "rev-rule" || step->backward_rule == "rev-rule") {
      found = true;
      break;
    }
  }
  EXPECT_TRUE(found) << "Rule name must appear even when explaining in reverse";
}

// Congruence: child_proofs are populated on the congruence step.
TEST(ExplanationTest, CongruenceChildProofsHaveContent) {
  EGraph<Symbol> graph;
  graph.withExplanationsEnabled();

  Id a = graph.add(makeNode("a"));
  Id b = graph.add(makeNode("b"));
  Id fa = graph.add(makeNode("f", {a}));
  Id fb = graph.add(makeNode("f", {b}));

  graph.unionTrusted(a, b, "ab-eq");
  graph.rebuild();

  EXPECT_EQ(graph.find(fa), graph.find(fb));

  auto expl = explainIdEquivalence(graph, fa, fb);
  const auto &trees = expl.trees();

  bool found_with_child_proofs = false;
  for (const auto &step : trees) {
    if (!step->child_proofs.empty()) {
      found_with_child_proofs = true;
      for (const auto &cp : step->child_proofs) {
        EXPECT_GE(cp.size(), 1U)
            << "Each child proof slot should have at least one step";
      }
      break;
    }
  }
  EXPECT_TRUE(found_with_child_proofs)
      << "Congruence step should have populated child_proofs";
}

// Transitive proof with 4 nodes: a==b==c==d via three rules.
// All three rule names must appear in the proof of a==d.
TEST(ExplanationTest, FourStepTransitiveProof) {
  EGraph<Symbol> graph;
  graph.withExplanationsEnabled();

  Id a = graph.add(makeNode("a"));
  Id b = graph.add(makeNode("b"));
  Id c = graph.add(makeNode("c"));
  Id d = graph.add(makeNode("d"));

  graph.unionTrusted(a, b, "rule-ab");
  graph.rebuild();
  graph.unionTrusted(b, c, "rule-bc");
  graph.rebuild();
  graph.unionTrusted(c, d, "rule-cd");
  graph.rebuild();

  EXPECT_EQ(graph.find(a), graph.find(d));

  auto expl = explainIdEquivalence(graph, a, d);
  EXPECT_GE(expl.treeSize(), 2U);

  const auto &trees = expl.trees();
  auto has_rule = [&](const std::string &name) -> bool {
    for (const auto &step : trees) {
      if (step->forward_rule == name || step->backward_rule == name) {
        return true;
      }
    }
    return false;
  };

  EXPECT_TRUE(has_rule("rule-ab")) << "rule-ab must appear in proof";
  EXPECT_TRUE(has_rule("rule-bc")) << "rule-bc must appear in proof";
  EXPECT_TRUE(has_rule("rule-cd")) << "rule-cd must appear in proof";
}

// Already-equal alternate rewrite: the first rule must still appear in
// the proof after a second alternate-rewrite edge is added.
TEST(ExplanationTest, AlternateRewriteFirstRuleStillVisible) {
  EGraph<Symbol> graph;
  graph.withExplanationsEnabled();

  Id a = graph.add(makeNode("a"));
  Id b = graph.add(makeNode("b"));

  graph.unionTrusted(a, b, "rule-1");
  graph.rebuild();

  // Second merge: already equivalent — creates a shortcut edge.
  graph.unionTrusted(a, b, "rule-2");
  graph.rebuild();

  auto expl = explainIdEquivalence(graph, a, b);
  EXPECT_GE(expl.treeSize(), 2U);

  const auto &trees = expl.trees();
  bool found_rule1 = false;
  for (const auto &step : trees) {
    if (step->forward_rule == "rule-1" || step->backward_rule == "rule-1") {
      found_rule1 = true;
      break;
    }
  }
  EXPECT_TRUE(found_rule1)
      << "rule-1 must remain visible after alternate rewrite";
}

// Flat proof: after a named merge, at least one FlatTerm carries a
// forward or backward rule annotation (rule labels survive flattening).
TEST(ExplanationTest, FlatProofPreservesRuleAnnotations) {
  EGraph<Symbol> graph;
  graph.withExplanationsEnabled();

  Id a = graph.add(makeNode("a"));
  Id b = graph.add(makeNode("b"));

  graph.unionTrusted(a, b, "flat-rule");
  graph.rebuild();

  auto expl = explainIdEquivalence(graph, a, b);
  const auto &flat = expl.flatExplanation();

  EXPECT_GE(flat.size(), 2U);

  bool has_annotation = false;
  for (const auto &ft : flat) {
    if (ft.hasRewriteForward() || ft.hasRewriteBackward()) {
      has_annotation = true;
      break;
    }
  }
  EXPECT_TRUE(has_annotation)
      << "At least one FlatTerm must carry a rule annotation";
}

// Flat proof: removeRewrites() strips all annotations recursively.
TEST(ExplanationTest, FlatTermRemoveRewritesClearsAnnotations) {
  EGraph<Symbol> graph;
  graph.withExplanationsEnabled();

  Id a = graph.add(makeNode("a"));
  Id b = graph.add(makeNode("b"));

  graph.unionTrusted(a, b, "clear-rule");
  graph.rebuild();

  auto expl = explainIdEquivalence(graph, a, b);
  // Take a mutable copy of the flat terms.
  auto flat = expl.flatExplanation();

  for (auto &ft : flat) {
    ft.removeRewrites();
  }

  for (const auto &ft : flat) {
    EXPECT_FALSE(ft.hasRewriteForward())
        << "No forward rule should remain after removeRewrites()";
    EXPECT_FALSE(ft.hasRewriteBackward())
        << "No backward rule should remain after removeRewrites()";
  }
}

// Error path: explainEquivalence with an expression not in the e-graph
// should throw std::invalid_argument.
TEST(ExplanationTest, ExplainEquivalenceNotFoundThrows) {
  EGraph<Symbol> graph;
  graph.withExplanationsEnabled();

  RecExpr<Symbol> expr_a;
  expr_a.add(makeNode("a"));
  graph.addExpr(expr_a);

  // 'b' was never added.
  RecExpr<Symbol> expr_b;
  expr_b.add(makeNode("b"));

  EXPECT_THROW(explainEquivalence(graph, expr_a, expr_b),
               std::invalid_argument);
}

// Error path: exactly which pairs throw vs succeed.
TEST(ExplanationTest, NonEquivalentPairsThrowInvalidArgument) {
  EGraph<Symbol> graph;
  graph.withExplanationsEnabled();

  Id a = graph.add(makeNode("a"));
  Id b = graph.add(makeNode("b"));
  Id c = graph.add(makeNode("c"));

  graph.unionTrusted(a, b, "ab");
  graph.rebuild();

  EXPECT_NO_THROW(explainIdEquivalence(graph, a, b));
  EXPECT_THROW(explainIdEquivalence(graph, a, c), std::invalid_argument);
  EXPECT_THROW(explainIdEquivalence(graph, b, c), std::invalid_argument);
}

// FlatTerm directional helpers: hasRewriteForward / hasRewriteBackward
// report correctly for each combination.
TEST(ExplanationTest, FlatTermDirectionalHelpers) {
  FlatTerm<Symbol> ft_fwd;
  ft_fwd.node = makeNode("x");
  ft_fwd.forward_rule = "forward-only";
  EXPECT_TRUE(ft_fwd.hasRewriteForward());
  EXPECT_FALSE(ft_fwd.hasRewriteBackward());

  FlatTerm<Symbol> ft_bwd;
  ft_bwd.node = makeNode("y");
  ft_bwd.backward_rule = "backward-only";
  EXPECT_FALSE(ft_bwd.hasRewriteForward());
  EXPECT_TRUE(ft_bwd.hasRewriteBackward());

  FlatTerm<Symbol> ft_none;
  ft_none.node = makeNode("z");
  EXPECT_FALSE(ft_none.hasRewriteForward());
  EXPECT_FALSE(ft_none.hasRewriteBackward());
}

// FlatTerm::hasRewriteForward propagates into children recursively.
TEST(ExplanationTest, FlatTermRecursiveForwardCheck) {
  FlatTerm<Symbol> parent;
  parent.node = makeNode("parent");

  FlatTerm<Symbol> child;
  child.node = makeNode("child");
  child.forward_rule = "nested-rule";

  parent.children.push_back(child);

  EXPECT_FALSE(parent.hasForwardRule());
  EXPECT_TRUE(parent.hasRewriteForward())
      << "hasRewriteForward() must propagate into children";
}

// Trivial self-explanation flat size is 1 with no rule annotations.
TEST(ExplanationTest, TrivialSelfExplanationFlatForm) {
  EGraph<Symbol> graph;
  graph.withExplanationsEnabled();

  Id a = graph.add(makeNode("a"));

  auto expl = explainIdEquivalence(graph, a, a);
  EXPECT_LE(expl.treeSize(), 1U);

  const auto &flat = expl.flatExplanation();
  EXPECT_GE(flat.size(), 1U);

  for (const auto &ft : flat) {
    EXPECT_FALSE(ft.hasRewriteForward());
    EXPECT_FALSE(ft.hasRewriteBackward());
  }
}

// FlatTerm::getRecExpr() on a bare leaf produces a single-node RecExpr.
TEST(ExplanationTest, FlatTermGetRecExprLeafNode) {
  FlatTerm<Symbol> leaf;
  leaf.node = makeNode("leaf-op");

  auto rec = leaf.getRecExpr();
  EXPECT_EQ(rec.size(), 1U);
  EXPECT_EQ(rec.root().op.value, "leaf-op");
}

// Explain after Runner saturation still produces valid proofs.
TEST(ExplanationTest, ExplainAfterRunnerSaturationWorks) {
  Runner<Symbol> runner;
  runner.egraph.withExplanationsEnabled();

  RecExpr<Symbol> expr;
  Id ea = expr.add(makeNode("a"));
  Id e0 = expr.add(makeNode("0"));
  expr.add(makeNode("+", {ea, e0}));
  runner.addExpr(expr);
  Id plus_root = runner.roots[0];

  PatternAst<Symbol> lhs_ast;
  Id lx = lhs_ast.add(makePatVar("?x"));
  Id l0 = lhs_ast.add(makePatENode("0"));
  lhs_ast.add(makePatENode("+", {lx, l0}));
  PatternAst<Symbol> rhs_ast;
  rhs_ast.add(makePatVar("?x"));

  Rewrite<Symbol> rule("plus-zero-sat", Pattern<Symbol>(std::move(lhs_ast)),
                       Pattern<Symbol>(std::move(rhs_ast)));
  runner.run({rule});

  RecExpr<Symbol> just_a;
  just_a.add(makeNode("a"));
  Id a_id = runner.egraph.lookupRecExpr(just_a);

  EXPECT_EQ(runner.egraph.find(plus_root), runner.egraph.find(a_id));

  auto expl = explainIdEquivalence(runner.egraph, plus_root, a_id);
  EXPECT_GE(expl.treeSize(), 2U);
}

// ===================================================================
// ErrorContractTest: misuse of core APIs fails consistently
//
// Exception contract (per README spec):
//   - Invalid or foreign Ids → std::out_of_range
//   - Malformed construction arguments → std::invalid_argument
//   - Wrong-state access (explanations off, wrong accessor) → std::logic_error
//   - Subst::get() → nullptr on miss (no throw)
//   - Subst::at() → std::out_of_range on miss
// ===================================================================

TEST(ErrorContractTest, ForeignIdInFindThrows) {
  EGraph<Symbol> graph_a;
  EGraph<Symbol> graph_b;
  Id id_from_a = graph_a.add(makeNode("a"));
  EXPECT_THROW(graph_b.find(id_from_a), std::out_of_range);
}

TEST(ErrorContractTest, ForeignIdInAddChildThrows) {
  EGraph<Symbol> graph_a;
  EGraph<Symbol> graph_b;
  Id id_from_a = graph_a.add(makeNode("leaf"));
  EXPECT_THROW(graph_b.add(makeNode("f", {id_from_a})), std::out_of_range);
}

TEST(ErrorContractTest, ForeignIdInMergeThrows) {
  EGraph<Symbol> graph_a;
  EGraph<Symbol> graph_b;
  graph_a.add(makeNode("leaf1"));
  graph_a.add(makeNode("leaf2"));
  Id id_large = graph_a.add(makeNode("leaf3"));

  Id id_b = graph_b.add(makeNode("leaf"));

  EXPECT_THROW(graph_b.merge(id_large, id_b), std::out_of_range);
  EXPECT_THROW(graph_b.merge(id_b, id_large), std::out_of_range);
}

TEST(ErrorContractTest, DefaultIdFindThrows) {
  EGraph<Symbol> graph;
  graph.add(makeNode("a"));

  Id invalid;
  EXPECT_FALSE(invalid.valid());
  EXPECT_THROW(graph.find(invalid), std::out_of_range);
}

TEST(ErrorContractTest, DefaultIdContainsThrows) {
  EGraph<Symbol> graph;
  graph.add(makeNode("a"));

  Id invalid;
  EXPECT_THROW(graph.contains(invalid), std::out_of_range);
}

TEST(ErrorContractTest, DefaultIdAddChildThrows) {
  EGraph<Symbol> graph;
  graph.add(makeNode("a"));

  Id invalid;
  EXPECT_THROW(graph.add(makeNode("f", {invalid})), std::out_of_range);
}

TEST(ErrorContractTest, DefaultIdMergeThrows) {
  EGraph<Symbol> graph;
  Id a = graph.add(makeNode("a"));

  Id invalid;
  EXPECT_THROW(graph.merge(invalid, a), std::out_of_range);
  EXPECT_THROW(graph.merge(a, invalid), std::out_of_range);
}

TEST(ErrorContractTest, RecExprForwardRefInAddThrows) {
  RecExpr<Symbol> expr;
  expr.add(makeNode("a"));
  EXPECT_THROW(expr.add(makeNode("f", {Id(5)})), std::out_of_range);
}

TEST(ErrorContractTest, RecExprForwardRefInVectorConstructorThrows) {
  std::vector<ENode<Symbol>> bad_nodes;
  bad_nodes.push_back(makeNode("f", {Id(1)}));
  bad_nodes.push_back(makeNode("a"));
  EXPECT_THROW((RecExpr<Symbol>(std::move(bad_nodes))), std::out_of_range);
}

TEST(ErrorContractTest, RecExprSelfReferenceThrows) {
  RecExpr<Symbol> expr;
  EXPECT_THROW(expr.add(makeNode("f", {Id(0)})), std::out_of_range);
}

TEST(ErrorContractTest, EmptyRecExprRootThrows) {
  RecExpr<Symbol> expr;
  EXPECT_TRUE(expr.empty());
  EXPECT_THROW(expr.root(), std::out_of_range);
  EXPECT_THROW(expr.rootId(), std::out_of_range);
}

TEST(ErrorContractTest, EmptyRecExprAddExprToGraphThrows) {
  EGraph<Symbol> graph;
  RecExpr<Symbol> empty_expr;
  EXPECT_THROW(graph.addExpr(empty_expr), std::out_of_range);
}

TEST(ErrorContractTest, RecExprIndexOutOfRangeThrows) {
  RecExpr<Symbol> expr;
  expr.add(makeNode("a"));

  EXPECT_THROW(expr[Id(1)], std::out_of_range);
  EXPECT_THROW(expr.at(1), std::out_of_range);
  EXPECT_THROW(expr[Id()], std::out_of_range);
}

TEST(ErrorContractTest, SubstGetMissingReturnsNullptr) {
  Subst subst;
  Var missing = Var::fromString("?missing");
  EXPECT_EQ(subst.get(missing), nullptr);
}

TEST(ErrorContractTest, SubstAtMissingVarThrows) {
  Subst subst;
  Var present = Var::fromString("?x");
  Var absent = Var::fromString("?y");
  subst.insert(present, Id(0));

  EXPECT_THROW(subst.at(absent), std::out_of_range);
}

TEST(ErrorContractTest, SubstAtOnEmptySubstThrows) {
  Subst subst;
  EXPECT_THROW(subst.at(Var::fromString("?any")), std::out_of_range);
}

TEST(ErrorContractTest, VarFromStringMissingPrefixThrows) {
  EXPECT_THROW(Var::fromString("x"), std::invalid_argument);
  EXPECT_THROW(Var::fromString(""), std::invalid_argument);
}

TEST(ErrorContractTest, VarFromStringBareQuestionMarkThrows) {
  EXPECT_THROW(Var::fromString("?"), std::invalid_argument);
}

TEST(ErrorContractTest, VarFromStringMalformedNumericThrows) {
  EXPECT_THROW(Var::fromString("?#"), std::invalid_argument);
  EXPECT_THROW(Var::fromString("?#notanumber"), std::invalid_argument);
}

TEST(ErrorContractTest, VarAccessorOnWrongKindThrows) {
  Var sym = Var::fromString("?x");
  Var num = Var::fromU32(5);

  EXPECT_THROW(sym.number(), std::logic_error);
  EXPECT_THROW(num.symbol(), std::logic_error);
}

TEST(ErrorContractTest, PatternVarNodeWithChildrenThrows) {
  PatternAst<Symbol> ast;
  Id leaf = ast.add(makePatVar("?x"));

  PatNode bad_node;
  bad_node.op = ENodeOrVar<Symbol>::fromVar(Var::fromString("?y"));
  bad_node.children = {leaf};
  ast.add(bad_node);

  EXPECT_THROW(Pattern<Symbol> pat(std::move(ast)), std::invalid_argument);
}

TEST(ErrorContractTest, ENodeOrVarEnodeAccessOnVarThrows) {
  auto var_val = ENodeOrVar<Symbol>::fromVar(Var::fromString("?x"));
  EXPECT_TRUE(var_val.isVar());
  EXPECT_THROW(var_val.enode(), std::logic_error);
}

TEST(ErrorContractTest, ENodeOrVarVarAccessOnEnodeThrows) {
  auto enode_val = ENodeOrVar<Symbol>::fromENode(makeNode("f"));
  EXPECT_TRUE(enode_val.isENode());
  EXPECT_THROW(enode_val.var(), std::logic_error);
}

TEST(ErrorContractTest, ENodeOrVarMutableChildrenOnVarThrows) {
  auto var_val = ENodeOrVar<Symbol>::fromVar(Var::fromString("?x"));
  EXPECT_THROW(var_val.children(), std::logic_error);
}

TEST(ErrorContractTest, EnableExplanationsAfterAddThrows) {
  EGraph<Symbol> graph;
  graph.add(makeNode("a"));
  EXPECT_THROW(graph.withExplanationsEnabled(), std::logic_error);
}

TEST(ErrorContractTest, ExplainAccessWithoutEnablingThrows) {
  EGraph<Symbol> graph;
  EXPECT_THROW(graph.explain(), std::logic_error);
}

TEST(ErrorContractTest, AddExprRootChildIndexOutOfRangeThrows) {
  EGraph<Symbol> graph;
  std::vector<ENode<Symbol>> subs = {makeNode("a")};
  ENode<Symbol> root_node = makeNode("f", {Id(5)});
  EXPECT_THROW(graph.addExpr(root_node, subs), std::out_of_range);
}

TEST(ErrorContractTest, AddExprRootInvalidSentinelChildThrows) {
  EGraph<Symbol> graph;
  std::vector<ENode<Symbol>> subs = {makeNode("a")};
  ENode<Symbol> root_node = makeNode("f", {Id()});
  EXPECT_THROW(graph.addExpr(root_node, subs), std::out_of_range);
}

// ===================================================================
// ApiStabilitySmokeTest: intended public adoption entry points
// ===================================================================

TEST(ApiStabilitySmokeTest, EGraphAddMergeRebuildEntryPoints) {
  EGraph<Symbol> graph;

  Id a = graph.add(makeNode("a"));
  Id b = graph.add(makeNode("b"));
  Id fa = graph.add(makeNode("f", {a}));
  Id fb = graph.add(makeNode("f", {b}));

  EXPECT_NE(graph.find(fa), graph.find(fb));

  Id merged = graph.merge(a, b);
  EXPECT_EQ(graph.find(merged), graph.find(a));

  graph.rebuild();

  EXPECT_EQ(graph.find(a), graph.find(b));
  EXPECT_EQ(graph.find(fa), graph.find(fb));
  EXPECT_TRUE(graph.contains(a));
  EXPECT_EQ(graph.classCount(), 2U);
}

TEST(ApiStabilitySmokeTest, RecExprConstructionAndAddExprEntryPoints) {
  RecExpr<Symbol> expr;
  Id a = expr.add(makeNode("a"));
  Id zero = expr.add(makeNode("0"));
  Id plus = expr.add(makeNode("+", {a, zero}));

  EXPECT_EQ(expr.rootId(), plus);
  EXPECT_EQ(expr.root().op.value, "+");

  EGraph<Symbol> graph;
  Id root = graph.addExpr(expr);
  Id root_again = graph.addExpr(expr);

  EXPECT_EQ(graph.find(root), graph.find(root_again));
  EXPECT_EQ(graph.memoSize(), 3U);
}

TEST(ApiStabilitySmokeTest, PatternSearcherSearchEntryPoints) {
  auto setup = makePlusGraph("a", "b");
  Pattern<Symbol> pattern(makeBinaryPatternAst("+", "?x", "?y"));
  PatternSearcher<Symbol> searcher(pattern);

  auto all_matches = searcher.search(setup.graph);
  ASSERT_EQ(all_matches.size(), 1U);
  ASSERT_EQ(all_matches[0].substs.size(), 1U);

  auto anchored = searcher.searchEclass(setup.graph, setup.root);
  ASSERT_EQ(anchored.substs.size(), 1U);
  EXPECT_EQ(setup.graph.find(*anchored.substs[0].get(Var::fromString("?x"))),
            setup.graph.find(setup.left));
  EXPECT_EQ(setup.graph.find(*anchored.substs[0].get(Var::fromString("?y"))),
            setup.graph.find(setup.right));
}

TEST(ApiStabilitySmokeTest, RunnerAppliesRewritePublicEntryPoints) {
  Runner<Symbol> runner;
  runner.withIterLimit(10).withNodeLimit(1000).withTimeLimit(1.0);

  RecExpr<Symbol> expr = makeBinaryExpr("a", "0", "+");
  Id root = runner.addExpr(expr);

  std::vector<Rewrite<Symbol>> rules = {makePlusZeroRule()};
  runner.run(rules);

  ASSERT_TRUE(runner.has_stop_reason);
  EXPECT_EQ(runner.stop_reason.kind, StopReasonKind::Saturated);
  EXPECT_FALSE(runner.iterations.empty());

  Report report = runner.report();
  EXPECT_EQ(report.stop_reason.kind, StopReasonKind::Saturated);
  EXPECT_EQ(report.egraph_nodes, runner.egraph.memoSize());

  Extractor<Symbol> extractor(runner.egraph);
  auto [cost, best] = extractor.findBest(root);
  EXPECT_EQ(cost, 1U);
  EXPECT_EQ(best.root().op.value, "a");
}

TEST(ApiStabilitySmokeTest, ExtractorFindBestNodeEntryPoints) {
  EGraph<Symbol> graph;
  Id a = graph.add(makeNode("a"));
  Id zero = graph.add(makeNode("0"));
  Id plus = graph.add(makeNode("+", {a, zero}));
  graph.merge(a, plus);
  graph.rebuild();

  Extractor<Symbol> extractor(graph);
  const auto &best_node = extractor.findBestNode(plus);
  auto [cost, best_expr] = extractor.findBest(plus);

  EXPECT_EQ(best_node.op.value, "a");
  EXPECT_EQ(cost, extractor.findBestCost(plus));
  EXPECT_EQ(best_expr.root().op.value, "a");
}

TEST(ApiStabilitySmokeTest, ExplanationEntryPointsProduceProofs) {
  EGraph<Symbol> graph;
  graph.withExplanationsEnabled();

  RecExpr<Symbol> expr_a;
  expr_a.add(makeNode("a"));
  RecExpr<Symbol> expr_b;
  expr_b.add(makeNode("b"));

  Id a_id = graph.addExpr(expr_a);
  Id b_id = graph.addExpr(expr_b);
  graph.unionTrusted(a_id, b_id, "api-smoke-proof");
  graph.rebuild();

  auto by_id = explainIdEquivalence(graph, a_id, b_id);
  auto by_expr = explainEquivalence(graph, expr_a, expr_b);

  EXPECT_GE(by_id.treeSize(), 2U);
  EXPECT_GE(by_expr.treeSize(), 2U);
  EXPECT_FALSE(by_id.flatExplanation().empty());
  EXPECT_FALSE(by_expr.flatExplanation().empty());
}

// ===================================================================
// InteropWorkflowTest: end-to-end regression coverage for the
// rewrite → rebuild → extract → explain interaction surface.
// ===================================================================

// 1. Rewrite → rebuild → extract
// Build (+ a 0), apply plus-zero, rebuild, extract the best term.
// The extracted term must be "a" (cost 1), not the original (+ a 0).
TEST(InteropWorkflowTest, RewriteThenRebuildThenExtract) {
  EGraph<Symbol> graph;
  Id a = graph.add(makeNode("a"));
  Id zero = graph.add(makeNode("0"));
  Id plus = graph.add(makeNode("+", {a, zero}));

  // Precondition: the two classes are disjoint before the rewrite.
  EXPECT_NE(graph.find(plus), graph.find(a));

  Rewrite<Symbol> rule = makePlusZeroRule();
  std::size_t merges = rule.run(graph);
  EXPECT_GE(merges, 1U);

  graph.rebuild();

  // Postcondition: (+ a 0) now lives in the same class as "a".
  EXPECT_EQ(graph.find(plus), graph.find(a));

  Extractor<Symbol> extractor(graph);
  auto [cost, best] = extractor.findBest(plus);

  // Extraction must return the cheaper representative "a".
  EXPECT_EQ(cost, 1U);
  EXPECT_EQ(best.size(), 1U);
  EXPECT_EQ(best.root().op.value, "a");
}

// 2. Rewrite → rebuild → explain
// Same flow, but verify that an explanation proof is produced for the
// equivalence that the rewrite created.
TEST(InteropWorkflowTest, RewriteThenRebuildThenExplain) {
  EGraph<Symbol> graph;
  graph.withExplanationsEnabled();

  Id a = graph.add(makeNode("a"));
  Id zero = graph.add(makeNode("0"));
  Id plus = graph.add(makeNode("+", {a, zero}));

  Rewrite<Symbol> rule = makePlusZeroRule();
  rule.run(graph);
  graph.rebuild();

  ASSERT_EQ(graph.find(plus), graph.find(a));

  auto expl = explainIdEquivalence(graph, plus, a);
  EXPECT_GE(expl.treeSize(), 2U);

  const auto &flat = expl.flatExplanation();
  ASSERT_FALSE(flat.empty());

  bool has_annotation = false;
  for (const auto &ft : flat) {
    if (ft.hasRewriteForward() || ft.hasRewriteBackward()) {
      has_annotation = true;
      break;
    }
  }
  EXPECT_TRUE(has_annotation) << "Expected a rule annotation in flat proof";

  bool found_rule_name = false;
  std::function<bool(const FlatTerm<Symbol> &)> search_rule =
      [&](const FlatTerm<Symbol> &ft) -> bool {
    if (ft.forward_rule.find("plus-zero") != std::string::npos) return true;
    if (ft.backward_rule.find("plus-zero") != std::string::npos) return true;
    for (const auto &child : ft.children) {
      if (search_rule(child)) return true;
    }
    return false;
  };
  for (const auto &ft : flat) {
    if (search_rule(ft)) {
      found_rule_name = true;
      break;
    }
  }
  EXPECT_TRUE(found_rule_name)
      << "Expected 'plus-zero' annotation in flat proof";
}

// 3. Multi-step rewrite → extract → explain
// Apply two rewrites in sequence (commutativity then plus-zero) to
// (+ (+ a 0) b).  The fully reduced form should be (+ a b) with cost 3,
// and the explanation must cover the original → final equivalence.
TEST(InteropWorkflowTest, MultiStepRewriteExtractExplain) {
  EGraph<Symbol> graph;
  graph.withExplanationsEnabled();

  Id a = graph.add(makeNode("a"));
  Id zero = graph.add(makeNode("0"));
  Id b = graph.add(makeNode("b"));
  Id plus_a0 = graph.add(makeNode("+", {a, zero}));
  Id root = graph.add(makeNode("+", {plus_a0, b}));

  // Step 1: (+ ?x 0) => ?x  eliminates (+ a 0).
  Rewrite<Symbol> r1 = makePlusZeroRule();
  r1.run(graph);
  graph.rebuild();

  // After step 1, plus_a0 and a are in the same class.
  EXPECT_EQ(graph.find(plus_a0), graph.find(a));

  // root now has an equivalent (+ a b) because plus_a0 ≡ a.
  // Step 2: apply commutativity to grow the equivalence class further.
  Rewrite<Symbol> r2 = makeCommRule();
  r2.run(graph);
  graph.rebuild();

  // Extract the best (cheapest) representative for the root class.
  Extractor<Symbol> extractor(graph);
  auto [cost, best] = extractor.findBest(root);

  // The cheapest representative is (+ a b) with AstSize cost 3.
  EXPECT_EQ(cost, 3U);
  ASSERT_EQ(best.root().op.value, "+");

  // Also verify that the extracted children are "a" and "b".
  ASSERT_EQ(best.root().children.size(), 2U);
  const auto &lc = best[best.root().children[0]];
  const auto &rc = best[best.root().children[1]];
  // The two children must be "a" and "b" in some order.
  std::set<std::string> child_ops = {lc.op.value, rc.op.value};
  EXPECT_EQ(child_ops.count("a"), 1U);
  EXPECT_EQ(child_ops.count("b"), 1U);

  // Explain original root vs. extracted root identity.
  // We add the extracted expression to the graph and check equivalence.
  RecExpr<Symbol> best_copy = best;
  Id best_id = graph.addExpr(best_copy);
  graph.rebuild();

  EXPECT_EQ(graph.find(root), graph.find(best_id));

  auto expl = explainIdEquivalence(graph, root, best_id);
  EXPECT_GE(expl.treeSize(), 1U);
  EXPECT_FALSE(expl.flatExplanation().empty());
}

// 4. Runner saturation → extract → explain
// Use a Runner to saturate (+ a 0) with plus-zero, then extract and explain.
// This validates that the three subsystems interoperate correctly when
// saturation is driven by the Runner rather than manual rule application.
TEST(InteropWorkflowTest, RunnerSaturationExtractExplain) {
  Runner<Symbol> runner;
  runner.egraph.withExplanationsEnabled();

  RecExpr<Symbol> expr;
  Id ea = expr.add(makeNode("a"));
  Id e0 = expr.add(makeNode("0"));
  expr.add(makeNode("+", {ea, e0}));
  Id root = runner.addExpr(expr);

  runner.run({makePlusZeroRule()});

  ASSERT_TRUE(runner.has_stop_reason);
  EXPECT_EQ(runner.stop_reason.kind, StopReasonKind::Saturated);

  // ── extraction ──────────────────────────────────────────────────────────
  Extractor<Symbol> extractor(runner.egraph);
  auto [cost, best] = extractor.findBest(root);
  EXPECT_EQ(cost, 1U);
  EXPECT_EQ(best.root().op.value, "a");

  // ── explanation ─────────────────────────────────────────────────────────
  // Look up the "a" class so we can explain root ≡ a.
  RecExpr<Symbol> just_a;
  just_a.add(makeNode("a"));
  Id a_id = runner.egraph.lookupRecExpr(just_a);

  ASSERT_EQ(runner.egraph.find(root), runner.egraph.find(a_id));

  auto expl = explainIdEquivalence(runner.egraph, root, a_id);
  EXPECT_GE(expl.treeSize(), 2U);
  EXPECT_FALSE(expl.flatExplanation().empty());
}

// ===================================================================
// CookbookWorkflowTest: Smoke-backed examples for the README cookbook.
// These tests precisely mirror the code snippets in the README.
// ===================================================================

// 1. Structural expression construction (RecExprBuilder)
TEST(CookbookWorkflowTest, StructuralExpressionConstruction) {
  RecExprBuilder<Symbol> builder;
  Id a = builder.addLeaf(Symbol{"a"});
  Id zero = builder.addLeaf(Symbol{"0"});
  builder.addNode(Symbol{"+"}, {a, zero});
  RecExpr<Symbol> expr = std::move(builder).build();

  EXPECT_EQ(expr.size(), 3U);
  EXPECT_EQ(expr.root().op.value, "+");
}

// 2. Structural pattern authoring (PatternBuilder)
TEST(CookbookWorkflowTest, StructuralPatternAuthoring) {
  PatternBuilder<Symbol> builder;
  auto x = builder.appendVar("?x");
  auto zero = builder.appendNode(Symbol{"0"});
  builder.appendNode(Symbol{"+"}, {x, zero});
  Pattern<Symbol> pat = std::move(builder).buildPattern();

  EXPECT_EQ(pat.vars().size(), 1U);
  EXPECT_EQ(pat.vars()[0], Var::fromString("?x"));
}

// 3. Rewrite application + rebuild
TEST(CookbookWorkflowTest, RewriteApplicationAndRebuild) {
  EGraph<Symbol> graph;
  Id a = graph.add(makeNode("a"));
  Id zero = graph.add(makeNode("0"));
  Id plus = graph.add(makeNode("+", {a, zero}));

  Rewrite<Symbol> rule = makePlusZeroRule();
  rule.run(graph);
  graph.rebuild();

  EXPECT_EQ(graph.find(plus), graph.find(a));
}

// 4. Extraction (finding the best / cheapest term)
TEST(CookbookWorkflowTest, ExtractionBestTerm) {
  EGraph<Symbol> graph;
  Id a = graph.add(makeNode("a"));
  Id zero = graph.add(makeNode("0"));
  Id plus = graph.add(makeNode("+", {a, zero}));
  graph.merge(a, plus);
  graph.rebuild();

  Extractor<Symbol> extractor(graph);
  auto [cost, best] = extractor.findBest(plus);

  EXPECT_EQ(best.root().op.value, "a");
  EXPECT_EQ(cost, 1U);
}

// 5. Explanation (explaining why two terms are equivalent)
TEST(CookbookWorkflowTest, ExplanationEquivalence) {
  EGraph<Symbol> graph;
  graph.withExplanationsEnabled();
  Id a = graph.add(makeNode("a"));
  Id b = graph.add(makeNode("b"));
  graph.unionTrusted(a, b, "manual-merge");
  graph.rebuild();

  auto expl = explainIdEquivalence(graph, a, b);
  EXPECT_GE(expl.treeSize(), 2U);
  EXPECT_FALSE(expl.flatExplanation().empty());
}

// 6. Runner-based saturation
TEST(CookbookWorkflowTest, RunnerSaturation) {
  Runner<Symbol> runner;
  RecExpr<Symbol> expr = makeBinaryExpr("a", "0", "+");
  Id root = runner.addExpr(expr);

  runner.run({makePlusZeroRule()});

  EXPECT_EQ(runner.stop_reason.kind, StopReasonKind::Saturated);
  Extractor<Symbol> extractor(runner.egraph);
  EXPECT_EQ(extractor.findBest(root).second.root().op.value, "a");
}

// ===================================================================
// EdgeCaseResilienceTest: targeted regression coverage for edge cases
// most likely to hurt production usability.
//
// Cases covered:
//   1. AlreadyEqualMergeIsNoOp       — merge(id, id) is a no-op
//   2. NoMatchPathReturnsZeroMerges  — rewrite with no applicable matches
//   3. CycleOnlyExtractionBehavior   — self-referential class after merge
//   4. ExplanationMisuseThrows       — explainIdEquivalence without enable
//   5. ZeroRewritesSaturatesImmediately — Runner with empty rule list
// ===================================================================

// 1. Already-equal merge: merging an id with itself must be a no-op.
//    The graph size and class count must not change.
TEST(EdgeCaseResilienceTest, AlreadyEqualMergeIsNoOp) {
  EGraph<Symbol> graph;
  Id a = graph.add(makeNode("a"));
  Id b = graph.add(makeNode("b"));

  std::size_t classes_before = graph.classCount();
  std::size_t memo_before = graph.memoSize();

  // Merge with itself — must be a no-op and return the canonical id.
  Id result = graph.merge(a, a);
  graph.rebuild();

  EXPECT_EQ(graph.find(result), graph.find(a));
  EXPECT_EQ(graph.classCount(), classes_before);
  EXPECT_EQ(graph.memoSize(), memo_before);

  // Also verify merge of canonical-equal ids (a was merged with itself above).
  Id result2 = graph.merge(a, b);
  // This is a real merge; sanity-check that after a true merge the already-
  // equal path is stable on subsequent calls.
  graph.rebuild();
  Id result3 = graph.merge(a, b);
  graph.rebuild();
  EXPECT_EQ(graph.find(result2), graph.find(result3));
}

// 2. No-match path: a rewrite rule whose LHS pattern does not appear in the
//    e-graph must produce exactly 0 merges and leave the graph unchanged.
TEST(EdgeCaseResilienceTest, NoMatchPathReturnsZeroMerges) {
  EGraph<Symbol> graph;
  // Graph only has leaf nodes — no binary operators.
  Id a = graph.add(makeNode("a"));
  Id b = graph.add(makeNode("b"));

  std::size_t classes_before = graph.classCount();
  std::size_t memo_before = graph.memoSize();

  // plus-zero rule: LHS is (+ ?x 0) — requires a "+" operator, absent here.
  Rewrite<Symbol> rule = makePlusZeroRule();
  std::size_t merges = rule.run(graph);

  EXPECT_EQ(merges, 0U);
  EXPECT_EQ(graph.classCount(), classes_before);
  EXPECT_EQ(graph.memoSize(), memo_before);

  // Graph contents must be unchanged.
  EXPECT_EQ(graph.find(a), graph.find(a));
  EXPECT_EQ(graph.find(b), graph.find(b));
  EXPECT_NE(graph.find(a), graph.find(b));
}

// 3. Cycle-resilient extraction: after merging a node with its parent class
//    the e-class becomes self-referential, but the class still contains the
//    original leaf.  The Extractor must still produce a finite result (it
//    picks the leaf, not the cyclic node).
//
//    Note: a "pure cycle-only" class (no leaves at all) cannot be created
//    through normal EGraph API calls, because add() always requires children
//    to already exist.  The documented std::runtime_error path in
//    assertHasCost() is a defensive guard against corrupted or externally
//    mutated graphs.  This test verifies the realistic scenario: the
//    extractor correctly ignores the self-referential node and picks the
//    cheapest acyclic term in the class.
TEST(EdgeCaseResilienceTest, CycleResilientExtractionPicksLeaf) {
  EGraph<Symbol> graph;

  // Build: a, f(a), then merge a's class with f(a)'s class.
  // After rebuild, the class contains both `a` (leaf) and `f(self)`.
  Id a = graph.add(makeNode("a"));
  Id fa = graph.add(makeNode("f", {a}));

  EXPECT_NE(graph.find(a), graph.find(fa));

  graph.merge(a, fa);
  graph.rebuild();

  // Both ids now belong to the same class.
  EXPECT_EQ(graph.find(a), graph.find(fa));

  // Extraction must succeed and return the leaf `a` (cost 1), not f(self).
  Extractor<Symbol> extractor(graph);
  auto [cost, best] = extractor.findBest(a);
  EXPECT_EQ(cost, 1U);
  EXPECT_EQ(best.root().op.value, "a");

  // findBestNode must also point to the leaf.
  const auto &best_node = extractor.findBestNode(a);
  EXPECT_TRUE(best_node.children.empty());
  EXPECT_EQ(best_node.op.value, "a");
}

// 4. Explanation misuse: calling explainIdEquivalence on an EGraph that
//    was NOT initialized with withExplanationsEnabled() must throw
//    std::logic_error.  Same for explainEquivalence.
TEST(EdgeCaseResilienceTest, ExplanationMisuseThrowsLogicError) {
  EGraph<Symbol> graph;
  Id a = graph.add(makeNode("a"));
  Id b = graph.add(makeNode("b"));
  graph.merge(a, b);
  graph.rebuild();

  // Explanations are NOT enabled — both free functions must throw.
  EXPECT_THROW(explainIdEquivalence(graph, a, b), std::logic_error);

  // explainEquivalence also checks before looking up the RecExprs.
  RecExpr<Symbol> expr_a;
  expr_a.add(makeNode("a"));
  RecExpr<Symbol> expr_b;
  expr_b.add(makeNode("b"));
  EXPECT_THROW(explainEquivalence(graph, expr_a, expr_b), std::logic_error);

  // egraph.explain() low-level accessor also throws.
  EXPECT_THROW(graph.explain(), std::logic_error);

  // Verify the accessor reports the correct state.
  EXPECT_FALSE(graph.explanationsEnabled());
}

// 5. Saturation with 0 rewrites: a Runner given an empty rule list must
//    reach Saturated on the very first iteration (no rules, no merges,
//    scheduler allows stop, size unchanged).
TEST(EdgeCaseResilienceTest, ZeroRewritesSaturatesImmediately) {
  Runner<Symbol> runner;
  RecExpr<Symbol> expr;
  Id ea = expr.add(makeNode("a"));
  Id e0 = expr.add(makeNode("0"));
  expr.add(makeNode("+", {ea, e0}));
  runner.addExpr(expr);

  // Run with an empty rule vector.
  runner.run(std::vector<Rewrite<Symbol>>{});

  ASSERT_TRUE(runner.has_stop_reason);
  EXPECT_EQ(runner.stop_reason.kind, StopReasonKind::Saturated);

  // Must have executed exactly 1 iteration (the saturation check fires
  // immediately since no rules could have fired).
  EXPECT_EQ(runner.iterations.size(), 1U);
  EXPECT_TRUE(runner.iterations[0].applied.empty());
  EXPECT_TRUE(runner.iterations[0].has_stop_reason);
  EXPECT_EQ(runner.iterations[0].stop_reason.kind, StopReasonKind::Saturated);

  // E-graph must be unchanged from what was added.
  EXPECT_EQ(runner.egraph.classCount(), 3U);  // a, 0, (+ a 0)
}

// ===================================================================
// SmokeTest: Canonical end-to-end adoption scenario for Q2 2026.
//
// This test is the single authoritative smoke check for the quarter
// adoption path. It exercises every layer of the public API in sequence:
//
//   RecExprBuilder → EGraph::addExpr → Runner saturation →
//   Extractor::findBest → explainIdEquivalence
//
// Run it directly with:
//   ./build/egraph_test --gtest_filter=SmokeTest.EndToEndAdoptionFlow
// ===================================================================

// End-to-end quarter adoption smoke check.
//
// Expression: (+ a 0)
// Rule:       (+ ?x 0) => ?x   ("plus-zero")
//
// After saturation the e-class that originally held (+ a 0) also
// contains just `a`.  The extractor must return `a` (cost 1), and the
// explainer must produce a non-empty proof.
TEST(SmokeTest, EndToEndAdoptionFlow) {
  // ── 1. Build the input expression with RecExprBuilder ────────────────
  // Use RecExprBuilder so expression construction is index-bookkeeping-free.
  RecExprBuilder<Symbol> builder;
  Id ba = builder.addLeaf(Symbol{"a"});
  Id b0 = builder.addLeaf(Symbol{"0"});
  builder.addNode(Symbol{"+"}, {ba, b0});
  RecExpr<Symbol> expr = std::move(builder).build();

  ASSERT_EQ(expr.size(), 3U);
  ASSERT_EQ(expr.root().op.value, "+");

  // ── 2. Insert into Runner and enable explanations ─────────────────────
  Runner<Symbol> runner;
  runner.egraph.withExplanationsEnabled();  // must be called before addExpr
  Id root = runner.addExpr(expr);

  // ── 3. Saturate with the plus-zero rule ───────────────────────────────
  runner.run({makePlusZeroRule()});

  ASSERT_TRUE(runner.has_stop_reason);
  EXPECT_EQ(runner.stop_reason.kind, StopReasonKind::Saturated);

  // ── 4. Extract the best (cheapest) term ───────────────────────────────
  Extractor<Symbol> extractor(runner.egraph);
  auto [cost, best] = extractor.findBest(root);

  // The rewrite (+ a 0) → a must have been applied; best term is `a`.
  EXPECT_EQ(cost, 1U);
  ASSERT_EQ(best.root().op.value, "a");

  // ── 5. Explain the equivalence (+ a 0) ≡ a ────────────────────────────
  // Look up the canonical Id for `a` so we can explain root ≡ a.
  RecExpr<Symbol> just_a;
  just_a.add(makeNode("a"));
  Id a_id = runner.egraph.lookupRecExpr(just_a);

  ASSERT_EQ(runner.egraph.find(root), runner.egraph.find(a_id));

  auto expl = explainIdEquivalence(runner.egraph, root, a_id);

  // The proof must be non-trivial: at least the rewrite step itself.
  EXPECT_GE(expl.treeSize(), 2U);
  EXPECT_FALSE(expl.flatExplanation().empty());
}
