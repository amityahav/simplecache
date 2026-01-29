#include "shard.hpp"
#include <cstring>
#include <fcntl.h>
#include <mutex>
#include <shared_mutex>
#include <unistd.h>
#include <vector>

using std::shared_mutex;
using std::unique_lock;
using std::shared_lock;


namespace cache {

Shard::Shard(int fd, int maxEntriesPerShard)
    : fd_(fd), maxEntriesPerShard_(maxEntriesPerShard) {}

bool Shard::Put(int key, const PageBuf& buf) {
  Entry* ent = get(key, false);
  if (!ent) return false;

  unique_lock<shared_mutex> lock(ent->mu);
  std::memcpy(ent->val.data(), buf.data(), pageSize);
  ent->isDirty = true;
  return true;
}

Entry* Shard::Get(int key) { return get(key, true); }

Entry* Shard::get(int key, bool loadFromFile) {
  {
    shared_lock<shared_mutex> lock(mu_);
    auto it = m_.find(key);
    if (it != m_.end()) return it->second.get();
  }

  {
    unique_lock<shared_mutex> lock(mu_);
    if (static_cast<int>(m_.size()) >= maxEntriesPerShard_) {
      if (!evictOneLocked()) return nullptr;
    }
  }

  auto e = std::make_unique<Entry>();
  e->key = key;
  e->isDirty = false;

  if (loadFromFile) {
    ssize_t n = pread(fd_, e->val.data(), pageSize, static_cast<off_t>(key));
    if (n < 0) return nullptr;
    if (n > 0 && n < static_cast<ssize_t>(pageSize))
      return nullptr;
  }

  Entry* ptr = e.get();
  
  unique_lock<shared_mutex> lock(mu_);
  m_[key] = std::move(e);
  return ptr;
}

bool Shard::Flush() {
  std::vector<std::pair<int, Entry*>> dirty;
  {
    unique_lock<shared_mutex> lock(mu_);
    for (auto& p : m_) {
      Entry* e = p.second.get();
      shared_lock<shared_mutex> entryLock(e->mu);
      if (e->isDirty) dirty.emplace_back(p.first, e);
    }
  }

  for (auto& p : dirty) {
    int offset = p.first;
    Entry* e = p.second;
    unique_lock<shared_mutex> lock(e->mu);
    if (!e->isDirty) continue;

    ssize_t n = pwrite(fd_, e->val.data(), pageSize, static_cast<off_t>(offset));
    if (n != static_cast<ssize_t>(pageSize)) continue;

    e->isDirty = false;
  }

  return true;
}

bool Shard::evictOneLocked() {
  for (auto it = m_.begin(); it != m_.end(); ++it) {
    Entry* e = it->second.get();
    if (e->isDirty) {
      ssize_t n =
          pwrite(fd_, e->val.data(), pageSize, static_cast<off_t>(it->first));
      if (n != static_cast<ssize_t>(pageSize)) return false;
    }
    m_.erase(it);
    return true;
  }
  return true;
}

}
