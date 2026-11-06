// Tests for the .milk importer (milk.hpp): hand-written checks plus parity with
// tests/parity/milk.golden (what the JavaScript importer made of the recorded files).
#include "check.hpp"
#include "fh/milk.hpp"
#include "tests.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <string>

#include "fh/json.hpp"

using namespace fh;
using json::Value;

using test::check;

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

static bool mentions(const MilkResult& r, const std::string& what) {
  return std::ranges::any_of(
      r.notes, [&](const std::string& n) { return n.find(what) != std::string::npos; });
}

static void hand_tests() {
  const std::string simple =
      "MILKDROP_PRESET_VERSION=201\n[preset00]\nfGammaAdj=1.5\nfDecay=0.96\nnWaveMode=4\nzoom=1."
      "01\nwarp=0.5\n"
      "per_frame_init_1=q8 = 0.5;\nper_frame_1=// pulse on the bass\nper_frame_2=zoom = 1 + "
      "0.02*max(bass-1, 0); /* a comment */\n"
      "per_frame_3=rot = 0.01*sin(time*0.3);\nper_pixel_1=zoom = zoom + 0.01*rad*q1;\n";
  MilkResult r = convert_milk(simple, "My Test Preset.milk");
  check("a simple preset converts", r.ok, r.error);
  check("the name comes from the file name, minus the ending",
        r.preset.name == "My Test Preset" && r.preset.author == "imported .milk");
  check("it is marked as imported", r.preset.dialect == "milk" && r.preset.hear == 1.8);
  check("base numbers carry over",
        std::fabs(r.preset.decay - 0.96) < 1e-9 && std::fabs(r.preset.zoom - 1.01) < 1e-9 &&
            std::fabs(r.preset.color.gamma - 1.5) < 1e-9 && r.preset.wave.mode == 1);
  check("formulas are joined in order with comments removed",
        r.preset.init == "q8 = 0.5;" && r.preset.frame.find("//") == std::string::npos &&
            r.preset.frame.find("/*") == std::string::npos &&
            r.preset.frame.find("rot = 0.01*sin(time*0.3);") != std::string::npos);
  check("it says it is an approximation",
        !r.notes.empty() && r.notes[0].starts_with("Translated from a MilkDrop preset"));

  const auto refuse = [](const std::string& what, const std::string& text,
                         const std::string& word) {
    const MilkResult x = convert_milk(text, "x.milk");
    check("refuses " + what, !x.ok && x.error.find(word) != std::string::npos, x.error);
  };
  refuse("a loop", "[preset00]\nfDecay=0.9\nper_frame_1=loop(10, q1 = q1 + 1);\n", "loop");
  refuse("a memory array", "[preset00]\nfDecay=0.9\nper_frame_1=megabuf(1) = 5;\n", "megabuf");
  refuse("a global memory array", "[preset00]\nfDecay=0.9\nper_pixel_1=dx = gmegabuf(3);\n",
         "gmegabuf");
  refuse("a loop hidden in a wave formula",
         "[preset00]\nfDecay=0.9\nwavecode_0_enabled=1\nwave_0_per_point1=loop(3, x = x + 1);\n",
         "loop");
  refuse("bitwise operators",
         "[preset00]\nfDecay=0.9\nper_frame_1=q1 = above(bass, 1) & above(mid, 1);\n", "bitwise");
  refuse("a formula it cannot parse", "[preset00]\nfDecay=0.9\nper_frame_1=zoom = 1 +;\n",
         "translated");
  refuse("nothing", "", "empty");
  refuse("a binary file", std::string("zoom=1\0\n", 8), "not a text file");
  refuse("text that is not a preset", "hello=world\n", "does not look like");
  refuse("a huge file", std::string(milk_max_text + 1, 'x'), "too big");
  check("&& and || are fine, a name containing 'loop' is fine",
        convert_milk("[preset00]\nfDecay=0.9\nper_frame_1=q1 = above(bass,1) && above(mid,1); q2 = "
                     "my_loop + 1;\n",
                     "x")
            .ok);
  check("a lone name 'x' still gets a name",
        convert_milk("zoom=1\n", ".milk").preset.name == "Imported preset");

  const MilkResult many = convert_milk(
      "zoom=1\nwavecode_0_enabled=1\nwavecode_1_enabled=1\nwavecode_2_enabled=1\nshapecode_0_"
      "enabled=1\nshapecode_0_textured=1\nwarp_1=x\nob_a=1\n",
      "m");
  check("extra waves, textured shapes and shaders get notes",
        many.ok && mentions(many, "first 2 of its custom waveforms") &&
            mentions(many, "textured") && mentions(many, "shader") && mentions(many, "borders"));
}

static void parity(const std::string& path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) {
    check("parity: open " + path, false);
    return;
  }
  int cases = 0;
  int bad = 0;
  int converted = 0;
  std::string line;
  while (std::getline(in, line)) {
    const auto parsed = json::parse(line);
    if (!parsed.ok) {
      check("parity: golden line " + std::to_string(cases + 1) + " is readable", false);
      bad++;
      break;
    }
    cases++;
    const Value& c = parsed.value;
    const MilkResult got = convert_milk(c.find("text")->as_string(), c.find("name")->as_string());
    const Value& want = *c.find("r");
    Value g = Value::object();
    g.set("ok", Value::boolean(got.ok));
    if (got.ok) {
      converted++;
      g.set("preset", to_json(got.preset));
      Value notes = Value::array();
      for (const auto& n : got.notes) {
        notes.push(Value::string(n));
      }
      g.set("notes", std::move(notes));
    } else {
      g.set("error", Value::string(got.error));
    }
    std::string why;
    if (!same(want, g, "", why) && ++bad <= 10) {
      std::string shown = c.find("text")->as_string().substr(0, 80);
      std::ranges::replace(shown, '\n', '|');
      check("parity: case " + std::to_string(cases) + " " + shown, false, why);
    }
  }
  check("parity: " + std::to_string(cases) + " files (" + std::to_string(converted) +
            " converted), " + std::to_string(bad) + " differ from the JavaScript",
        cases > 200 && bad == 0);
}

void milk_tests(const std::string& dir) {
  test::section("milk");
  hand_tests();
  parity(dir + "/milk.golden");
}
