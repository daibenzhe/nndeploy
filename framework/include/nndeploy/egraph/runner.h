#ifndef _NNDEPLOY_EGRAPH_RUNNER_H_
#define _NNDEPLOY_EGRAPH_RUNNER_H_

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <functional>
#include <limits>
#include <memory>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "nndeploy/egraph/egraph.h"
#include "nndeploy/egraph/pattern.h"
#include "nndeploy/egraph/recexpr.h"
#include "nndeploy/egraph/rewrite.h"

namespace nndeploy {
namespace egraph {

// ---------------------------------------------------------------------------
// StopReason: why the runner stopped.
//
// Mirrors egg::StopReason.
// ---------------------------------------------------------------------------

enum class StopReasonKind {
  Saturated,       // No rules made progress and scheduler agrees.
  IterationLimit,  // Hit the iteration ceiling.
  NodeLimit,       // Egraph exceeded the e-node limit.
  TimeLimit,       // Wall-clock time exceeded the time limit.
  Other,           // A user hook returned an error string.
};

struct StopReason {
  StopReasonKind kind = StopReasonKind::Saturated;

  // For IterationLimit: the limit value.
  // For NodeLimit: the current e-node count.
  std::size_t limit_value = 0;

  // For TimeLimit: elapsed seconds.
  double elapsed_seconds = 0.0;

  // For Other: the user-supplied message.
  std::string message;

  static StopReason Saturated() {
    return StopReason{StopReasonKind::Saturated};
  }
  static StopReason IterationLimit(std::size_t limit) {
    StopReason r{StopReasonKind::IterationLimit};
    r.limit_value = limit;
    return r;
  }
  static StopReason NodeLimit(std::size_t nodes) {
    StopReason r{StopReasonKind::NodeLimit};
    r.limit_value = nodes;
    return r;
  }
  static StopReason TimeLimit(double secs) {
    StopReason r{StopReasonKind::TimeLimit};
    r.elapsed_seconds = secs;
    return r;
  }
  static StopReason Other(std::string msg) {
    StopReason r{StopReasonKind::Other};
    r.message = std::move(msg);
    return r;
  }

  std::string toString() const {
    switch (kind) {
      case StopReasonKind::Saturated:
        return "Saturated";
      case StopReasonKind::IterationLimit:
        return "IterationLimit(" + std::to_string(limit_value) + ")";
      case StopReasonKind::NodeLimit:
        return "NodeLimit(" + std::to_string(limit_value) + ")";
      case StopReasonKind::TimeLimit: {
        std::ostringstream oss;
        oss << "TimeLimit(" << elapsed_seconds << "s)";
        return oss.str();
      }
      case StopReasonKind::Other:
        return "Other(" + message + ")";
    }
    return "Unknown";
  }
};

// ---------------------------------------------------------------------------
// Iteration: per-iteration data accumulated during the runner loop.
//
// Mirrors egg::Iteration (without IterData generic param for simplicity).
//
// Diagnostic guide:
//   - Check `applied` to see which rules fired and how many merges each
//     produced.  Rules that produced zero merges do not appear in the map.
//   - Sum `applied[name]` across all iterations in `runner.iterations` to
//     compute a rule's total merge contribution.
//   - If `applied` is empty on every iteration before `has_stop_reason` is
//     set to Saturated, the e-graph was already fully saturated on entry.
//   - `has_stop_reason` is true only on the last (terminal) iteration.
// ---------------------------------------------------------------------------

struct Iteration {
  std::size_t egraph_nodes = 0;    // memo size at start of iteration
  std::size_t egraph_classes = 0;  // class count at start of iteration

  // rule_name -> number of new merges this iteration.
  // Only rules that produced at least one merge appear as keys.
  std::unordered_map<std::string, std::size_t> applied;

  double search_time = 0.0;   // seconds in search phase
  double apply_time = 0.0;    // seconds in apply phase
  double rebuild_time = 0.0;  // seconds in rebuild phase
  double total_time = 0.0;    // total iteration time

  // Whether the runner stopped after this iteration, and why.
  bool has_stop_reason = false;
  StopReason stop_reason;
};

// ---------------------------------------------------------------------------
// Report: summary across all iterations, produced by runner.report().
//
// Mirrors egg::Report.
// ---------------------------------------------------------------------------

struct Report {
  std::size_t iterations = 0;
  StopReason stop_reason;
  std::size_t egraph_nodes = 0;    // final memo size
  std::size_t egraph_classes = 0;  // final class count
  double total_time = 0.0;
  double search_time = 0.0;
  double apply_time = 0.0;
  double rebuild_time = 0.0;

  std::string toString() const {
    std::ostringstream oss;
    oss << "Runner report:\n";
    oss << "  Stop reason: " << stop_reason.toString() << "\n";
    oss << "  Iterations: " << iterations << "\n";
    oss << "  Egraph nodes: " << egraph_nodes << "\n";
    oss << "  Egraph classes: " << egraph_classes << "\n";
    oss << "  Total time: " << total_time << "s\n";
    oss << "  Search time: " << search_time << "s\n";
    oss << "  Apply time: " << apply_time << "s\n";
    oss << "  Rebuild time: " << rebuild_time << "s\n";
    return oss.str();
  }
};

// ---------------------------------------------------------------------------
// RewriteScheduler<Op, Analysis, OpHash>: interface for controlling which
// rules fire and when.
//
// Mirrors egg::RewriteScheduler trait.
// ---------------------------------------------------------------------------

template <typename Op, typename Analysis = NoAnalysis,
          typename OpHash = std::hash<Op>>
class RewriteScheduler {
 public:
  using Graph = EGraph<Op, Analysis, OpHash>;
  using Rule = Rewrite<Op, Analysis, OpHash>;

  virtual ~RewriteScheduler() = default;

  // Called after each iteration to check if saturation is allowed.
  // Return true if saturation can be declared.
  // Default: always true.
  virtual bool canStop(std::size_t /*iteration*/) { return true; }

  // Search a single rewrite rule. Returns matches.
  // Default: delegates to rewrite.search().
  virtual std::vector<SearchMatches<Op>> searchRewrite(
      std::size_t /*iteration*/, Graph& egraph, const Rule& rewrite) {
    return rewrite.search(egraph);
  }

  // Apply a single rewrite rule with given matches. Returns merge count.
  // Default: delegates to rewrite.apply().
  virtual std::size_t applyRewrite(
      std::size_t /*iteration*/, Graph& egraph, const Rule& rewrite,
      const std::vector<SearchMatches<Op>>& matches) {
    return rewrite.apply(egraph, matches);
  }
};

// ---------------------------------------------------------------------------
// SimpleScheduler: passthrough scheduler with no rule management.
//
// Mirrors egg::SimpleScheduler.
// ---------------------------------------------------------------------------

template <typename Op, typename Analysis = NoAnalysis,
          typename OpHash = std::hash<Op>>
class SimpleScheduler : public RewriteScheduler<Op, Analysis, OpHash> {
 public:
  // All defaults — no customization.
};

// ---------------------------------------------------------------------------
// BackoffScheduler: exponential backoff scheduler.
//
// Mirrors egg::BackoffScheduler.
//
// If a rule produces more matches than a threshold, it is "banned" for
// some number of iterations.  Each subsequent ban doubles both the
// threshold and the ban duration.
// ---------------------------------------------------------------------------

template <typename Op, typename Analysis = NoAnalysis,
          typename OpHash = std::hash<Op>>
class BackoffScheduler : public RewriteScheduler<Op, Analysis, OpHash> {
 public:
  using Graph = EGraph<Op, Analysis, OpHash>;
  using Rule = Rewrite<Op, Analysis, OpHash>;

  explicit BackoffScheduler(std::size_t initial_match_limit = 1000,
                            std::size_t initial_ban_length = 5)
      : default_match_limit_(initial_match_limit),
        default_ban_length_(initial_ban_length) {}

  // Set a per-rule match limit override.
  void ruleMatchLimit(const std::string& name, std::size_t limit) {
    getOrCreate(name).match_limit = limit;
  }

  // Set a per-rule ban length override.
  void ruleBanLength(const std::string& name, std::size_t length) {
    getOrCreate(name).ban_length = length;
  }

  // Prevent a specific rule from ever being banned.
  void doNotBan(const std::string& name) {
    getOrCreate(name).match_limit = std::numeric_limits<std::size_t>::max();
  }

  bool canStop(std::size_t iteration) override {
    // If any rule is currently banned, we cannot declare saturation.
    for (const auto& entry : stats_) {
      if (entry.second.banned_until > iteration) {
        return false;
      }
    }
    return true;
  }

  std::vector<SearchMatches<Op>> searchRewrite(std::size_t iteration,
                                               Graph& egraph,
                                               const Rule& rewrite) override {
    auto& st = getOrCreate(rewrite.name());

    // If the rule is currently banned, skip it.
    if (iteration < st.banned_until) {
      return {};
    }

    // Compute the threshold: match_limit << times_banned (exponential).
    std::size_t threshold = st.match_limit;
    if (st.times_banned < 64) {
      // Guard against overflow.
      std::size_t shift = std::size_t(1) << st.times_banned;
      if (threshold <= std::numeric_limits<std::size_t>::max() / shift) {
        threshold *= shift;
      } else {
        threshold = std::numeric_limits<std::size_t>::max();
      }
    } else {
      threshold = std::numeric_limits<std::size_t>::max();
    }

    // Search.
    auto matches = rewrite.search(egraph);

    // Count total matches.
    std::size_t total = 0;
    for (const auto& sm : matches) {
      total += sm.substs.size();
    }

    if (total > threshold) {
      // Ban the rule.
      std::size_t ban_len = st.ban_length;
      if (st.times_banned < 64) {
        std::size_t shift = std::size_t(1) << st.times_banned;
        if (ban_len <= std::numeric_limits<std::size_t>::max() / shift) {
          ban_len *= shift;
        } else {
          ban_len = std::numeric_limits<std::size_t>::max() - iteration;
        }
      } else {
        ban_len = std::numeric_limits<std::size_t>::max() - iteration;
      }
      st.times_banned += 1;
      st.banned_until = iteration + ban_len;
      return {};  // discard matches
    }

    st.times_applied += 1;
    return matches;
  }

 private:
  struct RuleStats {
    std::size_t times_applied = 0;
    std::size_t banned_until = 0;
    std::size_t times_banned = 0;
    std::size_t match_limit = 0;
    std::size_t ban_length = 0;
  };

  RuleStats& getOrCreate(const std::string& name) {
    auto it = stats_.find(name);
    if (it != stats_.end()) {
      return it->second;
    }
    RuleStats st;
    st.match_limit = default_match_limit_;
    st.ban_length = default_ban_length_;
    auto result = stats_.emplace(name, st);
    return result.first->second;
  }

  std::size_t default_match_limit_;
  std::size_t default_ban_length_;
  std::unordered_map<std::string, RuleStats> stats_;
};

// ---------------------------------------------------------------------------
// Runner<Op, Analysis, OpHash>: iterative equality saturation driver.
//
// Mirrors egg::Runner.
//
// The runner owns the e-graph and applies a set of rewrite rules in a
// loop until saturation, an iteration/node/time limit is reached, or a
// user hook stops the run.
//
// Usage:
//   Runner<Symbol> runner;
//   runner.addExpr(expr);
//   runner.withIterLimit(30);
//   runner.run(rules);
//   auto report = runner.report();
// ---------------------------------------------------------------------------

template <typename Op, typename Analysis = NoAnalysis,
          typename OpHash = std::hash<Op>>
class Runner {
 public:
  using Graph = EGraph<Op, Analysis, OpHash>;
  using Rule = Rewrite<Op, Analysis, OpHash>;
  using Clock = std::chrono::steady_clock;
  using TimePoint = std::chrono::time_point<Clock>;
  using Scheduler = RewriteScheduler<Op, Analysis, OpHash>;
  using Hook = std::function<bool(Runner&, std::string&)>;

  // -- Construction -------------------------------------------------------

  explicit Runner(Analysis analysis = Analysis())
      : egraph(std::move(analysis)),
        scheduler_(std::make_unique<SimpleScheduler<Op, Analysis, OpHash>>()) {}

  // Builder-style configuration.

  Runner& withIterLimit(std::size_t limit) {
    iter_limit_ = limit;
    return *this;
  }

  Runner& withNodeLimit(std::size_t limit) {
    node_limit_ = limit;
    return *this;
  }

  Runner& withTimeLimit(double seconds) {
    time_limit_seconds_ = seconds;
    return *this;
  }

  Runner& withScheduler(std::unique_ptr<Scheduler> scheduler) {
    if (!scheduler) {
      throw std::invalid_argument(
          "Runner::withScheduler: scheduler must not be null");
    }
    scheduler_ = std::move(scheduler);
    return *this;
  }

  // Add a hook that runs at the start of each iteration (before rewrites).
  // The hook receives a mutable reference to the runner.
  // It should return true to continue, or false to stop (setting message).
  Runner& withHook(Hook hook) {
    hooks_.push_back(std::move(hook));
    return *this;
  }

  // Add an expression to the e-graph and record its root id.
  Id addExpr(const RecExpr<Op>& expr) {
    Id root = egraph.addExpr(expr);
    roots.push_back(root);
    return root;
  }

  // -- Running ------------------------------------------------------------

  // Run equality saturation with the given rewrite rules.
  // After run() returns, stop_reason is set and iterations is populated.
  void run(const std::vector<Rule>& rules) {
    std::vector<const Rule*> ptrs;
    ptrs.reserve(rules.size());
    for (const auto& r : rules) {
      ptrs.push_back(&r);
    }
    runImpl(ptrs);
  }

  // Overload taking a vector of pointers.
  void run(const std::vector<const Rule*>& rules) { runImpl(rules); }

  // -- Results ------------------------------------------------------------

  Report report() const {
    Report r;
    r.iterations = iterations.size();
    if (has_stop_reason) {
      r.stop_reason = stop_reason;
    }
    r.egraph_nodes = egraph.memoSize();
    r.egraph_classes = egraph.classCount();

    for (const auto& iter : iterations) {
      r.total_time += iter.total_time;
      r.search_time += iter.search_time;
      r.apply_time += iter.apply_time;
      r.rebuild_time += iter.rebuild_time;
    }
    return r;
  }

  // -- Public state -------------------------------------------------------

  Graph egraph;
  std::vector<Iteration> iterations;
  std::vector<Id> roots;
  bool has_stop_reason = false;
  StopReason stop_reason;

 private:
  // -- Limits & config ----------------------------------------------------

  std::size_t iter_limit_ = 30;
  std::size_t node_limit_ = 10000;
  double time_limit_seconds_ = 5.0;
  std::unique_ptr<Scheduler> scheduler_;
  std::vector<Hook> hooks_;
  TimePoint start_time_;
  bool started_ = false;

  // -- Helpers ------------------------------------------------------------

  double elapsedSeconds() const {
    if (!started_) return 0.0;
    auto now = Clock::now();
    return std::chrono::duration<double>(now - start_time_).count();
  }

  // Check limits. Returns true if a limit is hit (and sets stop info).
  bool checkLimits(StopReason& reason) {
    // Time limit.
    if (started_) {
      double elapsed = elapsedSeconds();
      if (elapsed > time_limit_seconds_) {
        reason = StopReason::TimeLimit(elapsed);
        return true;
      }
    }

    // Node limit.
    std::size_t nodes = egraph.memoSize();
    if (nodes > node_limit_) {
      reason = StopReason::NodeLimit(nodes);
      return true;
    }

    // Iteration limit.
    if (iterations.size() >= iter_limit_) {
      reason = StopReason::IterationLimit(iter_limit_);
      return true;
    }

    return false;
  }

  // -- Main loop ----------------------------------------------------------

  void runImpl(const std::vector<const Rule*>& rules) {
    // Reset per-run state so the runner is reusable.
    iterations.clear();
    has_stop_reason = false;
    stop_reason = StopReason::Saturated();  // safe default
    started_ = false;

    // Initial rebuild.
    egraph.rebuild();

    while (true) {
      Iteration iter = runOne(rules);
      bool should_stop = iter.has_stop_reason;
      StopReason reason = iter.stop_reason;
      iterations.push_back(std::move(iter));

      if (should_stop) {
        has_stop_reason = true;
        stop_reason = reason;
        break;
      }
    }
  }

  Iteration runOne(const std::vector<const Rule*>& rules) {
    // Lazy start time.
    if (!started_) {
      start_time_ = Clock::now();
      started_ = true;
    }

    Iteration iter;
    auto iter_start = Clock::now();

    // Check limits first.
    StopReason limit_reason;
    if (checkLimits(limit_reason)) {
      iter.egraph_nodes = egraph.memoSize();
      iter.egraph_classes = egraph.classCount();
      iter.has_stop_reason = true;
      iter.stop_reason = limit_reason;
      iter.total_time =
          std::chrono::duration<double>(Clock::now() - iter_start).count();
      return iter;
    }

    // Record e-graph size at start.
    iter.egraph_nodes = egraph.memoSize();
    iter.egraph_classes = egraph.classCount();

    // Hooks phase.
    std::size_t nodes_before_hooks = egraph.memoSize();
    std::size_t classes_before_hooks = egraph.classCount();
    for (auto& hook : hooks_) {
      std::string msg;
      bool ok = hook(*this, msg);
      if (!ok) {
        iter.has_stop_reason = true;
        iter.stop_reason = StopReason::Other(std::move(msg));
        iter.total_time =
            std::chrono::duration<double>(Clock::now() - iter_start).count();
        return iter;
      }
    }
    std::size_t nodes_after_hooks = egraph.memoSize();
    std::size_t classes_after_hooks = egraph.classCount();

    // Search phase.
    auto search_start = Clock::now();
    std::size_t current_iteration = iterations.size();

    // Per-rule matches.
    std::vector<std::vector<SearchMatches<Op>>> all_matches;
    all_matches.reserve(rules.size());
    for (const auto* rule : rules) {
      auto matches =
          scheduler_->searchRewrite(current_iteration, egraph, *rule);
      all_matches.push_back(std::move(matches));
    }
    auto search_end = Clock::now();
    iter.search_time =
        std::chrono::duration<double>(search_end - search_start).count();

    // Apply phase.
    auto apply_start = Clock::now();
    for (std::size_t i = 0; i < rules.size(); ++i) {
      const auto& matches = all_matches[i];
      if (matches.empty()) continue;

      std::size_t merges = scheduler_->applyRewrite(current_iteration, egraph,
                                                    *rules[i], matches);
      if (merges > 0) {
        iter.applied[rules[i]->name()] = merges;
      }
    }
    auto apply_end = Clock::now();
    iter.apply_time =
        std::chrono::duration<double>(apply_end - apply_start).count();

    // Rebuild phase.
    auto rebuild_start = Clock::now();
    egraph.rebuild();
    auto rebuild_end = Clock::now();
    iter.rebuild_time =
        std::chrono::duration<double>(rebuild_end - rebuild_start).count();

    // Saturation check.
    // Saturated if:
    //   1. No rules produced new merges this iteration.
    //   2. The scheduler agrees (canStop).
    //   3. Hooks didn't change the e-graph size.
    //   4. The e-graph size didn't change from start to finish (catches
    //      rebuild-induced merges and any deferred effects).
    bool no_new_applications = iter.applied.empty();
    bool scheduler_allows = scheduler_->canStop(current_iteration);
    bool hooks_unchanged = (nodes_after_hooks == nodes_before_hooks &&
                            classes_after_hooks == classes_before_hooks);
    bool size_unchanged = (egraph.memoSize() == iter.egraph_nodes &&
                           egraph.classCount() == iter.egraph_classes);

    if (no_new_applications && scheduler_allows && hooks_unchanged &&
        size_unchanged) {
      iter.has_stop_reason = true;
      iter.stop_reason = StopReason::Saturated();
    }

    // Check limits again after work.
    if (!iter.has_stop_reason) {
      StopReason post_limit;
      if (checkLimits(post_limit)) {
        iter.has_stop_reason = true;
        iter.stop_reason = post_limit;
      }
    }

    iter.total_time =
        std::chrono::duration<double>(Clock::now() - iter_start).count();
    return iter;
  }
};

}  // namespace egraph
}  // namespace nndeploy

#endif
