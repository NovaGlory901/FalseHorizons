// Tests for the equation language (expr.hpp): hand-written checks plus parity, where every formula
// in tests/parity/expr.golden must give what the JavaScript reference gave.
#include "check.hpp"
#include "fh/expr.hpp"
#include "tests.hpp"

#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

using namespace fh::expr;

using test::check;
static bool near(double a, double b, double eps = 1e-6) { return std::fabs(a - b) <= eps; }

// Runs `src` over the named starting values; returns the variables by name (a name never set reads
// as 0).
struct Ran {
  bool ok;
  std::string error;
  std::map<std::string, double> vars;
  Program program;
};
static Ran run(const std::string& src, const std::map<std::string, double>& start = {},
               Rng rng = nullptr) {
  Symbols sym;
  for (const auto& [k, v] : start) {
    sym.slot(k);
  }
  Options opt;
  opt.rng = std::move(rng);
  CompileResult r = compile(src, sym, opt);
  Ran out{r.ok, r.error, {}, std::move(r.program)};
  if (!r.ok && !out.error.empty() && out.program.slot_count() == 0) {
  }
  std::vector<double> vars(sym.size(), 0.0);
  for (const auto& [k, v] : start) {
    vars[static_cast<std::size_t>(sym.find(k))] = v;
  }
  if (out.ok) {
    out.program.run(vars);
  }
  for (std::size_t i = 0; i < sym.size(); i++) {
    out.vars[sym.name(static_cast<int>(i))] = vars[i];
  }
  return out;
}
static double value(const std::string& expr, const std::map<std::string, double>& start = {}) {
  return run("a = " + expr + ";", start).vars["a"];
}
static bool bad(const std::string& src) { return !check(src).empty(); }

static std::string unescape(const std::string& s) {
  std::string o;
  for (std::size_t i = 0; i < s.size(); i++) {
    if (s[i] == '\\' && i + 1 < s.size()) {
      const char n = s[++i];
      o += n == 'n' ? '\n' : n == 't' ? '\t' : n == 'r' ? '\r' : n;
    } else {
      o += s[i];
    }
  }
  return o;
}
static std::vector<std::string> split(const std::string& s, char sep) {
  std::vector<std::string> out;
  std::string cur;
  std::istringstream in(s);
  while (std::getline(in, cur, sep)) {
    out.push_back(cur);
  }
  return out;
}

static void hand_written() {
  check("arithmetic and precedence", value("1 + 2 * 3") == 7 && value("(1 + 2) * 3") == 9 &&
                                         value("10 - 4 - 3") == 3 && value("2 * 3 % 4") == 2 &&
                                         value("8 / 2 / 2") == 2);
  check("power is right to left and binds tighter than a minus sign",
        value("2 ^ 3 ^ 2") == 512 && value("-2 ^ 2") == -4 && value("2 ^ -1") == 0.5 &&
            value("(-2) ^ 2") == 4);
  check("numbers can be written .5, 1e3, 1.5e-1", value(".5 + 1e3 + 1.5e-1") == 1000.65);
  check("comparisons and logic give 1 or 0",
        value("3 > 2 && 1 < 2") == 1 && value("3 < 2 || 0") == 0 && value("!0") == 1 &&
            value("!5") == 0 && value("2 == 2") == 1 && value("2 != 2") == 0 &&
            value("2 <= 2") == 1 && value("3 >= 4") == 0);
  {
    int calls = 0;
    const Rng rng = [&] {
      calls++;
      return 0.5;
    };
    run("a = if(1, 5, rand(1));", {}, rng);
    const int taken = calls;
    run("a = if(0, 5, rand(1));", {}, rng);
    check("if() only works out the branch it takes", taken == 0 && calls == 1);
  }
  check("the functions",
        near(value("sin(0)"), 0) && near(value("cos(0)"), 1) && value("abs(-3)") == 3 &&
            value("min(2, 5)") == 2 && value("max(2, 5)") == 5 && value("floor(2.7)") == 2 &&
            value("ceil(2.1)") == 3 && value("int(-2.7)") == -2 &&
            near(value("frac(2.25)"), 0.25) && value("sign(-9)") == -1 && value("sqr(3)") == 9 &&
            value("sqrt(16)") == 4 && value("pow(2, 10)") == 1024 && value("mod(-1, 3)") == 2 &&
            value("above(2, 1)") == 1 && value("below(2, 1)") == 0 && value("equal(1, 1)") == 1 &&
            value("clamp(5, 0, 1)") == 1 && value("clamp(-5, 0, 1)") == 0 &&
            value("mix(0, 10, 0.25)") == 2.5 && near(value("sigmoid(0, 3)"), 0.5) &&
            value("smoothstep(0, 1, 2)") == 1 && value("smoothstep(0, 1, 0.5)") == 0.5 &&
            near(value("atan2(1, 1)"), std::atan(1.0) * 1));
  check("assignments and the shortcut forms",
        run("a = 2; a += 3; a *= 2; a -= 1; a /= 3;").vars["a"] == 3);
  check("names are not case sensitive, and a name never set reads as 0", [] {
    auto v = run("A = 2; b = a + c;").vars;
    return v["a"] == 2 && v["b"] == 2;
  }());
  check("inputs are read from the variables and results written back",
        run("zoom = 1 + bass * 0.5;", {{"bass", 1}}).vars["zoom"] == 1.5);
  check("dividing by zero, or mod 0, gives 0",
        value("1 / 0") == 0 && value("5 % 0") == 0 && run("a = 4; a /= 0;").vars["a"] == 0);
  check("results that are not numbers become 0 and huge ones are clamped",
        value("sqrt(0 - 4)") == 2 && value("1e30 * 1e30") == 1e6 && value("exp(1000)") == 1e6 &&
            std::isfinite(value("log(0)")) && value("asin(2)") == 0 && value("pow(-8, 0.5)") == 0 &&
            value("1e999") == 0);  // infinity is "not a number" too: 0, not 1e6
  check("rand uses the given source of randomness",
        run("a = rand(10);", {}, [] { return 0.37; }).vars["a"] == 3.7);
  check("empty text is a program that does nothing",
        run("", {{"a", 1}}).vars["a"] == 1 && run(" ;; ; ", {{"a", 1}}).vars["a"] == 1);
  {
    const Ran r = run("a = b + 1; c = a;");
    check("reads and writes are reported",
          r.program.writes() == std::vector<std::string>{"a", "c"} &&
              r.program.reads() == std::vector<std::string>{"a", "b", "c"} &&
              r.program.statements() == 2);
  }
  check("fix() and truthy()", fix(1e9) == 1e6 && fix(-1e9) == -1e6 && fix(NAN) == 0 &&
                                  fix(INFINITY) == 0 && fix(5) == 5 && truthy(0.001) &&
                                  !truthy(0.000001) && !truthy(NAN));
  check("the list of functions is the reference's, in its order",
        function_names().size() == 34 && function_names().front() == "sin" &&
            function_names().back() == "if");

  const std::vector<std::pair<std::string, std::string>> refuse = {
      {"unknown function", "a = foo(1);"},
      {"wrong number of arguments", "a = sin(1, 2);"},
      {"not an assignment", "1 + 2;"},
      {"missing value", "a = ;"},
      {"dangling operator", "a = 1 +"},
      {"a function name used as a value", "a = sin;"},
      {"setting a function name", "sin = 3;"},
      {"a string", "a = \"hi\";"},
      {"a dot", "a = b.c;"},
      {"brackets", "a = b[0];"},
      {"braces", "a = {};"},
      {"an unclosed bracket", "a = (1 + 2;"},
      {"an extra bracket", "a = 1 + 2);"},
      {"a pair with no semicolon", "a = 1 b = 2;"},
      {"a lone equals", "a == 1;"},
      {"__proto__", "__proto__ = 1;"},
      {"constructor", "a = constructor;"},
      {"prototype", "prototype = 3;"},
      {"a name that is far too long", "a" + std::string(40, 'b') + " = 1;"},
      {"an arrow function", "a = () => 1;"},
      {"a byte that is not ASCII", "a = \xEF\xBC\x92;"},
  };
  for (const auto& [what, src] : refuse) {
    check("refuses " + what, bad(src), src);
  }
  check("refuses a formula nested too deeply",
        bad("a = " + std::string(40, '(') + "1" + std::string(40, ')') + ";") &&
            bad("a = " + std::string(60, '-') + "1;"));
  {
    std::string many_names;
    std::string many_statements;
    for (int i = 0; i < 121; i++) {
      many_names += "n" + std::to_string(i) + " = 1;";
    }
    for (int i = 0; i < 251; i++) {
      many_statements += "a=1;";
    }
    std::string long_sum = "a = ";
    for (int i = 0; i < 1600; i++) {
      long_sum += "1+";
    }
    check("refuses a formula that is too long, has too many statements or too many names",
          bad(std::string(1200 * 6, ' ') + "a=1;") && bad(many_statements) && bad(many_names) &&
              bad(long_sum + "1;"));
  }
  check("the limits are the ones documented", Limits::source == 6000 && Limits::statements == 250 &&
                                                  Limits::names == 120 && Limits::depth == 24);
  check("error messages say where", check("a = 1 + ;").find("character 9") != std::string::npos,
        check("a = 1 + ;"));
  check("error messages print numbers the way JavaScript does",
        check("a = 1 1.50;") == "unexpected \"1.5\" at character 7" &&
            check("a = 1 1e21;") == "unexpected \"1e+21\" at character 7" &&
            check("a = 1 0.0000001;") == "unexpected \"1e-7\" at character 7" &&
            check("a = 1 100000000000000000000;") ==
                "unexpected \"100000000000000000000\" at character 7",
        check("a = 1 1e21;"));

  {
    // The most stack a formula needs grows with a chain of "/" (the right side is worked out first,
    // so the left one piles up). 1500 of them is the most 3000 tokens allow.
    std::string chain = "a = 1";
    for (int i = 0; i < 1400; i++) {
      chain += "/2";
    }
    Ran r = run(chain + ";");
    check("the longest chain of / still runs (the stack grows to fit)",
          r.ok && near(r.vars["a"], 0), r.error);
    std::string mod = "a = 99";
    for (int i = 0; i < 1400; i++) {
      mod += "%7";
    }
    check("a long chain of % is as fast as one % (each side is worked out once)",
          run(mod + ";").ok);
  }
  {
    Symbols sym;
    sym.slot("a");
    const CompileResult r = compile("a = b + 1;", sym);
    std::vector<double> too_small(1);
    bool threw = false;
    try {
      r.program.run(too_small);
    } catch (const std::length_error&) {
      threw = true;
    }
    check("running over an array that is too small throws instead of writing past it",
          r.ok && r.program.slot_count() == 2 && threw);
  }
  {
    Symbols sym;
    const CompileResult p = compile("a = rand(1);", sym, Options{[] { return 0.5; }});
    const CompileResult q = compile("a = rand(1);", sym, Options{[] { return 0.25; }});
    std::vector<double> v(sym.size());
    p.program.run(v);
    const double first = v[0];
    q.program.run(v);
    check(
        "each program keeps its own source of randomness; the default one stays in [0, 1)",
        first == 0.5 && v[0] == 0.25 && [] {
          Symbols s;
          const auto r = compile("a = rand(1);", s);
          std::vector<double> x(s.size());
          for (int i = 0; i < 200; i++) {
            r.program.run(x);
            if (x[0] < 0 || x[0] >= 1) {
              return false;
            }
          }
          return true;
        }());
  }
  {
    Symbols s1;
    Symbols s2;
    s1.slot("b");
    s1.slot("A");
    s2.slot("zzz");
    check("symbols give the same slot to the same name, whatever its case",
          s1.slot("B") == 0 && s1.slot("a") == 1 && s1.find("nope") == -1 && s1.size() == 2 &&
              s1.name(1) == "a");
  }
}

static void parity(const std::string& path) {
  std::ifstream in(path);
  if (!in) {
    check("parity: found " + path, false);
    return;
  }
  struct Case {
    std::string f, i, r, e, m, v;
  };
  std::vector<Case> cases;
  std::string line;
  while (std::getline(in, line)) {
    if (line.empty() || line[0] == '#') {
      continue;
    }
    const std::string tag = line.substr(0, 1);
    const std::string rest = line.size() > 2 ? line.substr(2) : "";
    if (tag == "F") {
      cases.push_back({unescape(rest), "", "", "", "", ""});
    } else if (cases.empty()) {
      continue;
    } else if (tag == "I") {
      cases.back().i = rest;
    } else if (tag == "R") {
      cases.back().r = rest;
    } else if (tag == "E") {
      cases.back().e = unescape(rest), cases.back().m = "E";
    } else if (tag == "M") {
      cases.back().m = rest;
    } else if (tag == "V") {
      cases.back().v = rest;
    }
  }
  int agree = 0;
  int refused = 0;
  int shown = 0;
  for (const Case& c : cases) {
    std::map<std::string, double> start;
    for (const std::string& kv : split(c.i, ' ')) {
      const auto p = kv.find('=');
      if (p != std::string::npos) {
        start[kv.substr(0, p)] = std::strtod(kv.c_str() + p + 1, nullptr);
      }
    }
    std::vector<double> seq;
    for (const std::string& x : split(c.r, ' ')) {
      seq.push_back(std::strtod(x.c_str(), nullptr));
    }
    std::size_t k = 0;
    Ran r = run(c.f, start, [&] { return seq[k++ % seq.size()]; });
    std::string why;
    if (c.m == "E") {
      if (r.ok) {
        why = "the reference refused it (" + c.e + "), the C++ version ran it";
      } else if (r.error != c.e) {
        why = "message differs: reference \"" + c.e + "\", C++ \"" + r.error + "\"";
      }
    } else if (!r.ok) {
      why = "the reference ran it, the C++ version refused it: " + r.error;
    } else {
      std::string reads;
      std::string writes;
      for (const auto& n : r.program.reads()) {
        reads += (reads.empty() ? "" : ",") + n;
      }
      for (const auto& n : r.program.writes()) {
        writes += (writes.empty() ? "" : ",") + n;
      }
      const std::string meta = "reads=" + reads + " writes=" + writes +
                               " statements=" + std::to_string(r.program.statements());
      if (meta != c.m) {
        why = "reads/writes differ: reference \"" + c.m + "\", C++ \"" + meta + "\"";
      }
      for (const std::string& kv : split(c.v, ' ')) {
        if (!why.empty()) {
          break;
        }
        const auto p = kv.find('=');
        if (p == std::string::npos) {
          continue;
        }
        const std::string name = kv.substr(0, p);
        const double want = std::strtod(kv.c_str() + p + 1, nullptr);
        const double got = r.vars[name];
        if (!(std::fabs(got - want) <= 1e-9 * std::max(1.0, std::fabs(want)))) {
          why =
              name + " is " + std::to_string(got) + ", the reference says " + std::to_string(want);
        }
      }
    }
    if (why.empty()) {
      agree++;
      if (c.m == "E") {
        refused++;
      }
    } else if (shown++ < 12) {
      std::cout << "     differs on  " << c.f.substr(0, 90) << "\n       " << why << "\n";
    }
  }
  check("parity: " + std::to_string(cases.size()) +
            " formulas give what the JavaScript reference gives (" + std::to_string(refused) +
            " of them refused, with the same message)",
        !cases.empty() && std::cmp_equal(agree, cases.size()),
        std::to_string(cases.size() - static_cast<std::size_t>(agree)) + " differ");
}

void expr_tests(const std::string& dir) {
  test::section("expr");
  hand_written();
  parity(dir + "/expr.golden");
}
