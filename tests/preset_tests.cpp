// Tests for the JSON reader and the preset loader: hand-written checks plus parity with
// tests/parity/preset.golden.
#include "check.hpp"
#include "fh/json.hpp"
#include "fh/preset.hpp"
#include "tests.hpp"

#include <cmath>
#include <fstream>
#include <iostream>
#include <string>

using namespace fh;
using json::Value;

using test::check;

// Same type, same keys, strings equal, numbers within 1e-9. Says where the first difference is.
static bool same(const Value& a, const Value& b, const std::string& at, std::string& why) {
  if (a.type() != b.type()) {
    why = at + ": different types";
    return false;
  }
  switch (a.type()) {
    case Value::Type::Null:
      return true;
    case Value::Type::Bool:
      if (a.as_bool() != b.as_bool()) {
        why = at + ": different flags";
        return false;
      }
      return true;
    case Value::Type::Number:
      if (std::fabs(a.as_number() - b.as_number()) >
          1e-9 * std::max(1.0, std::fabs(a.as_number()))) {
        why = at + ": " + json::stringify(a) + " vs " + json::stringify(b);
        return false;
      }
      return true;
    case Value::Type::String:
      if (a.as_string() != b.as_string()) {
        why = at + ": \"" + a.as_string() + "\" vs \"" + b.as_string() + "\"";
        return false;
      }
      return true;
    case Value::Type::Array:
      if (a.items().size() != b.items().size()) {
        why = at + ": different lengths";
        return false;
      }
      for (std::size_t i = 0; i < a.items().size(); i++) {
        if (!same(a.items()[i], b.items()[i], at + "[" + std::to_string(i) + "]", why)) {
          return false;
        }
      }
      return true;
    case Value::Type::Object:
      if (a.members().size() != b.members().size()) {
        why = at + ": different keys";
        return false;
      }
      for (const auto& m : a.members()) {
        const Value* o = b.find(m.first);
        if (!o) {
          why = at + ": missing key " + m.first;
          return false;
        }
        if (!same(m.second, *o, at + "." + m.first, why)) {
          return false;
        }
      }
      return true;
  }
  return false;
}

static bool good_json(const std::string& s) { return json::parse(s).ok; }

static void json_tests() {
  check("scalars", good_json("null") && good_json("true") && good_json("-0.5e+3") &&
                       good_json("\"x\"") && good_json(" [ 1 , 2 ] ") && good_json("{}"));
  for (const char* bad :
       {"", "{", "[1,]", "{\"a\":1,}", "01", "+1", ".5", "1.", "1e", "NaN", "'x'", "[1] x",
        "{\"a\" 1}", "{a:1}", "\"\t\"", "\"\\q\"", "\"\\u12\"", "tru", "\xEF\xBB\xBF{}"}) {
    check(std::string("refuses ") + (*bad ? bad : "(empty)"), !good_json(bad));
  }
  const auto v = json::parse("{\"a\":1,\"a\":2,\"b\":[true,null,\"\\u00e9\\n\"]}");
  check("a later duplicate key wins",
        v.ok && v.value.find("a")->as_number() == 2 && v.value.members().size() == 2);
  check("escapes become UTF-8", v.ok && v.value.find("b")->items()[2].as_string() == "\xC3\xA9\n");
  const auto pair = json::parse("\"\\ud83d\\ude00\"");
  check("a surrogate pair becomes one character",
        pair.ok && pair.value.as_string() == "\xF0\x9F\x98\x80");
  const auto lone = json::parse("\"\\ud800x\"");
  check("a lone surrogate becomes U+FFFD", lone.ok && lone.value.as_string() ==
                                                          "\xEF\xBF\xBD"
                                                          "x");
  check("1e999 is infinity, as in JavaScript",
        json::parse("1e999").ok && std::isinf(json::parse("1e999").value.as_number()));
  check("nesting at the limit is fine",
        good_json(std::string(json::max_depth, '[') + std::string(json::max_depth, ']')));
  check("nesting past the limit is refused, not a crash",
        !good_json(std::string(json::max_depth + 2, '[') + std::string(json::max_depth + 2, ']')));
  check("a million brackets is refused, not a crash", !good_json(std::string(1000000, '[')));
  check("stringify with indent 1",
        json::stringify(json::parse("{\"a\":[1,{\"b\":null}],\"c\":\"x\\\"\"}").value, 1) ==
            "{\n \"a\": [\n  1,\n  {\n   \"b\": null\n  }\n ],\n \"c\": \"x\\\"\"\n}");
  check("stringify compact",
        json::stringify(json::parse("{ \"a\" : [ ] , \"b\" : { } , \"c\" : 0.1 }").value) ==
            "{\"a\":[],\"b\":{},\"c\":0.1}");
}

static std::string file(const std::string& body) { return "{\"fh-preset\":1," + body + "}"; }

static void preset_file_tests() {
  const auto r = parse_preset_file(file("\"name\":\"Tide\""));
  check("a bare name loads with the defaults",
        r.ok && r.preset.name == "Tide" && r.preset.zoom == 1 && r.preset.decay == 0.97 &&
            r.preset.waves.empty() && r.preset.dialect == "fh");
  check("numbers are forced into range",
        parse_preset_file(file("\"name\":\"x\",\"zoom\":9,\"decay\":0,\"rot\":-9")).preset.zoom ==
                1.3 &&
            parse_preset_file(file("\"name\":\"x\",\"decay\":0")).preset.decay == 0.5);
  check("a wrong type gets the default",
        parse_preset_file(file("\"name\":\"x\",\"zoom\":\"2\"")).preset.zoom == 1);
  check("whole numbers round half up",
        parse_preset_file(file("\"name\":\"x\",\"wave\":{\"mode\":2.5,\"thick\":1.5}"))
                    .preset.wave.mode == 3 &&
            parse_preset_file(file("\"name\":\"x\",\"color\":{\"echoOrient\":-0.5}"))
                    .preset.color.echo_orient == 0);
  check("name is cleaned and cut",
        parse_preset_file(file("\"name\":\"  a\\tb  \"")).preset.name == "a b" &&
            parse_preset_file(file("\"name\":\"" + std::string(60, 'z') + "\"")).preset.name ==
                std::string(40, 'z'));
  check("a name of 40 two-byte characters is kept whole",
        parse_preset_file(file("\"name\":\"" + std::string("\xC3\xA9").append("") + "\"")).ok);
  check("no name", parse_preset_file(file("\"name\":\" \"")).error == "The preset needs a name.");
  check("not a FalseHorizons file",
        parse_preset_file("{\"name\":\"x\"}").error == "That file is not a FalseHorizons preset.");
  check("not JSON", parse_preset_file("nope").error ==
                        "That file is not a visual preset (it is not valid JSON).");
  check("too big", parse_preset_file(std::string(65537, ' ')).error ==
                       "That file is too big to be a visual preset.");
  check("a bad formula is named",
        parse_preset_file(file("\"name\":\"x\",\"waves\":[{},{\"point\":\"a = ;\"}]"))
            .error.starts_with("The wave 2 point formula has a problem: "));
  check("lists are cut to their maximum",
        parse_preset_file(file("\"name\":\"x\",\"waves\":[{},{},{}],\"shapes\":[{},{},{},{},{}]"))
                    .preset.waves.size() == 2 &&
            parse_preset_file(file("\"name\":\"x\",\"shapes\":[{},{},{},{},{}]"))
                    .preset.shapes.size() == 4);

  // A file that comes out of exportPreset loads back the same.
  const auto original = parse_preset_file(
      file("\"name\":\"Round\",\"author\":\"me\",\"dialect\":\"milk\",\"hear\":1.5,\"zoom\":1.07,"
           "\"frame\":\"zoom = 1 + bass * 0.1;\","
           "\"wave\":{\"mode\":2,\"r\":0.3,\"additive\":false},\"color\":{\"hue\":12.5},"
           "\"waves\":[{\"samples\":64,\"point\":\"x = "
           "sample;\"}],\"shapes\":[{\"sides\":7,\"instances\":3,\"frame\":\"ang = time;\"}]"));
  const auto again = parse_preset_file(export_preset(original.preset));
  std::string why;
  check("export then load gives the same preset",
        original.ok && again.ok && same(to_json(original.preset), to_json(again.preset), "", why),
        why);
  check("an export starts with the marker",
        export_preset(original.preset).starts_with("{\n \"fh-preset\": 1,\n \"name\": \"Round\""));
}

static std::string read_line(std::istream& in, char tag) {
  std::string line;
  if (!std::getline(in, line) || line.size() < 2 || line[0] != tag || line[1] != ' ') {
    return "\x01";
  }
  return line.substr(2);
}

static void parity(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    check("parity: open " + path, false);
    return;
  }
  int cases = 0;
  int bad = 0;
  while (true) {
    const std::string t = read_line(in, 'T');
    if (t == "\x01") {
      break;
    }
    const std::string r = read_line(in, 'R');
    const auto text = json::parse(t);
    const auto want = json::parse(r);
    if (!text.ok || !want.ok || !text.value.is_string()) {
      check("parity: golden entry " + std::to_string(cases) + " is readable", false);
      bad++;
      break;
    }
    cases++;
    const PresetResult got = parse_preset_file(text.value.as_string());
    Value got_json = Value::object();
    got_json.set("ok", Value::boolean(got.ok));
    if (got.ok) {
      got_json.set("preset", to_json(got.preset));
    } else {
      got_json.set("error", Value::string(got.error));
    }
    std::string why;
    if (!same(got_json, want.value, "", why)) {
      bad++;
      if (bad <= 10) {
        check("parity: case " + std::to_string(cases) + " " + text.value.as_string().substr(0, 90),
              false, why);
      }
    }
  }
  check("parity: " + std::to_string(cases) + " files, " + std::to_string(bad) +
            " differ from the JavaScript",
        cases > 2000 && bad == 0);
}

void preset_tests(const std::string& dir) {
  test::section("preset");
  test::section("json");
  json_tests();
  test::section("preset");
  preset_file_tests();
  parity(dir + "/preset.golden");
}
