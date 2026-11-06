// C++ port of js/falsehorizon-expr.js (rules in include/fh/expr.hpp). The parser mirrors the
// JavaScript one rule for rule, including the order of checks, so the same text gives the same
// error message. The reference's evaluation order is kept on purpose because rand() makes it
// visible: a / b and a % b work out b first and skip a when b is 0; a && b, a || b and if(c, x, y)
// only work out what they need.
#include "fh/expr.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <memory>
#include <random>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace fh::expr {

double fix(double x) {
  if (!std::isfinite(x)) {
    return 0;
  }
  return x > 1e6 ? 1e6 : x < -1e6 ? -1e6 : x;
}
bool truthy(double x) { return std::fabs(x) > 1e-5; }

namespace {

// JavaScript's Math.min / Math.max / Math.pow differ from the C++ ones when a value is not a number
// (NaN wins in JavaScript), and pow(1, infinity) is NaN there. The results go through fix(), so
// what matters is only whether they are NaN, and this keeps that the same.
double js_min(double a, double b) {
  return (std::isnan(a) || std::isnan(b)) ? std::numeric_limits<double>::quiet_NaN()
                                          : std::min(a, b);
}
double js_max(double a, double b) {
  return (std::isnan(a) || std::isnan(b)) ? std::numeric_limits<double>::quiet_NaN()
                                          : std::max(a, b);
}
double js_pow(double a, double b) {
  if (std::isnan(b)) {
    return std::numeric_limits<double>::quiet_NaN();
  }
  if (!std::isfinite(b) && std::fabs(a) == 1) {
    return std::numeric_limits<double>::quiet_NaN();
  }
  return std::pow(a, b);
}
double or0(double x) { return (x == 0 || std::isnan(x)) ? 0 : x; }  // JavaScript's  x || 0

// JavaScript's Number -> text, because error messages print numbers that way ("unexpected "1.5"").
// Only non-negative numbers are ever printed.
std::string js_number_string(double x) {
  if (std::isnan(x)) {
    return "NaN";
  }
  if (std::isinf(x)) {
    return "Infinity";
  }
  if (x == 0) {
    return "0";
  }
  std::array<char, 64> buf{};
  const auto r = std::to_chars(
      buf.data(), buf.data() + buf.size(), x,
      std::chars_format::scientific);  // shortest digits that read back exactly: d.ddde+XX
  const std::string sci(buf.data(), r.ptr);
  const auto epos = sci.find('e');
  std::string digits;
  const std::string mant = sci.substr(0, epos);
  for (const char c : mant) {
    if (c != '.') {
      digits += c;
    }
  }
  const int k = static_cast<int>(digits.size());
  const int n = std::atoi(sci.c_str() + epos + 1) + 1;  // value = 0.DIGITS * 10^n
  if (k <= n && n <= 21) {
    return digits + std::string(static_cast<std::size_t>(n - k), '0');
  }
  if (0 < n && n <= 21) {
    return digits.substr(0, static_cast<std::size_t>(n)) + "." +
           digits.substr(static_cast<std::size_t>(n));
  }
  if (-6 < n && n <= 0) {
    return "0." + std::string(static_cast<std::size_t>(-n), '0') + digits;
  }
  const int e = n - 1;
  std::string out = digits.substr(0, 1);
  if (k > 1) {
    out += "." + digits.substr(1);
  }
  return out + "e" + (e < 0 ? "-" : "+") + std::to_string(std::abs(e));
}

double parse_number(std::string_view text) {
  double v = 0;
  const auto r = std::from_chars(text.data(), text.data() + text.size(), v);
  if (r.ec == std::errc::result_out_of_range) {
    return std::strtod(std::string(text).c_str(),
                       nullptr);  // overflow -> infinity, underflow -> 0, like JavaScript
  }
  return v;
}

enum Fn : int {
  FSin,
  FCos,
  FTan,
  FAsin,
  FAcos,
  FAtan,
  FAtan2,
  FSqrt,
  FAbs,
  FExp,
  FLog,
  FLog10,
  FBand,
  FBor,
  FBnot,
  FFloor,
  FCeil,
  FInt,
  FFrac,
  FSign,
  FSqr,
  FMin,
  FMax,
  FPow,
  FMod,
  FAbove,
  FBelow,
  FEqual,
  FSigmoid,
  FClamp,
  FMix,
  FSmoothstep,
  FRand,
  FIf,
};
struct FnInfo {
  const char* name;
  int arity;
  Fn id;
};
// The order is the JavaScript object's, so functionNames() lists them the same way.
constexpr FnInfo k_fns[] = {
    {"sin", 1, FSin},         {"cos", 1, FCos},
    {"tan", 1, FTan},         {"asin", 1, FAsin},
    {"acos", 1, FAcos},       {"atan", 1, FAtan},
    {"atan2", 2, FAtan2},     {"sqrt", 1, FSqrt},
    {"abs", 1, FAbs},         {"exp", 1, FExp},
    {"log", 1, FLog},         {"log10", 1, FLog10},
    {"band", 2, FBand},       {"bor", 2, FBor},
    {"bnot", 1, FBnot},       {"floor", 1, FFloor},
    {"ceil", 1, FCeil},       {"int", 1, FInt},
    {"frac", 1, FFrac},       {"sign", 1, FSign},
    {"sqr", 1, FSqr},         {"min", 2, FMin},
    {"max", 2, FMax},         {"pow", 2, FPow},
    {"mod", 2, FMod},         {"above", 2, FAbove},
    {"below", 2, FBelow},     {"equal", 2, FEqual},
    {"sigmoid", 2, FSigmoid}, {"clamp", 3, FClamp},
    {"mix", 3, FMix},         {"smoothstep", 3, FSmoothstep},
    {"rand", 1, FRand},       {"if", 3, FIf},
};
const FnInfo* find_fn(std::string_view name) {
  for (const auto& f : k_fns) {
    if (name == f.name) {
      return &f;
    }
  }
  return nullptr;
}

double call1(int f, double a) {
  switch (f) {
    case FSin:
      return std::sin(a);
    case FCos:
      return std::cos(a);
    case FTan:
      return std::tan(a);
    case FAsin:
      return std::asin(a);
    case FAcos:
      return std::acos(a);
    case FAtan:
      return std::atan(a);
    case FSqrt:
      return std::sqrt(std::fabs(a));
    case FAbs:
      return std::fabs(a);
    case FExp:
      return std::exp(js_min(a, 50));
    case FLog: {
      const double m = std::fabs(a);
      return std::log(m == 0 || std::isnan(m) ? 1e-9 : m);
    }
    case FLog10: {
      const double m = std::fabs(a);
      return std::log10(m == 0 || std::isnan(m) ? 1e-9 : m);
    }
    case FBnot:
      return truthy(a) ? 0 : 1;
    case FFloor:
      return std::floor(a);
    case FCeil:
      return std::ceil(a);
    case FInt:
      return std::trunc(a);
    case FFrac:
      return a - std::floor(a);
    case FSign:
      return a > 0 ? 1 : a < 0 ? -1 : a;
    case FSqr:
      return a * a;
    default:
      return 0;
  }
}
double call2(int f, double a, double b) {
  switch (f) {
    case FAtan2:
      return std::atan2(a, b);
    case FBand:
      return truthy(a) && truthy(b) ? 1 : 0;
    case FBor:
      return truthy(a) || truthy(b) ? 1 : 0;
    case FMin:
      return js_min(a, b);
    case FMax:
      return js_max(a, b);
    case FPow:
      return js_pow(a, b);
    case FMod:
      return b == 0 ? 0 : a - b * std::floor(a / b);
    case FAbove:
      return a > b ? 1 : 0;
    case FBelow:
      return a < b ? 1 : 0;
    case FEqual:
      return std::fabs(a - b) < 1e-5 ? 1 : 0;
    case FSigmoid:
      return 1 / (1 + std::exp(-js_max(-50, js_min(50, a * b))));
    default:
      return 0;
  }
}
double call3(int f, double a, double b, double c) {
  switch (f) {
    case FClamp:
      return js_min(js_max(a, js_min(b, c)), js_max(b, c));
    case FMix:
      return a + (b - a) * c;
    case FSmoothstep: {
      if (a == b) {
        return c < a ? 0 : 1;
      }
      const double t = js_min(1, js_max(0, (c - a) / (b - a)));
      return t * t * (3 - 2 * t);
    }
    default:
      return 0;
  }
}

}  // namespace

enum class Op : std::uint8_t {
  Push,
  Load,
  PushRng,
  Neg,
  Not,
  ToBool,
  Add,
  Sub,
  Mul,
  Pow,
  Eq,
  Ne,
  Lt,
  Gt,
  Le,
  Ge,
  DivPre,
  DivPost,
  ModPre,
  ModPost,
  JumpIfTrue1,
  JumpIfFalse0,
  JumpIfFalse,
  Jump,
  Call1,
  Call2,
  Call3,
  Store,
  StoreAdd,
  StoreSub,
  StoreMul,
  StoreDiv,
};

int Symbols::slot(std::string_view name) {
  std::string key(name);
  for (char& c : key) {
    if (c >= 'A' && c <= 'Z') {
      c = static_cast<char>(c - 'A' + 'a');
    }
  }
  const auto it = index_.find(key);
  if (it != index_.end()) {
    return it->second;
  }
  const int s = static_cast<int>(names_.size());
  index_.emplace(key, s);
  names_.push_back(std::move(key));
  return s;
}
int Symbols::find(std::string_view name) const {
  std::string key(name);
  for (char& c : key) {
    if (c >= 'A' && c <= 'Z') {
      c = static_cast<char>(c - 'A' + 'a');
    }
  }
  const auto it = index_.find(key);
  return it == index_.end() ? -1 : it->second;
}

namespace {

struct ExprError : std::runtime_error {
  using std::runtime_error::runtime_error;
};

enum class Tok { Num, Id, Op, End };
struct Token {
  Tok type{};
  std::string text;
  double num = 0;
  std::size_t pos = 0;
};

const char* const k_operators[] = {
    "<=", ">=", "==", "!=", "&&", "||", "+=", "-=", "*=", "/=", "+", "-",
    "*",  "/",  "%",  "^",  "(",  ")",  ",",  ";",  "=",  "<",  ">", "!"};

bool is_digit(char c) { return c >= '0' && c <= '9'; }
bool is_name_start(char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_'; }
bool is_name_char(char c) { return is_name_start(c) || is_digit(c); }

// How much of `rest` is a number: digits[.digits] or .digits, then an optional exponent. 0 if it is
// not one.
std::size_t number_length(std::string_view rest) {
  std::size_t p = 0;
  while (p < rest.size() && is_digit(rest[p])) {
    p++;
  }
  if (p > 0) {
    if (p < rest.size() && rest[p] == '.') {
      p++;
      while (p < rest.size() && is_digit(rest[p])) {
        p++;
      }
    }
  } else if (rest.size() > 1 && rest[0] == '.' && is_digit(rest[1])) {
    p = 1;
    while (p < rest.size() && is_digit(rest[p])) {
      p++;
    }
  } else {
    return 0;
  }
  if (p < rest.size() && (rest[p] == 'e' || rest[p] == 'E')) {
    std::size_t q = p + 1;
    if (q < rest.size() && (rest[q] == '+' || rest[q] == '-')) {
      q++;
    }
    if (q < rest.size() && is_digit(rest[q])) {
      while (q < rest.size() && is_digit(rest[q])) {
        q++;
      }
      p = q;
    }
  }
  return p;
}

std::vector<Token> tokenize(std::string_view src) {
  std::vector<Token> tokens;
  std::size_t i = 0;
  while (i < src.size()) {
    const char ch = src[i];
    if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r') {
      i++;
      continue;
    }
    const std::string_view rest = src.substr(i, 40);  // the reference looks at 40 characters at a
                                                      // time; a longer number or name is cut there
    if (const std::size_t n = number_length(rest)) {
      tokens.push_back(
          {Tok::Num, std::string(rest.substr(0, n)), parse_number(rest.substr(0, n)), i});
      i += n;
    } else if (is_name_start(ch)) {
      std::size_t len = 1;
      while (len < rest.size() && is_name_char(rest[len])) {
        len++;
      }
      if (len > static_cast<std::size_t>(Limits::name_length)) {
        throw ExprError("the name \"" + std::string(rest.substr(0, 12)) +
                        "...\" is too long (at character " + std::to_string(i + 1) + ")");
      }
      std::string name(rest.substr(0, len));
      for (char& c : name) {
        if (c >= 'A' && c <= 'Z') {
          c = static_cast<char>(c - 'A' + 'a');
        }
      }
      tokens.push_back({Tok::Id, std::move(name), 0, i});
      i += len;
    } else {
      const char* found = nullptr;
      for (const char* op : k_operators) {
        if (rest.substr(0, std::char_traits<char>::length(op)) == op) {
          found = op;
          break;
        }
      }
      if (!found) {
        throw ExprError(std::string("unexpected \"") + ch + "\" at character " +
                        std::to_string(i + 1));
      }
      tokens.push_back({Tok::Op, found, 0, i});
      i += std::char_traits<char>::length(found);
    }
    if (tokens.size() > static_cast<std::size_t>(Limits::tokens)) {
      throw ExprError("this formula is too long");
    }
  }
  return tokens;
}

// A piece of code that leaves one number on the stack. `peak` is the most stack it needs while it
// runs (counted from where it starts). Jumps are relative, so a piece can be moved and copied.
struct Frag {
  std::vector<Instr> ops;
  int peak = 1;
};

Frag one(Op op, std::int32_t arg = 0, double num = 0) {
  Frag f;
  f.ops.push_back({op, arg, num});
  return f;
}
void append(Frag& to, const Frag& from) {
  to.ops.insert(to.ops.end(), from.ops.begin(), from.ops.end());
}
std::int32_t count(const Frag& f) { return static_cast<std::int32_t>(f.ops.size()); }

}  // namespace

struct Compiler {
  std::string_view src;
  std::vector<Token> tokens;
  std::size_t at = 0;
  int depth = 0;
  Symbols& symbols;
  std::vector<std::string> names, writes;
  std::unordered_set<std::string> name_set, write_set;
  std::size_t slot_count = 0;
  Rng rng;

  explicit Compiler(std::string_view s, Symbols& sym) : src(s), tokens(tokenize(s)), symbols(sym) {}

  const Token& peek() const {
    static thread_local Token end;
    if (at < tokens.size()) {
      return tokens[at];
    }
    end = {Tok::End, "", 0, src.size()};
    return end;
  }
  [[noreturn]] void fail(const std::string& what, const Token* tok = nullptr) const {
    throw ExprError(what + " at character " + std::to_string((tok ? tok->pos : peek().pos) + 1));
  }
  bool is_op(const char* v) const {
    const Token& t = peek();
    return t.type == Tok::Op && t.text == v;
  }
  void take(const char* v) {
    if (!is_op(v)) {
      fail(std::string("expected \"") + v + "\"");
    }
    at++;
  }
  void deeper() {
    if (++depth > Limits::depth) {
      fail("this formula is nested too deeply");
    }
  }
  static std::string show(const Token& t) {
    return t.type == Tok::Num ? js_number_string(t.num) : t.text;
  }

  void note(const std::string& name) {
    if (name.starts_with("__") || name == "constructor" || name == "prototype") {
      fail("\"" + name + "\" is not a name you can use");
    }
    if (name_set.insert(name).second) {
      names.push_back(name);
    }
    if (name_set.size() > static_cast<std::size_t>(Limits::names)) {
      fail("more than " + std::to_string(Limits::names) + " different names");
    }
  }
  int slot_of(const std::string& name) {
    const int s = symbols.slot(name);
    slot_count = std::max(slot_count, static_cast<std::size_t>(s) + 1);
    return s;
  }

  // precedence, loosest first: ||  &&  comparisons  + -  * / %  unary - !  ^ (right to left)  atoms
  Frag or_expr() {
    Frag a = and_expr();
    while (is_op("||")) {
      at++;
      const Frag r = and_expr();
      Frag f;
      append(f, a);
      f.ops.push_back({Op::JumpIfTrue1, count(r) + 1, 0});
      append(f, r);
      f.ops.push_back({Op::ToBool, 0, 0});
      f.peak = std::max(a.peak, r.peak);
      a = std::move(f);
    }
    return a;
  }
  Frag and_expr() {
    Frag a = compare();
    while (is_op("&&")) {
      at++;
      const Frag r = compare();
      Frag f;
      append(f, a);
      f.ops.push_back({Op::JumpIfFalse0, count(r) + 1, 0});
      append(f, r);
      f.ops.push_back({Op::ToBool, 0, 0});
      f.peak = std::max(a.peak, r.peak);
      a = std::move(f);
    }
    return a;
  }
  Frag binary(const Frag& l, const Frag& r, Op op) {
    Frag f;
    append(f, l);
    append(f, r);
    f.ops.push_back({op, 0, 0});
    f.peak = std::max(l.peak, 1 + r.peak);
    return f;
  }
  Frag compare() {
    Frag a = sum();
    for (;;) {
      const Token& t = peek();
      if (t.type != Tok::Op) {
        return a;
      }
      Op op{};
      if (t.text == "==") {
        op = Op::Eq;
      } else if (t.text == "!=") {
        op = Op::Ne;
      } else if (t.text == "<") {
        op = Op::Lt;
      } else if (t.text == ">") {
        op = Op::Gt;
      } else if (t.text == "<=") {
        op = Op::Le;
      } else if (t.text == ">=") {
        op = Op::Ge;
      } else {
        return a;
      }
      at++;
      Frag r = sum();
      a = binary(a, r, op);
    }
  }
  Frag sum() {
    Frag a = product();
    while (is_op("+") || is_op("-")) {
      const bool plus = peek().text == "+";
      at++;
      Frag r = product();
      a = binary(a, r, plus ? Op::Add : Op::Sub);
    }
    return a;
  }
  Frag product() {
    Frag a = unary();
    while (is_op("*") || is_op("/") || is_op("%")) {
      const std::string op = peek().text;
      at++;
      Frag r = unary();
      if (op == "*") {
        a = binary(a, r, Op::Mul);
        continue;
      }
      // / and %: the right side first; if it is 0 the answer is 0 and the left side is never worked
      // out (see the note at the top).
      Frag f;
      append(f, r);
      if (op == "/") {
        f.ops.push_back({Op::DivPre, count(a) + 1, 0});
        append(f, a);
        f.ops.push_back({Op::DivPost, 0, 0});
        f.peak = std::max(r.peak, 1 + a.peak);
      } else {
        f.ops.push_back({Op::ModPre, count(a) + 1, 0});
        append(f, a);
        f.ops.push_back({Op::ModPost, 0, 0});
        f.peak = std::max(r.peak, 1 + a.peak);
      }
      a = std::move(f);
    }
    return a;
  }
  Frag unary() {
    if (is_op("-")) {
      at++;
      deeper();
      Frag a = unary();
      depth--;
      a.ops.push_back({Op::Neg, 0, 0});
      return a;
    }
    if (is_op("+")) {
      at++;
      return unary();
    }
    if (is_op("!")) {
      at++;
      deeper();
      Frag a = unary();
      depth--;
      a.ops.push_back({Op::Not, 0, 0});
      return a;
    }
    return power();
  }
  Frag power() {
    Frag base = atom();
    if (!is_op("^")) {
      return base;
    }
    at++;
    deeper();
    Frag exponent = unary();  // right to left, and  2 ^ -1  works
    depth--;
    return binary(base, exponent, Op::Pow);
  }
  Frag atom() {
    const Token t = peek();
    if (t.type == Tok::Num) {
      at++;
      return one(Op::Push, 0, t.num);
    }
    if (is_op("(")) {
      at++;
      deeper();
      Frag a = or_expr();
      depth--;
      take(")");
      return a;
    }
    if (t.type != Tok::Id) {
      fail(t.type == Tok::End ? "the formula ends too soon" : "unexpected \"" + show(t) + "\"");
    }
    at++;
    const FnInfo* fn = find_fn(t.text);
    if (is_op("(")) {
      if (!fn) {
        fail("there is no function called \"" + t.text + "\"", &t);
      }
      at++;
      deeper();
      std::vector<Frag> args;
      if (!is_op(")")) {
        do {
          args.push_back(or_expr());
        } while (is_op(",") && ++at);
      }
      depth--;
      take(")");
      if (std::cmp_not_equal(args.size(), fn->arity)) {
        fail(t.text + "() needs " + std::to_string(fn->arity) + " value" +
                 (fn->arity == 1 ? "" : "s") + ", not " + std::to_string(args.size()),
             &t);
      }
      return call(*fn, args);
    }
    if (fn) {
      fail("\"" + t.text + "\" is a function and needs ( )", &t);
    }
    note(t.text);
    return one(Op::Load, slot_of(t.text));
  }
  Frag call(const FnInfo& fn, std::vector<Frag>& args) {
    Frag f;
    if (fn.id == FIf) {
      f = std::move(args[0]);
      f.ops.push_back({Op::JumpIfFalse, count(args[1]) + 1, 0});
      append(f, args[1]);
      f.ops.push_back({Op::Jump, count(args[2]), 0});
      append(f, args[2]);
      f.peak = std::max({args[0].peak, args[1].peak, args[2].peak});
      return f;
    }
    if (fn.id == FRand) {  // rand(n) = fix(rng() * n), and rng() is called before n is worked out
      f.ops.push_back({Op::PushRng, 0, 0});
      append(f, args[0]);
      f.ops.push_back({Op::Mul, 0, 0});
      f.peak = std::max(1, 1 + args[0].peak);
      return f;
    }
    int peak = 1;
    for (std::size_t i = 0; i < args.size(); i++) {
      append(f, args[i]);
      peak = std::max(peak, static_cast<int>(i) + args[i].peak);
    }
    f.ops.push_back({fn.arity == 1 ? Op::Call1 : fn.arity == 2 ? Op::Call2 : Op::Call3, fn.id, 0});
    f.peak = peak;
    return f;
  }

  void statements(Program& out) {
    std::size_t count = 0;
    int max_stack = 1;
    while (peek().type != Tok::End) {
      if (is_op(";")) {
        at++;
        continue;
      }
      const Token target = peek();
      const Token* op = at + 1 < tokens.size() ? &tokens[at + 1] : nullptr;
      static const char* const assigns[] = {"=", "+=", "-=", "*=", "/="};
      bool is_assign = false;
      if (op && op->type == Tok::Op) {
        for (const char* a : assigns) {
          if (op->text == a) {
            is_assign = true;
          }
        }
      }
      if (target.type != Tok::Id || !is_assign) {
        fail("each statement must look like  name = formula", &target);
      }
      if (find_fn(target.text)) {
        fail("\"" + target.text + "\" is a function name and cannot be set", &target);
      }
      note(target.text);
      if (write_set.insert(target.text).second) {
        writes.push_back(target.text);
      }
      const std::string op_text = op->text;
      at += 2;
      Frag value = or_expr();
      if (!is_op(";") && peek().type != Tok::End) {
        fail("unexpected \"" + show(peek()) + "\"");
      }
      if (count >= static_cast<std::size_t>(Limits::statements)) {
        fail("more than " + std::to_string(Limits::statements) + " statements", &target);
      }
      const Op store = op_text == "="    ? Op::Store
                       : op_text == "+=" ? Op::StoreAdd
                       : op_text == "-=" ? Op::StoreSub
                       : op_text == "*=" ? Op::StoreMul
                                         : Op::StoreDiv;
      value.ops.push_back({store, slot_of(target.text), 0});
      out.code_.insert(out.code_.end(), value.ops.begin(), value.ops.end());
      max_stack = std::max(max_stack, value.peak);
      count++;
    }
    out.statements_ = count;
    out.max_stack_ = static_cast<std::size_t>(max_stack);
    out.slot_count_ = slot_count;
    out.names_ = names;
    out.writes_ = writes;
    out.rng_ = rng;
  }
};

CompileResult compile(std::string_view source, Symbols& symbols, const Options& options) {
  CompileResult result;
  try {
    if (source.size() > static_cast<std::size_t>(Limits::source)) {
      throw ExprError("this formula is longer than " + std::to_string(Limits::source) +
                      " characters");
    }
    Compiler c(source, symbols);
    c.rng = options.rng ? options.rng : [] {
      static thread_local std::mt19937_64 gen{std::random_device{}()};
      return std::uniform_real_distribution<double>(0.0, 1.0)(gen);
    };
    c.statements(result.program);
    result.ok = true;
  } catch (const ExprError& e) {
    result.ok = false;
    result.error = e.what();
    result.program = Program{};
  }
  return result;
}

std::string check(std::string_view source) {
  Symbols scratch;
  const CompileResult r = compile(source, scratch);
  return r.ok ? std::string() : r.error;
}

const std::vector<std::string>& function_names() {
  static const std::vector<std::string> names = [] {
    std::vector<std::string> v;
    for (const auto& f : k_fns) {
      v.emplace_back(f.name);
    }
    return v;
  }();
  return names;
}

void Program::run(std::span<double> vars) const {
  if (vars.size() < slot_count_) {
    throw std::length_error(
        "fh::expr::Program::run: the variable array is smaller than slotCount()");
  }
  double local[64];
  std::unique_ptr<double[]>
      big;  // only a formula with a very long chain of "/" or "%" needs more than 64 places
  double* const base = max_stack_ > 64 ? (big.reset(new double[max_stack_]), big.get()) : local;
  double* sp = base;
  double* const v = vars.data();
  const Instr* const code = code_.data();
  const std::size_t n = code_.size();
  for (std::size_t pc = 0; pc < n;) {
    const Instr& in = code[pc++];
    switch (in.op) {
      case Op::Push:
        *sp++ = in.num;
        break;
      case Op::Load:
        *sp++ = v[in.arg];
        break;
      case Op::PushRng:
        *sp++ = rng_();
        break;
      case Op::Neg:
        sp[-1] = -sp[-1];
        break;
      case Op::Not:
        sp[-1] = truthy(sp[-1]) ? 0 : 1;
        break;
      case Op::ToBool:
        sp[-1] = truthy(sp[-1]) ? 1 : 0;
        break;
      case Op::Add:
        --sp;
        sp[-1] = fix(sp[-1] + sp[0]);
        break;
      case Op::Sub:
        --sp;
        sp[-1] = fix(sp[-1] - sp[0]);
        break;
      case Op::Mul:
        --sp;
        sp[-1] = fix(sp[-1] * sp[0]);
        break;
      case Op::Pow:
        --sp;
        sp[-1] = fix(js_pow(sp[-1], sp[0]));
        break;
      case Op::Eq:
        --sp;
        sp[-1] = std::fabs(sp[-1] - sp[0]) < 1e-5 ? 1 : 0;
        break;
      case Op::Ne:
        --sp;
        sp[-1] = std::fabs(sp[-1] - sp[0]) >= 1e-5 ? 1 : 0;
        break;
      case Op::Lt:
        --sp;
        sp[-1] = sp[-1] < sp[0] ? 1 : 0;
        break;
      case Op::Gt:
        --sp;
        sp[-1] = sp[-1] > sp[0] ? 1 : 0;
        break;
      case Op::Le:
        --sp;
        sp[-1] = sp[-1] <= sp[0] ? 1 : 0;
        break;
      case Op::Ge:
        --sp;
        sp[-1] = sp[-1] >= sp[0] ? 1 : 0;
        break;
      case Op::DivPre:
      case Op::ModPre:
        if (sp[-1] == 0) {
          pc += static_cast<std::size_t>(in.arg);
        }
        break;  // the 0 on the stack is the answer
      case Op::DivPost: {
        const double l = sp[-1];
        const double d = sp[-2];
        --sp;
        sp[-1] = fix(l / d);
        break;
      }
      case Op::ModPost: {
        const double l = sp[-1];
        const double d = sp[-2];
        --sp;
        sp[-1] = fix(l - d * std::floor(l / d));
        break;
      }
      case Op::JumpIfTrue1: {
        const double x = *--sp;
        if (truthy(x)) {
          *sp++ = 1;
          pc += static_cast<std::size_t>(in.arg);
        }
        break;
      }
      case Op::JumpIfFalse0: {
        const double x = *--sp;
        if (!truthy(x)) {
          *sp++ = 0;
          pc += static_cast<std::size_t>(in.arg);
        }
        break;
      }
      case Op::JumpIfFalse: {
        const double x = *--sp;
        if (!truthy(x)) {
          pc += static_cast<std::size_t>(in.arg);
        }
        break;
      }
      case Op::Jump:
        pc += static_cast<std::size_t>(in.arg);
        break;
      case Op::Call1:
        sp[-1] = fix(call1(in.arg, sp[-1]));
        break;
      case Op::Call2:
        --sp;
        sp[-1] = fix(call2(in.arg, sp[-1], sp[0]));
        break;
      case Op::Call3:
        sp -= 2;
        sp[-1] = fix(call3(in.arg, sp[-1], sp[0], sp[1]));
        break;
      case Op::Store:
        v[in.arg] = fix(*--sp);
        break;
      case Op::StoreAdd: {
        const double x = *--sp;
        v[in.arg] = fix(or0(v[in.arg]) + x);
        break;
      }
      case Op::StoreSub: {
        const double x = *--sp;
        v[in.arg] = fix(or0(v[in.arg]) - x);
        break;
      }
      case Op::StoreMul: {
        const double x = *--sp;
        v[in.arg] = fix(or0(v[in.arg]) * x);
        break;
      }
      case Op::StoreDiv: {
        const double d = *--sp;
        v[in.arg] = d == 0 ? 0 : fix(or0(v[in.arg]) / d);
        break;
      }
    }
  }
}

}  // namespace fh::expr
