// FalseHorizon's equation language: the formulas in a preset ("zoom = 1.02 + 0.05 * bass"). Port of
// js/falsehorizon-expr.js, which stays the reference (tests/parity/ runs both on the same
// formulas). A formula compiles to a small stack program that cannot call, allocate or loop, so
// presets from anyone are safe to run. Unset names read as 0, dividing by zero gives 0, results
// that are not numbers become 0 and huge ones are clamped to +-1e6; Limits refuse oversized or
// deeply nested formulas. Error positions count bytes (same as JS for the ASCII presets use).
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace fh::expr {

struct Limits {
  static constexpr int source = 6000, tokens = 3000, statements = 250, depth = 24, names = 120,
                       name_length = 32;
};

// Variable names to slots in the array a program runs over. One Symbols is shared by every formula
// of a preset, so they all see the same variables.
class Symbols {
 public:
  int slot(std::string_view
               name);  // the slot of `name`, adding it if it is new (names are lower-cased first)
  int find(std::string_view name) const;  // -1 if there is no such name
  std::size_t size() const { return names_.size(); }
  const std::string& name(int slot) const { return names_[static_cast<std::size_t>(slot)]; }

 private:
  std::unordered_map<std::string, int> index_;
  std::vector<std::string> names_;
};

using Rng = std::function<double()>;  // uniform in [0, 1); tests give it a fixed sequence

struct Options {
  Rng rng;  // empty: a thread-local Mersenne Twister
};

enum class Op : std::uint8_t;  // the bytecode; see expr.cpp
struct Instr {
  Op op{};
  std::int32_t arg = 0;  // a slot, a function number, or how many instructions to skip
  double num = 0;
};

class Program {
 public:
  // Runs every statement over `vars` (slot -> value). vars.size() must be at least slot_count(),
  // else std::length_error.
  void run(std::span<double> vars) const;

  [[nodiscard]] std::size_t slot_count() const { return slot_count_; }  // the highest slot used + 1
  [[nodiscard]] const std::vector<std::string>& reads() const {
    return names_;
  }  // every name used, read or written, in order of appearance
  [[nodiscard]] const std::vector<std::string>& writes() const {
    return writes_;
  }  // the names assigned
  [[nodiscard]] std::size_t statements() const { return statements_; }

 private:
  friend struct Compiler;
  std::vector<Instr> code_;
  std::vector<std::string> names_, writes_;
  std::size_t slot_count_ = 0, statements_ = 0, max_stack_ = 0;
  Rng rng_;
};

struct CompileResult {
  bool ok = false;
  std::string error;  // a sentence a person can read, ending "at character N"
  Program program;
};

CompileResult compile(std::string_view source, Symbols& symbols, const Options& options = {});

// "" if the formula is acceptable, else the error message (for checking a formula that came from a
// file).
std::string check(std::string_view source);

const std::vector<std::string>& function_names();

// The few helpers the evaluator is built from, exposed so tests can pin them down.
double fix(double x);  // not a number -> 0; beyond +-1e6 -> clamped
bool truthy(double x);  // |x| > 1e-5

}  // namespace fh::expr
