// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include "db/table_cache.h"

#include <vector>

#include "db/filename.h"
#include "leveldb/env.h"
#include "leveldb/table.h"
#include "util/coding.h"

namespace leveldb {

struct TableAndFile {
  RandomAccessFile* file;
  Table* table;
};

static void DeleteEntry(const Slice& key, void* value) {
  TableAndFile* tf = reinterpret_cast<TableAndFile*>(value);
  delete tf->table;
  delete tf->file;
  delete tf;
}

static void UnrefEntry(void* arg1, void* arg2) {
  Cache* cache = reinterpret_cast<Cache*>(arg1);
  Cache::Handle* h = reinterpret_cast<Cache::Handle*>(arg2);
  cache->Release(h);
}

TableCache::TableCache(const std::string& dbname, const Options& options,
                       int entries)
    : env_(options.env),
      dbname_(dbname),
      options_(options),
      cache_(NewLRUCache(entries)) {}

TableCache::~TableCache() { delete cache_; }

Status TableCache::FindTable(uint64_t file_number, uint64_t file_size,
                             Cache::Handle** handle) {
  Status s;
  char buf[sizeof(file_number)];
  EncodeFixed64(buf, file_number);
  Slice key(buf, sizeof(buf));
  *handle = cache_->Lookup(key);
  if (*handle == nullptr) {
    // Multi-disk stage-2: when enabled, search configured data dirs first
    // (preferred by a deterministic order) and fall back to the main DB dir.
    std::vector<std::string> search_dirs;
    auto push_unique_dir = [&](const std::string& dir) {
      if (dir.empty()) return;
      for (const auto& existing : search_dirs) {
        if (existing == dir) return;
      }
      search_dirs.push_back(dir);
    };

    if (options_.enable_multi_disk && !options_.data_dirs.empty()) {
      const size_t n = options_.data_dirs.size();
      const size_t start = static_cast<size_t>(file_number % n);
      for (size_t i = 0; i < n; ++i) {
        push_unique_dir(options_.data_dirs[(start + i) % n]);
      }
    }
    // Always include the main DB directory as a last resort for backward
    // compatibility (e.g., before write-path multi-disk placement is enabled).
    push_unique_dir(dbname_);

    RandomAccessFile* file = nullptr;
    Table* table = nullptr;
    Status last_status = Status::NotFound("table file not found");

    for (const std::string& dir : search_dirs) {
      // Try modern .ldb name first.
      std::string fname = TableFileName(dir, file_number);
      file = nullptr;
      table = nullptr;
      s = env_->NewRandomAccessFile(fname, &file);
      if (!s.ok()) {
        // Then try legacy .sst name.
        std::string old_fname = SSTTableFileName(dir, file_number);
        if (env_->NewRandomAccessFile(old_fname, &file).ok()) {
          s = Status::OK();
        }
      }
      if (s.ok()) {
        s = Table::Open(options_, file, file_size, &table);
      }

      if (s.ok()) {
        TableAndFile* tf = new TableAndFile;
        tf->file = file;
        tf->table = table;
        *handle = cache_->Insert(key, tf, 1, &DeleteEntry);
        return s;
      }

      // Cleanup and try next dir. Useful when one replica is missing/corrupt.
      assert(table == nullptr);
      delete file;
      last_status = s;
    }

    // No dir worked. Return the last error we saw.
    s = last_status;
  }
  return s;
}

Iterator* TableCache::NewIterator(const ReadOptions& options,
                                  uint64_t file_number, uint64_t file_size,
                                  Table** tableptr) {
  if (tableptr != nullptr) {
    *tableptr = nullptr;
  }

  Cache::Handle* handle = nullptr;
  Status s = FindTable(file_number, file_size, &handle);
  if (!s.ok()) {
    return NewErrorIterator(s);
  }

  Table* table = reinterpret_cast<TableAndFile*>(cache_->Value(handle))->table;
  Iterator* result = table->NewIterator(options);
  result->RegisterCleanup(&UnrefEntry, cache_, handle);
  if (tableptr != nullptr) {
    *tableptr = table;
  }
  return result;
}

Status TableCache::Get(const ReadOptions& options, uint64_t file_number,
                       uint64_t file_size, const Slice& k, void* arg,
                       void (*handle_result)(void*, const Slice&,
                                             const Slice&)) {
  Cache::Handle* handle = nullptr;
  Status s = FindTable(file_number, file_size, &handle);
  if (s.ok()) {
    Table* t = reinterpret_cast<TableAndFile*>(cache_->Value(handle))->table;
    s = t->InternalGet(options, k, arg, handle_result);
    cache_->Release(handle);
  }
  return s;
}

void TableCache::Evict(uint64_t file_number) {
  char buf[sizeof(file_number)];
  EncodeFixed64(buf, file_number);
  cache_->Erase(Slice(buf, sizeof(buf)));
}

}  // namespace leveldb
