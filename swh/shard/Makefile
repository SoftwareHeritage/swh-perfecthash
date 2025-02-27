CFLAGS=-D_FILE_OFFSET_BITS=64 -DHASH_DEBUG -Wall -I../.. -g -fprofile-arcs -ftest-coverage
CXXFLAGS=$(CFLAGS) -std=c++17
LDFLAGS=-lcmph -lgtest -lpthread -lstdc++ -lstdc++fs -fprofile-arcs -ftest-coverage

test_shard: shard.o test_shard.o
	$(CXX) -o $@ $^ $(LDFLAGS)

shard.c: shard.h
test_shard.o: test_shard.cpp shard.h
test_shard.cpp: shard.h

format:
	clang-format -i shard.c shard.h test_shard.cpp

check: test_shard
	valgrind --leak-check=full --tool=memcheck ./test_shard
	lcov -d . -c -o test_shard.lcov
	rm -fr html ; genhtml -o html test_shard.lcov

clean:
	rm -f *.o test_shard
