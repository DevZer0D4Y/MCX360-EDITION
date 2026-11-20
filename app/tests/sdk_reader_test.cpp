// The importer with the real readers: the SDK's STFS reader (title update
// packages) and the app's disc image reader, checked against the SDK's own
// DiscImageDevice. Built only when REXGLUE_SDK is set (see CMakeLists.txt).
// Usage: sdk_reader_test <tools dir>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

#include <rex/filesystem/devices/disc_image_device.h>
#include <rex/filesystem/entry.h>
#include <rex/filesystem/file.h>

#include "launcher/importer.h"
#include "launcher/launcher.h"
#include "launcher/stfs_archive.h"
#include "launcher/xdvdfs.h"

using namespace mc360::launcher;
namespace fs = std::filesystem;
namespace rfs = rex::filesystem;
using rex::X_STATUS;

namespace {

int failures = 0;
#define CHECK(c)                                                    \
  do {                                                              \
    if (!(c)) {                                                     \
      std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);     \
      ++failures;                                                   \
    }                                                               \
  } while (0)

std::string tools;
void Run(const std::string& cmd) {
  if (std::system(cmd.c_str()) != 0) {
    std::printf("command failed: %s\n", cmd.c_str());
    std::exit(2);
  }
}
std::string Slurp(const fs::path& p) {
  std::ifstream f(p, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(f), {});
}
fs::path Fresh(const std::string& name) {
  fs::path d = fs::temp_directory_path() / ("mc360sdk_" + name);
  fs::remove_all(d);
  fs::create_directories(d);
  return d;
}
void RandomFile(const fs::path& p, size_t size, unsigned seed) {
  std::string data(size, '\0');
  for (size_t i = 0; i < size; ++i) {
    seed = seed * 1103515245u + 12345u;
    data[i] = char(seed >> 16);
  }
  std::ofstream(p, std::ios::binary).write(data.data(), std::streamsize(data.size()));
}

const CoreInfo kCores[] = {
    {"tu9", "Title Update 9", "libmc360core_tu9.dylib", true, 0x584111F7u, 0x7CD33B56u,
     0x00000A01u, true, "tu", "userdata", "cache"},
};

using Listing = std::map<std::string, std::string>;  // path -> contents

Listing ListWithSdkDiscReader(const fs::path& image) {
  Listing out;
  rfs::DiscImageDevice device("", image);
  if (!device.Initialize()) {
    return out;
  }
  std::vector<rfs::Entry*> pending{device.ResolvePath("")};
  while (!pending.empty()) {
    rfs::Entry* entry = pending.back();
    pending.pop_back();
    for (const auto& child : entry->children()) {
      pending.push_back(child.get());
    }
    if (entry->attributes() & rfs::kFileAttributeDirectory) {
      continue;
    }
    std::string path = entry->path();
    NormalizeArchivePath(path);
    std::string data(entry->size(), '\0');
    rfs::File* file = nullptr;
    size_t got = 0;
    if (entry->Open(rfs::FileAccess::kFileReadData, &file) == X_STATUS_SUCCESS && file) {
      file->ReadSync(std::span<uint8_t>(reinterpret_cast<uint8_t*>(data.data()), data.size()), 0,
                     &got);
      file->Destroy();
    }
    out[path] = data;
  }
  return out;
}

Listing ListWithArchive(Archive& archive) {
  Listing out;
  for (const ArchiveFile& file : archive.files()) {
    std::string data(file.size, '\0');
    CHECK(archive.Read(file, 0,
                       std::span<uint8_t>(reinterpret_cast<uint8_t*>(data.data()), data.size())));
    out[file.path] = data;
  }
  return out;
}

}  // namespace

int main(int, char** argv) {
  tools = argv[1];
  const fs::path src = Fresh("src");
  Run("python3 " + tools + "/mkxex.py " + (src / "default.xex").string() +
      " 584111F7 7CD33B56 00000000 00000000");
  Run("python3 " + tools + "/mkxex.py " + (src / "default.xexp").string() +
      " 584111F7 7CD33B56 00000A01 00000000 00000A01");
  RandomFile(src / "big.bin", 1300000, 1);  // 318 STFS blocks: crosses a hash table
  RandomFile(src / "terrain.png", 5000, 2);
  const std::string s = src.string();

  // --- Title update package through the SDK's STFS reader.
  const fs::path base = Fresh("base");
  const fs::path package = base / "TU_16L61V6_0000014000000.00000000000O9";
  Run("python3 " + tools + "/mkstfs.py '" + package.string() + "' 584111F7 00000A01 default.xexp=" +
      s + "/default.xexp res/1_2_2/terrain.png=" + s + "/terrain.png res/big.bin=" + s +
      "/big.bin > /dev/null");
  {
    std::string error;
    auto archive = OpenStfsArchive(package, &error);
    CHECK(archive);
    if (archive) {
      Listing listing = ListWithArchive(*archive);
      CHECK(listing.size() == 3);
      CHECK(listing["default.xexp"] == Slurp(src / "default.xexp"));
      CHECK(listing["res/1_2_2/terrain.png"] == Slurp(src / "terrain.png"));
      CHECK(listing["res/big.bin"] == Slurp(src / "big.bin"));
      // Reads at offsets that don't line up with blocks.
      for (const ArchiveFile& file : archive->files()) {
        if (file.path != "res/big.bin") {
          continue;
        }
        std::string data(file.size, '\0');
        for (size_t offset = 0; offset < file.size; offset += 4097) {
          const size_t n = std::min<size_t>(4097, file.size - offset);
          CHECK(archive->Read(
              file, offset,
              std::span<uint8_t>(reinterpret_cast<uint8_t*>(data.data()) + offset, n)));
        }
        CHECK(data == Slurp(src / "big.bin"));
        std::vector<uint8_t> past(10);
        CHECK(!archive->Read(file, file.size - 5, past));
      }
    }
  }

  // --- Disc images: the app's reader against the SDK's, in every layout.
  for (const char* offset : {"0", "FD90000", "2080000"}) {
    const fs::path image = base / (std::string("disc_") + offset + ".iso");
    Run("python3 " + tools + "/mkiso.py " + image.string() + " " + offset +
        " default.xex=" + s + "/default.xex res/terrain.png=" + s +
        "/terrain.png res/sub/deep/big.bin=" + s + "/big.bin Tutorial/Tutorial.ini=" + s +
        "/default.xexp '$SystemUpdate/su.bin=" + s + "/terrain.png' a=" + s + "/terrain.png b=" +
        s + "/default.xexp");
    std::string error;
    auto archive = XdvdfsArchive::Open(image, &error);
    CHECK(archive);
    if (archive) {
      const Listing mine = ListWithArchive(*archive);
      const Listing sdk = ListWithSdkDiscReader(image);
      std::printf("disc layout at 0x%s: SDK %zu files, app %zu files\n", offset, sdk.size(),
                  mine.size());
      CHECK(!sdk.empty() && sdk == mine);
    }
    if (std::string(offset) != "FD90000") {
      fs::remove(image);
    }
  }

  // --- The whole import, as the menu runs it.
  fs::create_directories(base / "game");
  fs::rename(base / "disc_FD90000.iso", base / "Minecraft.iso");
  ArchiveOpener opener = [](const ImportCandidate& c,
                            std::string* error) -> std::unique_ptr<Archive> {
    if (c.kind == ImportCandidate::Kind::kDiscImage) {
      return XdvdfsArchive::Open(c.path, error);
    }
    return OpenStfsArchive(c.path, error);
  };
  auto candidates = FindImportCandidates(base, kCores);
  CHECK(candidates.size() == 2);
  for (const ImportCandidate& candidate : candidates) {
    CHECK(candidate.state == ImportCandidate::State::kReady);
    ImportJob job(candidate, base, kCores, opener);
    job.Start();
    while (!job.finished()) {
    }
    std::printf("import %s: %s\n", candidate.path.filename().string().c_str(),
                job.message().c_str());
    CHECK(job.succeeded());
  }
  CHECK(Slurp(base / "game/default.xex") == Slurp(src / "default.xex"));
  CHECK(Slurp(base / "game/res/sub/deep/big.bin") == Slurp(src / "big.bin"));
  CHECK(!fs::exists(base / "game/$SystemUpdate"));
  CHECK(Slurp(base / "tu/default.xexp") == Slurp(src / "default.xexp"));
  CHECK(Slurp(base / "tu/res/big.bin") == Slurp(src / "big.bin"));
  CHECK(!HasPendingImports(FindImportCandidates(base, kCores)));
  Launcher launcher(base, kCores);
  launcher.Scan();
  CHECK(launcher.cores()[0].issues.empty());
  CHECK(launcher.cores()[0].update_root == base / "tu");

  std::printf(failures ? "%d FAILURES\n" : "ALL PASSED\n", failures);
  return failures != 0;
}
