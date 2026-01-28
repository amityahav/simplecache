#ifndef CACHE_BUFFER_POOL_HPP
#define CACHE_BUFFER_POOL_HPP

#include "shard.hpp"
#include <array>
#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace cache {

/** Configuration for BufferPool: shard count and eviction limit per shard. */
struct BufferPoolConfig {
  int nShards = defaultNShards;
  int maxEntriesPerShard = defaultMaxEntriesPerShard;
};

/** Sharded buffer pool: file-backed page cache with per-shard locking and a background flusher. */
class BufferPool {
 public:
  /** Opens or creates the file at \a path and builds a pool with \a config. Starts the flusher thread. Returns nullptr if the file cannot be opened. */
  static std::unique_ptr<BufferPool> New(const std::string& path,
                                         BufferPoolConfig config = {});

  /** Writes \a buf to the page at file offset \a offset. Returns false on error. */
  bool Put(const PageBuf& buf, int offset);

  /** Reads the page at file offset \a offset into \a buf. Returns false on error. */
  bool Get(PageBuf& buf, int offset);

  /** Starts the background thread that flushes dirty pages every 5 seconds. */
  void startFlusher();

  /** Stops the flusher thread (blocks until it exits). */
  void stopFlusher();

  /** Stops the flusher and closes the file. */
  ~BufferPool();

 private:
  /** Hashes \a key for shard selection. */
  static unsigned hash(int key);
  /** Loop run by the flusher thread: sleep 5s, then flush all shards. */
  void flusher();

  int fd_{-1};
  int nShards_{0};
  std::vector<std::unique_ptr<Shard>> shards_;
  std::thread flusherThread_;
  std::atomic<bool> stopFlusher_{false};
};

} 

#endif 
