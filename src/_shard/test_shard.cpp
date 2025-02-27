/*
 * Copyright (C) 2021  The Software Heritage developers
 * See the AUTHORS file at the top-level directory of this distribution
 * License: GNU General Public License version 3, or any later version
 * See top-level LICENSE file for more information
 */

#include <experimental/filesystem>
#include <fcntl.h>
#include <gtest/gtest.h>
#include <random>
#include <sys/types.h>
#include <unistd.h>

extern "C" {
#include "shard.h"
}

using namespace std::experimental;

filesystem::path
create_temporary_directory(unsigned long long max_tries = 1000) {
    auto tmp_dir = filesystem::temp_directory_path();
    unsigned long long i = 0;
    std::random_device dev;
    std::mt19937 prng(dev());
    std::uniform_int_distribution<int> rand(0);
    filesystem::path path;
    while (true) {
        std::stringstream ss;
        ss << std::hex << rand(prng);
        path = tmp_dir / ss.str();
        // true if the directory was created.
        if (filesystem::create_directory(path)) {
            break;
        }
        if (i == max_tries) {
            throw std::runtime_error("could not find non-existing directory");
        }
        i++;
    }
    return path;
}

std::string gen_random(const int len) {

    std::string tmp_s;
    static const char alphanum[] = "0123456789"
                                   "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                                   "abcdefghijklmnopqrstuvwxyz";

    tmp_s.reserve(len);
    std::random_device dev;
    std::mt19937 prng(dev());
    std::uniform_int_distribution<int> rand(0, strlen(alphanum) - 1);

    for (int i = 0; i < len; ++i)
        tmp_s += alphanum[rand(prng)];

    return tmp_s;
}

TEST(ShardTest, One) {
    auto tmpdir = create_temporary_directory();
    filesystem::path tmpfile = tmpdir / std::string("shard");
    ASSERT_GE(close(open(tmpfile.c_str(), O_CREAT, 0777)), 0);
    ASSERT_GE(truncate(tmpfile.c_str(), 10 * 1024 * 1024), 0);

    //
    // Create a Read Shard and write a single object
    //
    shard_t *shard = shard_init(tmpfile.c_str());
    ASSERT_NE(shard, nullptr);
    ASSERT_GE(shard_prepare(shard, 1), 0);
    const char *keyA = "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA";
    const char *objectA = "AAAAA";
    size_t objectA_size = strlen(objectA);
    ASSERT_GE(shard_object_write(shard, keyA, objectA, objectA_size), 0);
    ASSERT_GE(shard_finalize(shard), 0);
    size_t found_size = 0;
    ASSERT_GE(shard_find_object(shard, keyA, &found_size), 0);
    ASSERT_EQ(objectA_size, found_size);
    char *found = (char *)malloc(found_size);
    ASSERT_GE(shard_read_object(shard, found, found_size), 0);
    ASSERT_EQ(memcmp((const void *)objectA, (const void *)found, found_size),
              0);
    free(found);
    ASSERT_GE(shard_destroy(shard), 0);

    //
    // Open the Read Shard created above and verify the object can be
    // looked up.
    //
    shard = shard_init(tmpfile.c_str());
    ASSERT_NE(shard, nullptr);
    ASSERT_GE(shard_load(shard), 0);
    found_size = 0;
    ASSERT_GE(shard_find_object(shard, keyA, &found_size), 0);
    ASSERT_EQ(objectA_size, found_size);
    found = (char *)malloc(found_size);
    ASSERT_GE(shard_read_object(shard, found, found_size), 0);
    ASSERT_EQ(memcmp((const void *)objectA, (const void *)found, found_size),
              0);
    free(found);
    ASSERT_GE(shard_destroy(shard), 0);

    filesystem::remove_all(tmpdir);
}

TEST(ShardTest, Many) {
    auto tmpdir = create_temporary_directory();
    filesystem::path tmpfile = tmpdir / std::string("shard");
    ASSERT_GE(close(open(tmpfile.c_str(), O_CREAT, 0777)), 0);
    ASSERT_GE(truncate(tmpfile.c_str(), 10 * 1024 * 1024), 0);

    std::random_device dev;
    std::mt19937 prng(dev());
    std::uniform_int_distribution<int> rand(0, 80 * 1024);

    //
    // Populate a Read Shard with multiple objects (objects_count)
    // The object content and their keys are from a random source
    // A map is kept in memory in key2object for verification
    //
    std::map<std::string, std::string> key2object;

    shard_t *shard = shard_init(tmpfile.c_str());
    ASSERT_NE(shard, nullptr);
    int objects_count = 10;
    ASSERT_GE(shard_prepare(shard, objects_count), 0);
    for (int i = 0; i < objects_count; i++) {
        std::string key = gen_random(SHARD_KEY_LEN);
        std::string object = gen_random(rand(prng));
        key2object[key] = object;
        std::cout << key << std::endl;
        ASSERT_GE(shard_object_write(shard, key.c_str(), object.c_str(),
                                     object.length()),
                  0);
    }
    ASSERT_GE(shard_finalize(shard), 0);
    ASSERT_GE(shard_destroy(shard), 0);

    //
    // Open the Read Shard for lookups, lookup every key inserted
    // and verify the matching object has the expected content.
    //
    shard = shard_init(tmpfile.c_str());
    ASSERT_NE(shard, nullptr);
    ASSERT_GE(shard_load(shard), 0);
    for (std::pair<std::string, std::string> p : key2object) {
        size_t found_size = 0;
        ASSERT_GE(shard_find_object(shard, p.first.c_str(), &found_size), 0);
        ASSERT_EQ(p.second.length(), found_size);
        char *found = (char *)malloc(found_size);
        ASSERT_GE(shard_read_object(shard, found, found_size), 0);
        ASSERT_EQ(memcmp((const void *)p.second.c_str(), (const void *)found,
                         found_size),
                  0);
        free(found);
    }
    ASSERT_GE(shard_destroy(shard), 0);

    filesystem::remove_all(tmpdir);
}

int main(int argc, char **argv) {
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
