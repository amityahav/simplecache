#include "buffer_pool.hpp"
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <unistd.h>

namespace cache {

std::unique_ptr<BufferPool> BufferPool::New(const std::string& path,
                                            BufferPoolConfig config) {
  int fd = open(path.c_str(), O_CREAT | O_RDWR, 0600);
  if (fd < 0) return nullptr;

  auto bp = std::make_unique<BufferPool>();
  bp->fd_ = fd;
  bp->nShards_ = config.nShards;
  bp->shards_.resize(config.nShards);
  for (int i = 0; i < config.nShards; ++i) {
    bp->shards_[i] = std::make_unique<Shard>(fd, config.maxEntriesPerShard);
  }
  bp->startFlusher();
  return bp;
}

bool BufferPool::Put(const PageBuf& buf, int offset) {
  size_t shardId = hash(offset) % nShards_;
  return shards_[shardId]->Put(offset, buf);
}

bool BufferPool::Get(PageBuf& buf, int offset) {
  size_t shardId = hash(offset) % nShards_;
  Entry* e = shards_[shardId]->Get(offset);
  if (!e) return false;

  std::shared_lock<std::shared_mutex> lock(e->mu);
  std::memcpy(buf.data(), e->val.data(), pageSize);
  return true;
}

void BufferPool::startFlusher() {
  flusherThread_ = std::thread([this] {
    flusher(); 
  });
}

void BufferPool::stopFlusher() {
  stopFlusher_ = true;
  if (flusherThread_.joinable()) flusherThread_.join();
}

BufferPool::~BufferPool() {
  stopFlusher();
  if (fd_ >= 0) close(fd_);
}

unsigned BufferPool::hash(int key) {
  unsigned u = static_cast<unsigned>(key);
  u = u * 2654435761u; 
  return u;
}

void BufferPool::flusher() {
  while (!stopFlusher_) {
    std::this_thread::sleep_for(std::chrono::seconds(5));
    if (stopFlusher_) break;
    for (int i = 0; i < nShards_; ++i) {
      if (!shards_[i]->Flush()) {
        std::cerr << "Flush failed for shard " << i << std::endl;
      }
    }
  }
}

}  
