#ifdef CACHE_MAIN
#include "buffer_pool.hpp"
#include "shard.hpp"
#include <cassert>
#include <cstring>
#include <iostream>

#define ASSERT(x) \
  do { \
    if (!(x)) { \
      std::cerr << "FAIL: " #x << "\n"; \
      return 1; \
    } \
  } while (0)

int main() {
  const char* path = "cache_test.dat";

  // Basic Put/Get single byte
  {
    auto bp = cache::BufferPool::New(path);
    ASSERT(bp);
    cache::PageBuf buf{};
    buf[0] = 42;
    ASSERT(bp->Put(buf, 0));
    buf[0] = 0;
    ASSERT(bp->Get(buf, 0));
    ASSERT(buf[0] == 42);
  }

  // Multiple offsets (different shards via hash)
  {
    auto bp = cache::BufferPool::New(path);
    ASSERT(bp);
    cache::PageBuf w{};
    for (int off = 0; off < 100; off += 7) {
      w.fill(0);
      w[0] = static_cast<uint8_t>(off & 0xff);
      w[1] = static_cast<uint8_t>(off >> 8);
      ASSERT(bp->Put(w, off));
    }
    cache::PageBuf r{};
    for (int off = 0; off < 100; off += 7) {
      r.fill(0xff);
      ASSERT(bp->Get(r, off));
      ASSERT(r[0] == static_cast<uint8_t>(off & 0xff));
      ASSERT(r[1] == static_cast<uint8_t>(off >> 8));
    }
  }

  // Overwrite same offset
  {
    auto bp = cache::BufferPool::New(path);
    ASSERT(bp);
    cache::PageBuf w{};
    w[0] = 1;
    ASSERT(bp->Put(w, 0));
    w[0] = 2;
    ASSERT(bp->Put(w, 0));
    cache::PageBuf r{};
    ASSERT(bp->Get(r, 0));
    ASSERT(r[0] == 2);
  }

  // Full page pattern
  {
    auto bp = cache::BufferPool::New(path);
    ASSERT(bp);
    cache::PageBuf w{};
    for (size_t i = 0; i < cache::pageSize; ++i)
      w[i] = static_cast<uint8_t>(i & 0xff);
    ASSERT(bp->Put(w, 1000));
    cache::PageBuf r{};
    ASSERT(bp->Get(r, 1000));
    ASSERT(std::memcmp(w.data(), r.data(), cache::pageSize) == 0);
  }

  // Get of never-written offset (reads from file; zero or existing)
  {
    auto bp = cache::BufferPool::New(path);
    ASSERT(bp);
    cache::PageBuf r{};
    r.fill(0xff);
    ASSERT(bp->Get(r, 9999));
    // After Get, buffer is whatever was on disk or zeros
    (void)r;
  }

  // Eviction: one shard, max 2 entries; put 5 keys, all must be readable
  {
    const char* evict_path = "cache_evict_test.dat";
    cache::BufferPoolConfig config;
    config.nShards = 1;
    config.maxEntriesPerShard = 2;
    auto bp = cache::BufferPool::New(evict_path, config);
    ASSERT(bp);
    cache::PageBuf w{};
    for (int off = 0; off < 5; ++off) {
      w.fill(0);
      w[0] = static_cast<uint8_t>('A' + off);
      ASSERT(bp->Put(w, off));
    }
    cache::PageBuf r{};
    for (int off = 0; off < 5; ++off) {
      r.fill(0);
      ASSERT(bp->Get(r, off));
      ASSERT(r[0] == static_cast<uint8_t>('A' + off));
    }
  }

  std::cout << "All tests passed.\n";
  return 0;
}
#endif
