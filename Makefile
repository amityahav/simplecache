CXX = c++
CXXFLAGS = -std=c++17 -Wall -Wextra

# Object files for library
shard.o: shard.cpp shard.hpp
	$(CXX) $(CXXFLAGS) -c shard.cpp -o shard.o

buffer_pool.o: buffer_pool.cpp buffer_pool.hpp shard.hpp
	$(CXX) $(CXXFLAGS) -c buffer_pool.cpp -o buffer_pool.o

# With test main
cache_test: main.cpp shard.o buffer_pool.o
	$(CXX) $(CXXFLAGS) -DCACHE_MAIN main.cpp shard.o buffer_pool.o -o cache_test

.PHONY: clean
clean:
	rm -f cache.o shard.o buffer_pool.o cache_test
