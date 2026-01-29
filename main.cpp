#ifdef CACHE_MAIN
#include "buffer_pool.hpp"
#include "shard.hpp"
#include <atomic>
#include <cassert>
#include <cstring>
#include <iostream>
#include <thread>
#include <vector>

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

  // Concurrent: many threads Put distinct offsets, then all Get and verify
  {
    const char* conc_path = "cache_concurrent_test.dat";
    auto bp = cache::BufferPool::New(conc_path);
    ASSERT(bp);
    const int numThreads = 8;
    const int offsetsPerThread = 50;
    std::vector<std::thread> threads;
    std::atomic<int> putErrors{0};
    for (int t = 0; t < numThreads; ++t) {
      threads.emplace_back([bp = bp.get(), t, &putErrors]() {
        cache::PageBuf w{};
        for (int i = 0; i < offsetsPerThread; ++i) {
          int off = t * 1000 + i;
          w.fill(0);
          w[0] = static_cast<uint8_t>(off & 0xff);
          w[1] = static_cast<uint8_t>((off >> 8) & 0xff);
          if (!bp->Put(w, off)) putErrors++;
        }
      });
    }
    for (auto& th : threads) th.join();
    ASSERT(putErrors == 0);

    std::atomic<int> getErrors{0};
    threads.clear();
    for (int t = 0; t < numThreads; ++t) {
      threads.emplace_back([bp = bp.get(), t, &getErrors]() {
        cache::PageBuf r{};
        for (int i = 0; i < offsetsPerThread; ++i) {
          int off = t * 1000 + i;
          r.fill(0xff);
          if (!bp->Get(r, off)) {
            getErrors++;
            continue;
          }
          uint8_t expected0 = static_cast<uint8_t>(off & 0xff);
          uint8_t expected1 = static_cast<uint8_t>((off >> 8) & 0xff);
          if (r[0] != expected0 || r[1] != expected1) getErrors++;
        }
      });
    }
    for (auto& th : threads) th.join();
    ASSERT(getErrors == 0);
  }

  // Concurrent: mixed readers and writers on the same set of offsets
  {
    const char* mixed_path = "cache_mixed_test.dat";
    auto bp = cache::BufferPool::New(mixed_path);
    ASSERT(bp);
    const int numOffsets = 32;
    cache::PageBuf w{};
    for (int off = 0; off < numOffsets; ++off) {
      w.fill(0);
      w[0] = static_cast<uint8_t>(off);
      ASSERT(bp->Put(w, off));
    }

    const int numThreads = 6;
    std::vector<std::thread> threads;
    std::atomic<int> errors{0};
    for (int t = 0; t < numThreads; ++t) {
      threads.emplace_back([bp = bp.get(), t, &errors]() {
        cache::PageBuf buf{};
        for (int round = 0; round < 20; ++round) {
          int off = (t + round) % numOffsets;
          if (t % 2 == 0) {
            buf.fill(0);
            buf[0] = static_cast<uint8_t>((off + round) & 0xff);
            if (!bp->Put(buf, off)) errors++;
          } else {
            buf.fill(0xff);
            if (!bp->Get(buf, off)) errors++;
          }
        }
      });
    }
    for (auto& th : threads) th.join();
    ASSERT(errors == 0);
  }

  // Concurrent: many readers and writers hammering same offset (stress entry lock)
  {
    const char* stress_path = "cache_stress_test.dat";
    auto bp = cache::BufferPool::New(stress_path);
    ASSERT(bp);
    cache::PageBuf w{};
    w[0] = 0;
    ASSERT(bp->Put(w, 0));

    const int numThreads = 8;
    const int iters = 200;
    std::vector<std::thread> threads;
    std::atomic<int> errors{0};
    for (int t = 0; t < numThreads; ++t) {
      threads.emplace_back([bp = bp.get(), t, &errors]() {
        cache::PageBuf buf{};
        for (int i = 0; i < iters; ++i) {
          if (t % 2 == 0) {
            buf.fill(0);
            buf[0] = static_cast<uint8_t>(i & 0xff);
            if (!bp->Put(buf, 0)) errors++;
          } else {
            buf.fill(0);
            if (!bp->Get(buf, 0)) errors++;
          }
        }
      });
    }
    for (auto& th : threads) th.join();
    ASSERT(errors == 0);
  }

  std::cout << "All tests passed.\n";
  return 0;
}
#endif
