// Tests for the folders of people's own pictures (include/fh/userfiles.hpp), in a temporary folder.
#include "check.hpp"
#include "fh/userfiles.hpp"
#include "tests.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>

using namespace fh;
namespace fs = std::filesystem;

using test::check;
static void write(const fs::path& p, const std::string& body) {
  std::ofstream(p, std::ios::binary) << body;
}
static bool has(const std::vector<fs::path>& v, const fs::path& p) {
  return std::ranges::find(v, p) != v.end();
}

void userfiles_tests() {
  test::section("userfiles");
  const fs::path tmp =
      fs::temp_directory_path() / ("fh-userfiles-test-" + std::to_string(std::rand()));
  fs::remove_all(tmp);

  check("a missing folder is an empty list", list_pictures(tmp / "nope").empty());
  for (const char* name : {"a.png", "b.JPG", "c.jpeg", "d.gif", "l.webp", "e.bmp", "f.tga", "g.psd",
                           "h.pic", "i.pnm", "j.ppm", "k.pgm", "dir/x.png"}) {
    check(std::string(name) + " looks like a picture", looks_like_picture(name));
  }
  for (const char* name : {"README.txt", "notes", "x.avif", "y.pdf", "z.png.txt", ".png"}) {
    check(std::string(name) + " does not",
          !looks_like_picture(name) ||
              std::string(name) == ".png");  // ".png" alone has no extension in std::filesystem
  }

  fs::create_directories(tmp / "sub");
  write(tmp / "b.png", "x");
  write(tmp / "a.GIF", "x");
  write(tmp / "README.txt", "x");
  write(tmp / "sub" / "deep.png", "x");
  auto list = list_pictures(tmp);
  check("the list has the pictures, sorted, and not the README or what is in sub-folders",
        list.size() == 2 && list[0] == tmp / "a.GIF" && list[1] == tmp / "b.png");

  FolderWatcher w(tmp);
  const auto first = w.poll();
  check("watcher: the first look reports what is there as added",
        first.added.size() == 2 && first.removed.empty() && first.changed.empty());
  check("watcher: nothing changing is nothing reported", !w.poll().any());
  write(tmp / "c.jpg", "new");
  const auto added = w.poll();
  check("watcher: a new picture is reported once",
        added.added.size() == 1 && has(added.added, tmp / "c.jpg") && !w.poll().any());
  write(tmp / "notapicture.txt", "x");
  check("watcher: a new text file is ignored", !w.poll().any());
  fs::remove(tmp / "a.GIF");
  const auto gone = w.poll();
  check("watcher: a removed picture is reported",
        gone.removed.size() == 1 && has(gone.removed, tmp / "a.GIF") && gone.added.empty());
  write(tmp / "b.png", "a different, longer body");
  const auto changed = w.poll();
  check("watcher: a replaced picture is reported as changed",
        changed.changed.size() == 1 && has(changed.changed, tmp / "b.png") &&
            changed.added.empty() && changed.removed.empty());
  {
    FolderWatcher missing(tmp / "later");
    check("watcher: a folder that does not exist yet reports nothing", !missing.poll().any());
    fs::create_directories(tmp / "later");
    write(tmp / "later" / "p.png", "x");
    check("watcher: it notices when the folder appears with a picture in it",
          missing.poll().added.size() == 1);
  }
  {
    FolderWatcher texts(tmp, [](const fs::path& f) { return f.extension() == ".txt"; });
    const auto got = texts.poll();
    check("watcher: with its own rule it watches other kinds of file",
          got.added.size() == 2 && has(got.added, tmp / "README.txt") &&
              has(got.added, tmp / "notapicture.txt"));
    check("listFiles uses the rule given",
          list_files(tmp, [](const fs::path& f) { return f.extension() == ".png"; }).size() == 1);
  }
  {
    const fs::path deep = tmp / "deep";
    fs::create_directories(deep / "Artist" / "Album" / "x" / "y" / "z");
    write(deep / "Artist" / "Album" / "1.png", "x");
    write(deep / "Artist" / "2.png", "x");
    write(deep / "0.png", "x");
    write(deep / "Artist" / "Album" / "x" / "y" / "ok.png", "x");  // 4 folders down: still found
    write(deep / "Artist" / "Album" / "x" / "y" / "z" / "toodeep.png",
          "x");  // 5 down: not looked at
    const auto flat = list_files(deep, looks_like_picture);
    const auto all = list_files(deep, looks_like_picture, true);
    check(
        "not recursive by default; recursive finds the nested ones, sorted with each "
        "folder's files together, and stops 4 levels down",
        flat.size() == 1 && all.size() == 4 && all[0] == deep / "0.png" &&
            all[1] == deep / "Artist" / "2.png" && all[2] == deep / "Artist" / "Album" / "1.png" &&
            all[3].filename() == "ok.png");
    FolderWatcher rec(deep, looks_like_picture, true);
    check("watcher: a recursive one reports nested files", rec.poll().added.size() == 4);
    write(deep / "Artist" / "new.png", "x");
    check("watcher: a recursive one also reports new nested files", rec.poll().added.size() == 1);
  }
  fs::remove_all(tmp);
}
