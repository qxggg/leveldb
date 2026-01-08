//
// Created by xinge qi on 26-1-8.
//
// multidisk_smoke_main.cc
// 一个最小可运行的 smoke test：验证 Stage1/2
// 1) enable_multi_disk 时自动创建 data_dirs
// 2) TableCache 能在 data_dirs 里找到 SST（这里通过“关闭后把 SST 从 meta 目录搬到 data_dirs”来验证）

#include <cassert>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

#include "leveldb/db.h"
#include "leveldb/env.h"
#include "leveldb/options.h"
#include "leveldb/status.h"

namespace {

static inline bool EndsWith(const std::string& s, const std::string& suffix) {
  return s.size() >= suffix.size() &&
         s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

static inline void CheckOK(const leveldb::Status& s, const char* what) {
  if (!s.ok()) {
    std::cerr << "[FAIL] " << what << ": " << s.ToString() << "\n";
    std::abort();
  }
}

static inline void PrintFiles(leveldb::Env* env, const std::string& dir,
                              const char* title) {
  std::vector<std::string> files;
  leveldb::Status s = env->GetChildren(dir, &files);
  if (!s.ok()) {
    std::cerr << "[WARN] GetChildren(" << dir << ") failed: " << s.ToString()
              << "\n";
    return;
  }
  std::cerr << title << " (" << dir << "):\n";
  for (const auto& f : files) {
    std::cerr << "  " << f << "\n";
  }
}

}  // namespace

int main(int argc, char** argv) {
  leveldb::Env* env = leveldb::Env::Default();

  // 生成一个唯一的根目录，避免重复运行互相影响
  const uint64_t ts = env->NowMicros();
  const std::string root = std::string("/Users/xingeqi/leveldb_md_smoke_") + std::to_string(ts);
  const std::string meta = root + "/meta";
  const std::string d0 = root + "/d0";
  const std::string d1 = root + "/d1";

  // 只创建 root，data_dirs 由 DB::Open 自动创建（验证 Stage1）
  CheckOK(env->CreateDir(root), "CreateDir(root)");

  std::cerr << "[Smoke] root=" << root << "\n";
  std::cerr << "[Smoke] meta=" << meta << "\n";
  std::cerr << "[Smoke] data_dirs=[" << d0 << ", " << d1 << "]\n";

  leveldb::Options options;
  options.create_if_missing = true;

  // 开启多盘（Stage1）
  options.enable_multi_disk = true;
  options.data_dirs = {d0, d1};
  options.replication_factor = 1;  // Stage3 才会用到复制，这里先设 1

  // 为了尽快产生 SST，调小 write buffer
  options.write_buffer_size = 64 * 1024;  // 64KB
  options.compression = leveldb::kNoCompression;

  leveldb::DB* db = nullptr;
  leveldb::Status s = leveldb::DB::Open(options, meta, &db);
  CheckOK(s, "DB::Open(meta)");

  // 验证 data_dirs 被自动创建
  if (!env->FileExists(d0) || !env->FileExists(d1)) {
    std::cerr << "[FAIL] data_dirs not created by DB::Open\n";
    return 2;
  }
  std::cerr << "[Smoke] data_dirs created OK\n";

  // 写入一些 key，触发 flush
  leveldb::WriteOptions wo;
  wo.sync = false;
  const std::string value(1024, 'v');  // 1KB value

  const int kN = 500;
  std::cerr << "[Smoke] Writing " << kN << " keys...\n";
  for (int i = 0; i < kN; ++i) {
    std::string key = "k" + std::to_string(i);
    CheckOK(db->Put(wo, key, value), "Put");
  }

  // 强制 compact 一下，确保落到 SST
  std::cerr << "[Smoke] Forcing CompactRange...\n";
  db->CompactRange(nullptr, nullptr);  // LevelDB API returns void

  delete db;
  db = nullptr;

  // 现在 SST 默认还在 meta 目录里（因为 Stage3 还没改写路径）
  // 为了验证 Stage2 的“多目录找 SST”，我们把 SST 文件搬到 data_dirs 里。
  std::vector<std::string> meta_files;
  CheckOK(env->GetChildren(meta, &meta_files), "GetChildren(meta)");

  int moved = 0;
  for (const auto& f : meta_files) {
    if (!(EndsWith(f, ".ldb") || EndsWith(f, ".sst"))) continue;
    const std::string src = meta + "/" + f;
    const std::string dst_dir = (moved % 2 == 0) ? d0 : d1;
    const std::string dst = dst_dir + "/" + f;

    leveldb::Status rs = env->RenameFile(src, dst);
    if (!rs.ok()) {
      std::cerr << "[WARN] RenameFile failed: " << src << " -> " << dst
                << " : " << rs.ToString() << "\n";
      continue;
    }
    std::cerr << "[Smoke] moved " << src << " -> " << dst << "\n";
    ++moved;
  }

  if (moved == 0) {
    std::cerr << "[FAIL] No SST files found in meta dir to move. "
                 "Maybe compaction/flush didn't generate tables?\n";
    PrintFiles(env, meta, "[Debug] meta files");
    return 3;
  }

  // 重新打开 DB：此时 SST 在 data_dirs，但 MANIFEST 仍在 meta。
  // Stage2 的 TableCache 会在 data_dirs 里找到这些 SST。
  std::cerr << "[Smoke] Reopening DB (SST files are in data_dirs)...\n";
  CheckOK(leveldb::DB::Open(options, meta, &db), "DB::Open(meta) reopen");

  // 读回若干 key 验证
  leveldb::ReadOptions ro;
  std::string got;
  for (int i = 0; i < 10; ++i) {
    std::string key = "k" + std::to_string(i * 37);  // 取一些分散 key
    got.clear();
    CheckOK(db->Get(ro, key, &got), "Get");
    if (got != value) {
      std::cerr << "[FAIL] value mismatch for key=" << key
                << " got_size=" << got.size() << "\n";
      return 4;
    }
  }

  std::cerr << "[PASS] Multi-disk Stage1/2 smoke test OK.\n";
  std::cerr << "       (We moved SST from meta to data_dirs to validate TableCache lookup.)\n";

  delete db;
  db = nullptr;

  // 可选：不做清理也没问题，因为 root 是唯一的
  return 0;
}