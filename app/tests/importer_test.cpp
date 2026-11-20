// Importer tests with the app's disc image reader and an in-memory stand-in
// for title update packages (sdk_reader_test covers the real package reader).
// Usage: importer_test <tools dir>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include "launcher/importer.h"
#include "launcher/launcher.h"
#include "launcher/xdvdfs.h"
#include "launcher/xex_info.h"
using namespace mc360::launcher;
namespace fs = std::filesystem;

static int failures = 0;
#define CHECK(c) do { if(!(c)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); ++failures; } } while(0)
static std::string T;  // test tools dir
static void run(const std::string& cmd) { if (std::system(cmd.c_str())) { std::printf("cmd failed: %s\n", cmd.c_str()); std::exit(2); } }
static std::string slurp(const fs::path& p) { std::ifstream f(p, std::ios::binary); return std::string(std::istreambuf_iterator<char>(f), {}); }
static fs::path fresh(const std::string& n) { fs::path d = fs::temp_directory_path() / ("mc360imp_" + n); fs::remove_all(d); fs::create_directories(d); return d; }

static const CoreInfo kCores[] = {
  {"tu9","Title Update 9","libmc360core_tu9.dylib",true,0x584111F7u,0x7CD33B56u,0x00000A01u,true,"tu","userdata","cache"},
};

// In-memory stand-in for an STFS package (the SDK reader isn't built here).
class MemArchive : public Archive {
 public:
  std::map<std::string, std::string> data;
  std::vector<ArchiveFile> list;
  void add(std::string path, std::string bytes) { list.push_back({path, bytes.size(), list.size()}); data[path] = std::move(bytes); }
  const std::vector<ArchiveFile>& files() const override { return list; }
  bool Read(const ArchiveFile& f, uint64_t off, std::span<uint8_t> out) override {
    auto& d = data[f.path]; if (off + out.size() > d.size()) return false;
    std::memcpy(out.data(), d.data() + off, out.size()); return true;
  }
};

static ArchiveOpener MakeOpener(std::map<std::string, std::string> tu_files) {
  return [tu_files](const ImportCandidate& c, std::string* err) -> std::unique_ptr<Archive> {
    if (c.kind == ImportCandidate::Kind::kDiscImage) return XdvdfsArchive::Open(c.path, err);
    auto a = std::make_unique<MemArchive>();
    for (auto& [k, v] : tu_files) a->add(k, v);
    return a;
  };
}

int main(int, char** argv) {
  T = argv[1];
  fs::path src = fresh("src");
  run("python3 " + T + "/mkxex.py " + (src/"default.xex").string() + " 584111F7 7CD33B56 00000000 00000000");
  run("python3 " + T + "/mkxex.py " + (src/"default.xexp").string() + " 584111F7 7CD33B56 00000A01 00000000 00000A01");
  run("python3 " + T + "/mkxex.py " + (src/"other.xex").string() + " 4D5307E6 11111111 00000000 00000000");
  run("head -c 9437000 /dev/urandom > " + (src/"big.bin").string());
  run("head -c 3000 /dev/urandom > " + (src/"terrain.png").string());
  run("head -c 777 /dev/urandom > " + (src/"su.bin").string());
  const std::string s = src.string();

  // ---- XDVDFS reader, XISO layout
  fs::path base = fresh("base");
  run("python3 " + T + "/mkiso.py " + (base/"Minecraft.iso").string() + " 0 default.xex=" + s + "/default.xex res/terrain.png=" + s + "/terrain.png res/sub/big.bin=" + s + "/big.bin '$SystemUpdate/su.bin=" + s + "/su.bin' Tutorial/empty.bin=@empty");
  {
    std::string err;
    auto iso = XdvdfsArchive::Open(base/"Minecraft.iso", &err);
    CHECK(iso);
    if (iso) {
      std::printf("iso files:");
      for (auto& f : iso->files()) std::printf(" %s(%llu)", f.path.c_str(), (unsigned long long)f.size);
      std::printf("\n");
      CHECK(iso->files().size() == 5);
    }
  }
  // packages
  run("python3 " + T + "/mkpkg.py " + (base/"TU_16L61V6_0000014000000.00000000000O9").string() + " LIVE 000B0000 584111F7 00000A01 'Minecraft: Xbox 360 Edition'");
  fs::create_directories(base/"import");
  run("python3 " + T + "/mkpkg.py " + (base/"import/TU_later").string() + " LIVE 000B0000 584111F7 00001101 'Minecraft: Xbox 360 Edition'");
  run("python3 " + T + "/mkpkg.py " + (base/"SaveGame").string() + " CON 00000001 584111F7 00000000 'My world'");
  run("python3 " + T + "/mkpkg.py " + (base/"OtherGameTU").string() + " LIVE 000B0000 4D5307E6 00000005 'Other'");
  std::ofstream(base/"notes.txt") << "hello";
  fs::create_directories(base/"game");  // the app makes an empty game/ at first launch

  auto c = FindImportCandidates(base, kCores);
  for (auto& x : c) std::printf("cand: %s kind=%d state=%d ver=%X core=%s name='%s' note='%s'\n", x.path.filename().c_str(), (int)x.kind, (int)x.state, x.version, x.core ? x.core->id : "-", x.display_name.c_str(), x.note.c_str());
  CHECK(c.size() == 4);
  CHECK(HasPendingImports(c));

  auto find = [&](const char* name) -> ImportCandidate { for (auto& x : FindImportCandidates(base, kCores)) if (x.path.filename() == name) return x; return {}; };

  // ---- import the disc
  {
    ImportJob job(find("Minecraft.iso"), base, kCores, MakeOpener({}));
    job.RunForTest();
    std::printf("disc: ok=%d msg=%s done=%llu/%llu\n", job.succeeded(), job.message().c_str(), (unsigned long long)job.bytes_done(), (unsigned long long)job.bytes_total());
    CHECK(job.succeeded());
    CHECK(slurp(base/"game/default.xex") == slurp(src/"default.xex"));
    CHECK(slurp(base/"game/res/sub/big.bin") == slurp(src/"big.bin"));
    CHECK(slurp(base/"game/res/terrain.png") == slurp(src/"terrain.png"));
    CHECK(fs::exists(base/"game/Tutorial/empty.bin") && fs::file_size(base/"game/Tutorial/empty.bin") == 0);
    CHECK(!fs::exists(base/"game/$SystemUpdate"));
    CHECK(!fs::exists(base/".import-tmp"));
    CHECK(job.bytes_done() == job.bytes_total());
    CHECK(find("Minecraft.iso").state == ImportCandidate::State::kInstalled);
  }
  // ---- import TU9 (package contents via the stand-in)
  std::map<std::string, std::string> tu9 = {{"default.xexp", slurp(src/"default.xexp")}, {"res/1_2_2/terrain.png", "PNG"}};
  {
    auto cand = find("TU_16L61V6_0000014000000.00000000000O9");
    CHECK(cand.state == ImportCandidate::State::kReady && cand.core == &kCores[0]);
    ImportJob job(cand, base, kCores, MakeOpener(tu9));
    job.RunForTest();
    std::printf("tu9: ok=%d msg=%s\n", job.succeeded(), job.message().c_str());
    CHECK(job.succeeded());
    CHECK(slurp(base/"tu/default.xexp") == slurp(src/"default.xexp"));
    CHECK(slurp(base/"tu/res/1_2_2/terrain.png") == "PNG");
    CHECK(find("TU_16L61V6_0000014000000.00000000000O9").state == ImportCandidate::State::kInstalled);
    // the launcher now sees TU9 as ready
    Launcher l(base, kCores); l.Scan();
    for (auto& i : l.cores()[0].issues) std::printf("  issue: %s\n", i.text.c_str());
    CHECK(l.cores()[0].issues.empty() && l.cores()[0].update_root == base/"tu");
    // importing again is refused, nothing touched
    ImportJob again(cand, base, kCores, MakeOpener(tu9));
    again.RunForTest();
    std::printf("again: ok=%d msg=%s\n", again.succeeded(), again.message().c_str());
    CHECK(!again.succeeded());
  }
  // ---- a title update this app has no core for: unpacked to tu-<version>
  {
    std::string later_xexp_path = (src/"later.xexp").string();
    run("python3 " + T + "/mkxex.py " + later_xexp_path + " 584111F7 7CD33B56 00001101 00000000 00001101");
    auto cand = find("TU_later");
    CHECK(cand.state == ImportCandidate::State::kReady && cand.core == nullptr);
    ImportJob job(cand, base, kCores, MakeOpener({{"default.xexp", slurp(later_xexp_path)}}));
    job.RunForTest();
    std::printf("later: ok=%d msg=%s\n", job.succeeded(), job.message().c_str());
    CHECK(job.succeeded() && fs::exists(base/"tu-0.0.17.1/default.xexp"));
  }
  // ---- wrong-game title update and save are not offered
  CHECK(find("OtherGameTU").state == ImportCandidate::State::kBlocked);
  CHECK(find("SaveGame").path.empty());
  CHECK(!HasPendingImports(FindImportCandidates(base, kCores)));

  // ---- full-size disc layout (game partition at 0xFD90000), wrong game, damaged image
  {
    fs::path b2 = fresh("xgd2");
    run("python3 " + T + "/mkiso.py " + (b2/"full.iso").string() + " FD90000 default.xex=" + s + "/default.xex res/a.bin=" + s + "/terrain.png");
    auto cands = FindImportCandidates(b2, kCores);
    CHECK(cands.size() == 1 && cands[0].kind == ImportCandidate::Kind::kDiscImage);
    ImportJob job(cands[0], b2, kCores, MakeOpener({}));
    job.RunForTest();
    std::printf("xgd2: ok=%d msg=%s\n", job.succeeded(), job.message().c_str());
    CHECK(job.succeeded() && slurp(b2/"game/res/a.bin") == slurp(src/"terrain.png"));

    fs::path b3 = fresh("wrong");
    run("python3 " + T + "/mkiso.py " + (b3/"other.iso").string() + " 0 default.xex=" + s + "/other.xex");
    auto c3 = FindImportCandidates(b3, kCores);
    ImportJob wrong(c3.at(0), b3, kCores, MakeOpener({}));
    wrong.RunForTest();
    std::printf("wrong: ok=%d msg=%s\n", wrong.succeeded(), wrong.message().c_str());
    CHECK(!wrong.succeeded() && !fs::exists(b3/"game") && !fs::exists(b3/".import-tmp"));

    // truncated: cut the XISO in the middle of big.bin
    fs::path b4 = fresh("cut");
    fs::copy_file(base/"Minecraft.iso", b4/"cut.iso");
    fs::resize_file(b4/"cut.iso", fs::file_size(b4/"cut.iso") - 4000000);
    std::string err;
    auto cut = XdvdfsArchive::Open(b4/"cut.iso", &err);
    std::printf("cut: open=%d err=%s\n", cut != nullptr, err.c_str());
    CHECK(!cut);
    std::ofstream(b4/"garbage.iso") << std::string(200000, 'x');
    CHECK(!XdvdfsArchive::LooksLikeDiscImage(b4/"garbage.iso"));
    CHECK(FindImportCandidates(b4, kCores).size() == 1);  // cut.iso still has the signature
  }
  // ---- game/ already has other files: blocked, untouched
  {
    fs::path b5 = fresh("busy");
    fs::copy_file(base/"Minecraft.iso", b5/"m.iso");
    fs::create_directories(b5/"game"); std::ofstream(b5/"game/readme.txt") << "x";
    auto c5 = FindImportCandidates(b5, kCores);
    CHECK(c5.size() == 1 && c5[0].state == ImportCandidate::State::kBlocked);
  }
  std::printf("FormatBytes: %s %s %s\n", FormatBytes(7.3 * (1ull<<30)).c_str(), FormatBytes(340ull<<20).c_str(), FormatBytes(2000).c_str());
  std::printf(failures ? "%d FAILURES\n" : "ALL PASSED\n", failures);
  return failures != 0;
}
