// Launcher model tests: which folders each core uses, the checks on the
// player's files, instances and the remembered choice.
// Usage: launcher_test <path to tools/mkxex.py>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include "launcher/launcher.h"
#include "launcher/xex_info.h"
using namespace mc360::launcher;
namespace fs = std::filesystem;

static const CoreInfo kTu9{"tu9","Title Update 9","libmc360core_tu9.dylib",true,0x584111F7u,0x7CD33B56u,0x00000A01u,true,"tu","userdata","cache"};
static const CoreInfo kTu14{"tu14","Title Update 14","libmc360core_tu14.dylib",true,0x584111F7u,0x7CD33B56u,0x00001101u,true,"tu14","userdata-tu14","cache-tu14"};
static const CoreInfo kUnknown{"tux","Mystery","libx.dylib",false,0,0,0,true,"tux","userdata-tux","cache-tux"};

static int failures = 0;
#define CHECK(c) do { if(!(c)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); ++failures; } } while(0)

static std::string P; // python script path
static void mkxex(const fs::path& out, const char* args) {
  fs::create_directories(out.parent_path());
  std::string cmd = "python3 " + P + " '" + out.string() + "' " + args;
  if (std::system(cmd.c_str()) != 0) { std::printf("mkxex failed\n"); std::exit(2); }
}
static fs::path fresh(const char* name) {
  fs::path d = fs::temp_directory_path() / ("mc360test_" + std::string(name));
  fs::remove_all(d); fs::create_directories(d); return d;
}
static void dump(const Launcher& l) {
  for (auto& s : l.cores()) {
    std::printf("  [%s] update=%s instances=%zu\n", s.core->id, s.update_root.string().c_str(), s.instances.size());
    for (auto& i : s.issues) std::printf("    %s: %s\n", i.severity==Issue::Severity::kError?"E":"W", i.text.c_str());
  }
}

int main(int, char** argv) {
  P = argv[1];
  // --- parser
  {
    fs::path d = fresh("parse");
    mkxex(d/"default.xex", "584111F7 7CD33B56 00000000 00000000");
    mkxex(d/"default.xexp", "584111F7 7CD33B56 00000A01 00000000 00000A01");
    auto x = ReadXexInfo(d/"default.xex");
    CHECK(x && x->has_execution_info && x->title_id==0x584111F7 && x->media_id==0x7CD33B56 && !x->is_patch());
    CHECK(x && x->resulting_version() && *x->resulting_version()==0);
    auto p = ReadXexInfo(d/"default.xexp");
    CHECK(p && p->is_patch() && p->has_delta_patch && p->patch_target_version==0xA01 && *p->resulting_version()==0xA01);
    CHECK(FormatXexVersion(0xA01)=="0.0.10.1");
    CHECK(FormatXexVersion(0x20012345)=="2.0.291.69");
    std::ofstream(d/"junk") << "not a xex";
    CHECK(!ReadXexInfo(d/"junk"));
    CHECK(!ReadXexInfo(d/"missing"));
    // truncated header must not crash
    uint8_t buf[0x20] = {'X','E','X','2'}; buf[0x17]=200;
    CHECK(!ParseXexInfo(buf, sizeof(buf)));
  }
  // --- 1. legacy TU9-only, correct files
  {
    fs::path d = fresh("legacy");
    mkxex(d/"game/default.xex", "584111F7 7CD33B56 00000000 00000000");
    mkxex(d/"tu/default.xexp", "584111F7 7CD33B56 00000A01 00000000 00000A01");
    const CoreInfo cores[] = {kTu9};
    Launcher l(d, cores); l.Scan(); dump(l);
    CHECK(l.cores()[0].issues.empty());
    CHECK(!l.ShouldShowMenu(false));
    CHECK(l.ShouldShowMenu(true));
    auto sel = l.DefaultSelection();
    CHECK(sel.core_id=="tu9" && sel.instance==1);
    auto paths = l.PathsFor(sel);
    CHECK(paths && paths->update_data_root==d/"tu" && paths->user_data_root==d/"userdata" && paths->cache_root==d/"cache");
    // TU9 xexp also in game/ (runtime auto-applies it): matching -> fine
    mkxex(d/"game/default.xexp", "584111F7 7CD33B56 00000A01 00000000 00000A01");
    l.Scan(); CHECK(l.cores()[0].issues.empty());
  }
  // --- 2. tu/ missing -> warning, menu shown, no update root
  {
    fs::path d = fresh("noupdate");
    mkxex(d/"game/default.xex", "584111F7 7CD33B56 00000000 00000000");
    const CoreInfo cores[] = {kTu9};
    Launcher l(d, cores); l.Scan(); dump(l);
    CHECK(l.cores()[0].has_warning() && !l.cores()[0].has_error());
    CHECK(l.ShouldShowMenu(false));
    CHECK(l.cores()[0].update_root.empty());
  }
  // --- 3. two cores; TU14 in an oddly named folder; TU14 in tu/ instead
  {
    fs::path d = fresh("two");
    mkxex(d/"game/default.xex", "584111F7 7CD33B56 00000000 00000000");
    mkxex(d/"tu/default.xexp", "584111F7 7CD33B56 00000A01 00000000 00000A01");
    mkxex(d/"Title Update 14/default.xexp", "584111F7 7CD33B56 00001101 00000000 00001101");
    const CoreInfo cores[] = {kTu9, kTu14};
    Launcher l(d, cores); l.Scan(); dump(l);
    CHECK(l.cores()[0].issues.empty() && l.cores()[0].update_root==d/"tu");
    CHECK(l.cores()[1].issues.empty() && l.cores()[1].update_root==d/"Title Update 14");
    CHECK(l.ShouldShowMenu(false));
    CHECK(l.Options().size()==2);
    auto p14 = l.PathsFor({"tu14",1});
    CHECK(p14 && p14->user_data_root==d/"userdata-tu14" && p14->cache_root==d/"cache-tu14");
    // swap: tu/ now holds TU14, TU9 nowhere
    fs::remove_all(d/"Title Update 14");
    mkxex(d/"tu/default.xexp", "584111F7 7CD33B56 00001101 00000000 00001101");
    l.Scan(); dump(l);
    CHECK(l.cores()[0].has_warning() && l.cores()[0].update_root==d/"tu");
    CHECK(l.cores()[1].issues.empty() && l.cores()[1].update_root==d/"tu");
  }
  // --- 4. stray game/default.xexp of TU9 breaks TU14
  {
    fs::path d = fresh("stray");
    mkxex(d/"game/default.xex", "584111F7 7CD33B56 00000000 00000000");
    mkxex(d/"game/default.xexp", "584111F7 7CD33B56 00000A01 00000000 00000A01");
    mkxex(d/"tu/default.xexp", "584111F7 7CD33B56 00000A01 00000000 00000A01");
    mkxex(d/"tu14/default.xexp", "584111F7 7CD33B56 00001101 00000000 00001101");
    const CoreInfo cores[] = {kTu9, kTu14};
    Launcher l(d, cores); l.Scan(); dump(l);
    CHECK(l.cores()[0].issues.empty());
    CHECK(l.cores()[1].issues.size()==1 && l.cores()[1].has_warning());
  }
  // --- 5/6. wrong disc / missing xex
  {
    fs::path d = fresh("disc");
    mkxex(d/"game/default.xex", "584111F7 DEADBEEF 00000000 00000000");
    mkxex(d/"tu/default.xexp", "584111F7 7CD33B56 00000A01 00000000 00000A01");
    const CoreInfo cores[] = {kTu9};
    Launcher l(d, cores); l.Scan(); dump(l);
    CHECK(l.cores()[0].has_warning() && !l.cores()[0].has_error());
    fs::remove(d/"game/default.xex");
    l.Scan(); dump(l);
    CHECK(l.cores()[0].has_error());
    CHECK(l.ShouldShowMenu(false));
  }
  // --- 7/8. instances + last selection
  {
    fs::path d = fresh("inst");
    mkxex(d/"game/default.xex", "584111F7 7CD33B56 00000000 00000000");
    mkxex(d/"tu/default.xexp", "584111F7 7CD33B56 00000A01 00000000 00000A01");
    mkxex(d/"tu14/default.xexp", "584111F7 7CD33B56 00001101 00000000 00001101");
    fs::create_directories(d/"userdata/logs");
    fs::create_directories(d/"userdata-tu14");
    fs::create_directories(d/"userdata-tu14-2");
    fs::create_directories(d/"userdata-abc");
    const CoreInfo cores[] = {kTu9, kTu14};
    Launcher l(d, cores); l.Scan(); dump(l);
    CHECK(l.cores()[0].instances.size()==1);
    CHECK(l.cores()[1].instances.size()==2 && l.cores()[1].instances[1].number==2);
    auto n = l.CreateInstance("tu9");
    CHECK(n && *n==2 && fs::is_directory(d/"userdata-2"));
    CHECK(l.cores()[0].instances.size()==2);
    auto n3 = l.CreateInstance("tu9");
    CHECK(n3 && *n3==3);
    fs::remove_all(d/"userdata-2");
    l.Scan();
    CHECK(l.cores()[0].instances.size()==2 && l.cores()[0].instances[1].number==3);
    CHECK(*l.CreateInstance("tu9")==4);
    // single core with 2 instances -> menu
    const CoreInfo one[] = {kTu9};
    Launcher l1(d, one); l1.Scan(); CHECK(l1.ShouldShowMenu(false));
    // last selection
    CHECK(!l.LoadLastSelection());
    CHECK(l.DefaultSelection()==(Selection{"tu9",1}));
    l.SaveLastSelection({"tu14",2});
    auto last = l.LoadLastSelection();
    CHECK(last && *last==(Selection{"tu14",2}));
    CHECK(l.DefaultSelection()==(Selection{"tu14",2}));
    l.SaveLastSelection({"tu14",9});  // gone -> falls back to first clean
    CHECK(l.DefaultSelection()==(Selection{"tu9",1}));
    CHECK(!l.PathsFor({"tu14",9}));
    CHECK(!l.PathsFor({"nope",1}));
  }
  // --- 9. identity unknown core + 10. override
  {
    fs::path d = fresh("unknown");
    mkxex(d/"game/default.xex", "584111F7 DEADBEEF 00000000 00000000");
    fs::create_directories(d/"tux");
    const CoreInfo cores[] = {kUnknown};
    Launcher l(d, cores); l.Scan(); dump(l);
    CHECK(l.cores()[0].issues.empty() && l.cores()[0].update_root==d/"tux");
    CHECK(!l.ShouldShowMenu(false));
    const CoreInfo cores2[] = {kTu9};
    Launcher l2(d, cores2); l2.SetUpdateRootOverride("/somewhere"); l2.Scan(); dump(l2);
    CHECK(l2.cores()[0].update_root=="/somewhere");
  }
  // --- empty Documents (first launch): no crash, error shown
  {
    fs::path d = fresh("empty");
    const CoreInfo cores[] = {kTu9};
    Launcher l(d / "does-not-exist", cores); l.Scan(); dump(l);
    CHECK(l.cores()[0].has_error());
    Launcher none(d, {}); none.Scan(); CHECK(none.ShouldShowMenu(false)); CHECK(none.Options().empty());
    auto s = none.DefaultSelection(); CHECK(s.core_id.empty());
    CHECK(!none.PathsFor(s));
  }
  std::printf(failures ? "%d FAILURES\n" : "ALL PASSED\n", failures);
  return failures ? 1 : 0;
}
