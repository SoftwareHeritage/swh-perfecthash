CFLAGS=-D_FILE_OFFSET_BITS=64 -DHASH_DEBUG -Wall -I../.. -g -std=c++17 -fprofile-arcs -ftest-coverage
LDFLAGS=-lcmph -lgtest -lpthread -lstdc++ -lstdc++fs -fprofile-arcs -ftest-coverage

test_hash: hash.o test_hash.o
	$(CXX) -o $@ $^ $(LDFLAGS)

hash.c: hash.h
test_hash.o: test_hash.cpp hash.h
test_hash.cpp: hash.h

format:
	clang-format -i hash.c hash.h test_hash.cpp

check: test_hash
	valgrind --leak-check=full --tool=memcheck ./test_hash
	lcov -d . -c -o test_hash.lcov
	rm -fr html ; genhtml -o html test_hash.lcov

clean:
	rm -f *.o test_hash
