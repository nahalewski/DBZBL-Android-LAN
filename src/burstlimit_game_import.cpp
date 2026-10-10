#include "burstlimit_game_import.h"

#include <CommonCrypto/CommonDigest.h>
#include <archive.h>
#include <archive_entry.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <vector>

namespace burstlimit::install {
namespace {
namespace fs = std::filesystem;
constexpr uint64_t kSector = 2048;
constexpr uint64_t kUsaSize = 7983104;
constexpr uint64_t kMaxBytes = 16ull << 30;
constexpr char kUsaSha1[] = "aec598f88cf51181fc377b148e0b1ad30db4485c";
constexpr std::array<const char*, 3> kArchives = {
    "long2data/long2data_us.cpk", "long2data/stream_us.cpk", "long2data/stream_jp.cpk"};

std::string Lower(std::string value) {
  for (char& c : value) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
  return value;
}

std::string Hash(const std::vector<uint8_t>& data) {
  unsigned char digest[CC_SHA1_DIGEST_LENGTH];
  CC_SHA1(data.data(), static_cast<CC_LONG>(data.size()), digest);
  std::string result;
  for (unsigned char b : digest) {
    result += "0123456789abcdef"[b >> 4];
    result += "0123456789abcdef"[b & 15];
  }
  return result;
}

void CheckCancel(const std::atomic<bool>& cancel) {
  if (cancel.load()) throw std::runtime_error("Setup canceled. Choose your game file to try again.");
}

fs::path SafePath(std::string name) {
  // ZIP directory records use a trailing slash, but no absolute paths,
  // dot components, control characters or platform-specific separators.
  if (!name.empty() && name.back() == '/') name.pop_back();
  if (name.empty() || name.size() > 4096 || name.front() == '/')
    throw std::runtime_error("The game file contains an invalid filename.");
  for (unsigned char c : name) {
    if (c < 32 || c == 127 || c == '\\' || c == ':')
      throw std::runtime_error("The game file contains an invalid filename.");
  }
  size_t start = 0;
  while (start < name.size()) {
    const auto end = name.find('/', start);
    const auto part = name.substr(start, end == std::string::npos ? end : end - start);
    if (part.empty() || part == "." || part == "..")
      throw std::runtime_error("The game file contains an unsafe filename.");
    if (end == std::string::npos) break;
    start = end + 1;
  }
  return fs::path(name);
}

struct Item {
  fs::path path;
  uint64_t offset = 0, size = 0;
};

class Iso {
 public:
  explicit Iso(const fs::path& path, const std::atomic<bool>& cancel)
      : input_(path, std::ios::binary), cancel_(cancel) {
    if (!input_) throw std::runtime_error("Couldn't open the ISO.");
    input_.seekg(0, std::ios::end);
    const auto end = input_.tellg();
    if (end < 0) throw std::runtime_error("Couldn't read the ISO.");
    size_ = static_cast<uint64_t>(end);
    bool found = false;
    for (uint64_t base : {0ull, 0xFB20ull, 0x20600ull, 0x2080000ull, 0xFD90000ull}) {
      const uint64_t offset = base + 32 * kSector;
      if (offset > size_ || size_ - offset < 28) continue;
      const auto header = Read(offset, 28);
      if (std::string(reinterpret_cast<const char*>(header.data()), 20) !=
          "MICROSOFT*XBOX*MEDIA") continue;
      base_ = base;
      Directory(U32(header.data() + 20), U32(header.data() + 24), {}, 0);
      found = true;
      break;
    }
    if (!found) throw std::runtime_error("This isn't an Xbox 360 ISO. Choose a finished .iso or the extracted game ZIP.");
  }

  std::vector<uint8_t> Read(uint64_t offset, uint64_t size) {
    CheckCancel(cancel_);
    if (offset > size_ || size > size_ - offset || size > (32ull << 20))
      throw std::runtime_error("The ISO is incomplete or has an invalid file entry.");
    std::vector<uint8_t> data(static_cast<size_t>(size));
    input_.clear();
    input_.seekg(static_cast<std::streamoff>(offset));
    if (size && !input_.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(size)))
      throw std::runtime_error("Couldn't read the ISO. Check that the download has finished.");
    return data;
  }

  std::vector<Item> files;

 private:
  static uint16_t U16(const uint8_t* p) { return p[0] | (uint16_t(p[1]) << 8); }
  static uint32_t U32(const uint8_t* p) { return U16(p) | (uint32_t(U16(p + 2)) << 16); }

  void Directory(uint32_t sector, uint32_t size, const fs::path& parent, unsigned depth) {
    if (depth > 64 || size < 14 || size > (32u << 20) ||
        !directories_.insert({sector, size}).second)
      throw std::runtime_error("The ISO contains an invalid directory table.");
    const auto table = Read(base_ + uint64_t(sector) * kSector, size);
    std::vector<uint16_t> pending{0};
    std::set<uint16_t> visited;
    while (!pending.empty()) {
      CheckCancel(cancel_);
      const auto node = pending.back(); pending.pop_back();
      const size_t offset = size_t(node) * 4;
      if (!visited.insert(node).second || offset + 14 > table.size())
        throw std::runtime_error("The ISO contains an invalid directory tree.");
      const auto* p = table.data() + offset;
      const auto left = U16(p), right = U16(p + 2);
      const auto child_sector = U32(p + 4), length = U32(p + 8);
      if (!p[13] || offset + 14 + p[13] > table.size())
        throw std::runtime_error("The ISO contains an invalid filename.");
      const auto name = std::string(reinterpret_cast<const char*>(p + 14), p[13]);
      const auto component = SafePath(name);
      if (component.has_parent_path()) throw std::runtime_error("The ISO contains an unsafe filename.");
      const auto path = parent / component;
      if (names_.size() >= 20000 || !names_.insert(Lower(path.generic_string())).second)
        throw std::runtime_error("The ISO contains duplicate or too many filenames.");
      if (left) pending.push_back(left);
      if (right) pending.push_back(right);
      if (parent.empty() && Lower(name) == "$systemupdate") continue;
      if (p[12] & 0x10) {
        if (length) Directory(child_sector, length, path, depth + 1);
      } else {
        const uint64_t start = base_ + uint64_t(child_sector) * kSector;
        if (start > size_ || length > size_ - start)
          throw std::runtime_error("The ISO is incomplete or has an invalid file entry.");
        files.push_back({path, start, length});
      }
    }
  }

  std::ifstream input_;
  const std::atomic<bool>& cancel_;
  uint64_t size_ = 0, base_ = 0;
  std::set<std::pair<uint32_t, uint32_t>> directories_;
  std::set<std::string> names_;
};

struct ArchiveDeleter { void operator()(archive* a) const { archive_read_free(a); } };
using Archive = std::unique_ptr<archive, ArchiveDeleter>;
Archive OpenZip(const fs::path& source) {
  Archive a(archive_read_new());
  if (!a || archive_read_support_format_zip(a.get()) != ARCHIVE_OK ||
      archive_read_open_filename(a.get(), source.c_str(), 1u << 20) != ARCHIVE_OK)
    throw std::runtime_error("Couldn't open the ZIP. Check that the download has finished.");
  return a;
}

bool Next(archive* a, archive_entry** entry) {
  const int result = archive_read_next_header(a, entry);
  if (result == ARCHIVE_EOF) return false;
  if (result != ARCHIVE_OK) throw std::runtime_error("Couldn't read the ZIP. It may be incomplete or damaged.");
  return true;
}

uint64_t ValidateFiles(const std::vector<Item>& files, const std::vector<uint8_t>& xex) {
  if (xex.size() != kUsaSize || Hash(xex) != kUsaSha1)
    throw std::runtime_error("This isn't the supported Dragon Ball Z: Burst Limit USA Xbox 360 version. Choose the USA game file.");
  std::set<std::string> names;
  uint64_t total = 0;
  for (const auto& file : files) {
    const auto name = Lower(file.path.generic_string());
    if (!names.insert(name).second || file.size > kMaxBytes - total)
      throw std::runtime_error("The game file contains duplicate filenames or too much data.");
    total += file.size;
  }
  for (const auto* name : kArchives) {
    const auto file = std::find_if(files.begin(), files.end(), [name](const Item& f) {
      return Lower(f.path.generic_string()) == name && f.size != 0;
    });
    if (file == files.end()) throw std::runtime_error("The game file is missing a required Burst Limit archive.");
  }
  return total;
}

std::string ZipPrefix(const std::vector<Item>& files) {
  std::string prefix;
  bool found = false;
  for (const auto& file : files) {
    if (Lower(file.path.filename().string()) != "default.xex") continue;
    if (found) throw std::runtime_error("The ZIP contains more than one game. Choose a ZIP with just the USA game.");
    found = true;
    prefix = file.path.parent_path().generic_string();
    if (!prefix.empty()) prefix += '/';
  }
  if (!found) throw std::runtime_error("The ZIP has no default.xex. If it contains an ISO, unzip it and choose that ISO.");
  return prefix;
}

fs::path RelativeZipPath(const fs::path& original, const std::string& prefix) {
  const auto name = original.generic_string();
  if (!name.starts_with(prefix)) return {};
  const auto relative = name.substr(prefix.size());
  if (relative.empty() || Lower(relative).starts_with("$systemupdate/")) return {};
  return SafePath(relative);
}

struct Staging {
  fs::path path;
  ~Staging() { if (!path.empty()) { std::error_code ec; fs::remove_all(path, ec); } }
};

Staging MakeStaging(const fs::path& target, uint64_t total) {
  fs::create_directories(target.parent_path());
  if (fs::exists(target) && (!fs::is_directory(target) || !fs::is_empty(target)))
    throw std::runtime_error("The game installation folder already contains files. Move them out before running setup again.");
  if (fs::space(target.parent_path()).available < total + (64ull << 20))
    throw std::runtime_error("Not enough free space. Game setup needs about 3.4 GB.");
  auto path = target;
  path += ".installing-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
  if (!fs::create_directory(path)) throw std::runtime_error("Couldn't create the game installation folder.");
  return {path};
}

std::ofstream CreateFile(const fs::path& root, const fs::path& relative) {
  const auto path = root / relative;
  fs::create_directories(path.parent_path());
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  if (!file) throw std::runtime_error("Couldn't create " + relative.generic_string() + ".");
  return file;
}

void Publish(Staging& staging, const fs::path& target, const std::atomic<bool>& cancel) {
  CheckCancel(cancel);
  if (!VerifyData(staging.path)) throw std::runtime_error("Game verification failed. Please try setup again.");
  // remove only an empty pre-existing target; never replace installed data.
  if (fs::exists(target)) {
    if (!fs::is_directory(target) || !fs::is_empty(target))
      throw std::runtime_error("The installation folder changed during setup. Existing files were kept.");
    fs::remove(target);
  }
  fs::rename(staging.path, target);
  staging.path.clear();
}
}  // namespace

bool VerifyData(const fs::path& root) {
  std::error_code ec;
  if (root.empty() || fs::file_size(root / "default.xex", ec) != kUsaSize || ec) return false;
  std::ifstream input(root / "default.xex", std::ios::binary);
  std::vector<uint8_t> xex(kUsaSize);
  if (!input.read(reinterpret_cast<char*>(xex.data()), xex.size()) || Hash(xex) != kUsaSha1) return false;
  for (const auto* name : {"LONG2DATA_US.CPK", "STREAM_US.CPK", "STREAM_JP.CPK"}) {
    const auto path = root / "LONG2DATA" / name;
    if (!fs::is_regular_file(path, ec) || ec || !fs::file_size(path, ec) || ec) return false;
  }
  return true;
}

void Import(const fs::path& source, const fs::path& target,
            const std::atomic<bool>& cancel, const Progress& progress) {
  CheckCancel(cancel);
  if (Lower(source.extension().string()) == ".iso") {
    Iso disc(source, cancel);
    const auto xex = std::find_if(disc.files.begin(), disc.files.end(), [](const Item& f) {
      return Lower(f.path.generic_string()) == "default.xex";
    });
    if (xex == disc.files.end() || xex->size != kUsaSize)
      throw std::runtime_error("This ISO doesn't contain the supported USA Xbox 360 game.");
    const auto total = ValidateFiles(disc.files, disc.Read(xex->offset, xex->size));
    auto staging = MakeStaging(target, total);
    std::stable_sort(disc.files.begin(), disc.files.end(), [](const Item& a, const Item& b) {
      const bool ax = Lower(a.path.generic_string()) == "default.xex";
      const bool bx = Lower(b.path.generic_string()) == "default.xex";
      return ax != bx ? !ax : a.offset < b.offset;
    });
    uint64_t done = 0;
    for (const auto& file : disc.files) {
      CheckCancel(cancel);
      auto out = CreateFile(staging.path, file.path);
      for (uint64_t copied = 0; copied < file.size;) {
        auto data = disc.Read(file.offset + copied, std::min<uint64_t>(file.size - copied, 8u << 20));
        if (!out.write(reinterpret_cast<char*>(data.data()), data.size()))
          throw std::runtime_error("Couldn't save the game files. Check free disk space.");
        copied += data.size(); done += data.size();
        progress(done, total, file.path.generic_string());
      }
      out.close();
      if (!out) throw std::runtime_error("Couldn't finish saving the game files.");
    }
    Publish(staging, target, cancel);
  } else if (Lower(source.extension().string()) == ".zip") {
    auto zip = OpenZip(source);
    std::vector<Item> original;
    std::vector<uint8_t> xex;
    archive_entry* entry;
    std::set<std::string> names;
    while (Next(zip.get(), &entry)) {
      CheckCancel(cancel);
      const auto* raw_name = archive_entry_pathname(entry);
      if (!raw_name) throw std::runtime_error("The ZIP contains an invalid filename.");
      const auto path = SafePath(raw_name);
      if (names.size() >= 20000 || !names.insert(Lower(path.generic_string())).second)
        throw std::runtime_error("The ZIP contains duplicate or too many filenames.");
      const auto type = archive_entry_filetype(entry);
      if ((type != AE_IFREG && type != AE_IFDIR) || archive_entry_is_encrypted(entry))
        throw std::runtime_error("The ZIP contains unsupported entries or needs a password.");
      if (type == AE_IFDIR) continue;
      const auto size = archive_entry_size(entry);
      if (size < 0 || uint64_t(size) > kMaxBytes)
        throw std::runtime_error("The ZIP contains an invalid file size.");
      original.push_back({path, 0, static_cast<uint64_t>(size)});
      if (Lower(path.filename().string()) == "default.xex") {
        if (size != kUsaSize) throw std::runtime_error("This ZIP doesn't contain the supported USA Xbox 360 game.");
        xex.resize(kUsaSize);
        size_t done = 0;
        while (done < xex.size()) {
          CheckCancel(cancel);
          const auto read = archive_read_data(zip.get(), xex.data() + done, xex.size() - done);
          if (read <= 0) throw std::runtime_error("Couldn't verify default.xex in the ZIP.");
          done += read;
        }
      }
    }
    const auto prefix = ZipPrefix(original);
    std::vector<Item> files;
    std::map<std::string, uint64_t> expected;
    for (const auto& file : original) {
      const auto path = RelativeZipPath(file.path, prefix);
      if (path.empty()) continue;
      files.push_back({path, 0, file.size});
      expected.emplace(path.generic_string(), file.size);
    }
    const auto total = ValidateFiles(files, xex);
    auto staging = MakeStaging(target, total);
    zip = OpenZip(source);
    // macOS worker threads have a small default stack. Keep copy buffers on
    // the heap even when importing an ISO (the compiler reserves frame space
    // for both branches of this function).
    std::vector<uint8_t> buffer(1u << 20);
    uint64_t done = 0;
    while (Next(zip.get(), &entry)) {
      CheckCancel(cancel);
      const auto* raw_name = archive_entry_pathname(entry);
      if (!raw_name) throw std::runtime_error("The ZIP changed during setup.");
      const auto path = RelativeZipPath(SafePath(raw_name), prefix);
      if (path.empty() || archive_entry_filetype(entry) == AE_IFDIR) continue;
      const auto found = expected.find(path.generic_string());
      if (found == expected.end() || archive_entry_filetype(entry) != AE_IFREG ||
          archive_entry_size(entry) < 0 || uint64_t(archive_entry_size(entry)) != found->second)
        throw std::runtime_error("The ZIP changed during setup. Please choose it again.");
      auto out = CreateFile(staging.path, path);
      uint64_t copied = 0;
      for (;;) {
        CheckCancel(cancel);
        const auto read = archive_read_data(zip.get(), buffer.data(), buffer.size());
        if (read < 0) throw std::runtime_error("Couldn't extract the ZIP. It may be damaged.");
        if (!read) break;
        if (uint64_t(read) > found->second - copied)
          throw std::runtime_error("The ZIP contains an invalid file size.");
        if (!out.write(reinterpret_cast<char*>(buffer.data()), read))
          throw std::runtime_error("Couldn't save the game files. Check free disk space.");
        copied += read; done += read;
        progress(done, total, path.generic_string());
      }
      out.close();
      if (!out || copied != found->second) throw std::runtime_error("The ZIP is incomplete or couldn't be saved.");
      expected.erase(found);
    }
    if (!expected.empty()) throw std::runtime_error("The ZIP changed during setup. Some game files are missing.");
    Publish(staging, target, cancel);
  } else {
    throw std::runtime_error("Choose an Xbox 360 .iso or a .zip containing the extracted USA game.");
  }
}
}  // namespace burstlimit::install
