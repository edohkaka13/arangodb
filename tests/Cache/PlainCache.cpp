////////////////////////////////////////////////////////////////////////////////
/// @brief test suite for arangodb::cache::PlainCache
///
/// @file
///
/// DISCLAIMER
///
/// Copyright 2017 ArangoDB GmbH, Cologne, Germany
///
/// Licensed under the Apache License, Version 2.0 (the "License");
/// you may not use this file except in compliance with the License.
/// You may obtain a copy of the License at
///
///     http://www.apache.org/licenses/LICENSE-2.0
///
/// Unless required by applicable law or agreed to in writing, software
/// distributed under the License is distributed on an "AS IS" BASIS,
/// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
/// See the License for the specific language governing permissions and
/// limitations under the License.
///
/// Copyright holder is ArangoDB GmbH, Cologne, Germany
///
/// @author Daniel H. Larkin
/// @author Copyright 2017, ArangoDB GmbH, Cologne, Germany
////////////////////////////////////////////////////////////////////////////////

#include "Cache/PlainCache.h"
#include "Basics/Common.h"
#include "Cache/Common.h"
#include "Cache/Manager.h"
#include "Random/RandomGenerator.h"

#include "MockScheduler.h"
#include "catch.hpp"

#include <stdint.h>
#include <string>
#include <thread>
#include <vector>

using namespace arangodb;
using namespace arangodb::cache;

TEST_CASE("cache::PlainCache", "[cache][!hide][longRunning]") {
  SECTION("test basic cache creation") {
    Manager manager(nullptr, 1024ULL * 1024ULL);
    auto cache1 =
        manager.createCache(CacheType::Plain, 256ULL * 1024ULL, false);
    REQUIRE(true);
    auto cache2 =
        manager.createCache(CacheType::Plain, 512ULL * 1024ULL, false);

    REQUIRE(0ULL == cache1->usage());
    REQUIRE(256ULL * 1024ULL == cache1->limit());
    REQUIRE(0ULL == cache2->usage());
    REQUIRE(512ULL * 1024ULL > cache2->limit());

    manager.destroyCache(cache1);
    manager.destroyCache(cache2);
  }

  SECTION("check that insertion works as expected") {
    uint64_t cacheLimit = 256ULL * 1024ULL;
    Manager manager(nullptr, 4ULL * cacheLimit);
    auto cache = manager.createCache(CacheType::Plain, cacheLimit, false);

    for (uint64_t i = 0; i < 1024; i++) {
      CachedValue* value =
          CachedValue::construct(&i, sizeof(uint64_t), &i, sizeof(uint64_t));
      bool success = cache->insert(value);
      REQUIRE(success);
      auto f = cache->find(&i, sizeof(uint64_t));
      REQUIRE(f.found());
    }

    for (uint64_t i = 0; i < 1024; i++) {
      uint64_t j = 2 * i;
      CachedValue* value =
          CachedValue::construct(&i, sizeof(uint64_t), &j, sizeof(uint64_t));
      bool success = cache->insert(value);
      REQUIRE(success);
      auto f = cache->find(&i, sizeof(uint64_t));
      REQUIRE(f.found());
      REQUIRE(0 == memcmp(f.value()->value(), &j, sizeof(uint64_t)));
    }

    uint64_t notInserted = 0;
    for (uint64_t i = 1024; i < 128 * 1024; i++) {
      CachedValue* value =
          CachedValue::construct(&i, sizeof(uint64_t), &i, sizeof(uint64_t));
      bool success = cache->insert(value);
      if (success) {
        auto f = cache->find(&i, sizeof(uint64_t));
        REQUIRE(f.found());
      } else {
        delete value;
        notInserted++;
      }
    }
    REQUIRE(notInserted > 0);

    manager.destroyCache(cache);
  }

  SECTION("test that removal works as expected") {
    uint64_t cacheLimit = 256ULL * 1024ULL;
    Manager manager(nullptr, 4ULL * cacheLimit);
    auto cache = manager.createCache(CacheType::Plain, cacheLimit, false);

    for (uint64_t i = 0; i < 1024; i++) {
      CachedValue* value =
          CachedValue::construct(&i, sizeof(uint64_t), &i, sizeof(uint64_t));
      bool success = cache->insert(value);
      REQUIRE(success);
      auto f = cache->find(&i, sizeof(uint64_t));
      REQUIRE(f.found());
      REQUIRE(f.value() != nullptr);
      REQUIRE(f.value()->sameKey(&i, sizeof(uint64_t)));
    }

    // test removal of bogus keys
    for (uint64_t i = 1024; i < 2048; i++) {
      bool removed = cache->remove(&i, sizeof(uint64_t));
      BOOST_ASSERT(removed);
      // ensure existing keys not removed
      for (uint64_t j = 0; j < 1024; j++) {
        auto f = cache->find(&j, sizeof(uint64_t));
        REQUIRE(f.found());
        REQUIRE(f.value() != nullptr);
        REQUIRE(f.value()->sameKey(&j, sizeof(uint64_t)));
      }
    }

    // remove actual keys
    for (uint64_t i = 0; i < 1024; i++) {
      bool removed = cache->remove(&i, sizeof(uint64_t));
      REQUIRE(removed);
      auto f = cache->find(&i, sizeof(uint64_t));
      REQUIRE(!f.found());
    }

    manager.destroyCache(cache);
  }

  SECTION("verify that cache can indeed grow when it runs out of space") {
    uint64_t initialSize = 16ULL * 1024ULL;
    uint64_t minimumSize = 64ULL * initialSize;
    MockScheduler scheduler(4);
    Manager manager(scheduler.ioService(), 1024ULL * 1024ULL * 1024ULL);
    auto cache = manager.createCache(CacheType::Plain, initialSize, true);

    for (uint64_t i = 0; i < 4ULL * 1024ULL * 1024ULL; i++) {
      CachedValue* value =
          CachedValue::construct(&i, sizeof(uint64_t), &i, sizeof(uint64_t));
      bool success = cache->insert(value);
      if (!success) {
        delete value;
      }
    }

    CHECK(cache->usage() > minimumSize);

    manager.destroyCache(cache);
  }

  SECTION("test that cache can shrink when requested") {
    uint64_t initialSize = 16ULL * 1024ULL;
    RandomGenerator::initialize(RandomGenerator::RandomType::MERSENNE);
    MockScheduler scheduler(4);
    Manager manager(scheduler.ioService(), 1024ULL * 1024ULL * 1024ULL);
    auto cache = manager.createCache(CacheType::Plain, initialSize, true);

    for (uint64_t i = 0; i < 16ULL * 1024ULL * 1024ULL; i++) {
      CachedValue* value =
          CachedValue::construct(&i, sizeof(uint64_t), &i, sizeof(uint64_t));
      bool success = cache->insert(value);
      if (!success) {
        delete value;
      }
    }

    cache->disableGrowth();
    uint64_t target = cache->usage() / 2;
    while (!cache->resize(target)) {
    };

    for (uint64_t i = 0; i < 16ULL * 1024ULL * 1024ULL; i++) {
      CachedValue* value =
          CachedValue::construct(&i, sizeof(uint64_t), &i, sizeof(uint64_t));
      bool success = cache->insert(value);
      if (!success) {
        delete value;
      }
    }

    while (cache->isResizing()) {
    }
    REQUIRE(cache->usage() <= target);

    manager.destroyCache(cache);
    RandomGenerator::shutdown();
  }

  SECTION("test behavior under mixed load") {
    uint64_t initialSize = 16ULL * 1024ULL;
    RandomGenerator::initialize(RandomGenerator::RandomType::MERSENNE);
    MockScheduler scheduler(4);
    Manager manager(scheduler.ioService(), 1024ULL * 1024ULL * 1024ULL);
    size_t threadCount = 4;
    std::shared_ptr<Cache> cache =
        manager.createCache(CacheType::Plain, initialSize, true);

    uint64_t chunkSize = 16 * 1024 * 1024;
    uint64_t initialInserts = 4 * 1024 * 1024;
    uint64_t operationCount = 16 * 1024 * 1024;
    std::atomic<uint64_t> hitCount(0);
    std::atomic<uint64_t> missCount(0);
    auto worker = [&manager, &cache, initialInserts, operationCount, &hitCount,
                   &missCount](uint64_t lower, uint64_t upper) -> void {
      // fill with some initial data
      for (uint64_t i = 0; i < initialInserts; i++) {
        uint64_t item = lower + i;
        CachedValue* value = CachedValue::construct(&item, sizeof(uint64_t),
                                                    &item, sizeof(uint64_t));
        bool ok = cache->insert(value);
        if (!ok) {
          delete value;
        }
      }

      // initialize valid range for keys that *might* be in cache
      uint64_t validLower = lower;
      uint64_t validUpper = lower + initialInserts - 1;

      // commence mixed workload
      for (uint64_t i = 0; i < operationCount; i++) {
        uint32_t r = RandomGenerator::interval(static_cast<uint32_t>(99UL));

        if (r >= 99) {  // remove something
          if (validLower == validUpper) {
            continue;  // removed too much
          }

          uint64_t item = validLower++;

          cache->remove(&item, sizeof(uint64_t));
        } else if (r >= 95) {  // insert something
          if (validUpper == upper) {
            continue;  // already maxed out range
          }

          uint64_t item = ++validUpper;
          CachedValue* value = CachedValue::construct(&item, sizeof(uint64_t),
                                                      &item, sizeof(uint64_t));
          bool ok = cache->insert(value);
          if (!ok) {
            delete value;
          }
        } else {  // lookup something
          uint64_t item =
              RandomGenerator::interval(static_cast<int64_t>(validLower),
                                        static_cast<int64_t>(validUpper));

          Cache::Finding f = cache->find(&item, sizeof(uint64_t));
          if (f.found()) {
            hitCount++;
            TRI_ASSERT(f.value() != nullptr);
            TRI_ASSERT(f.value()->sameKey(&item, sizeof(uint64_t)));
          } else {
            missCount++;
            TRI_ASSERT(f.value() == nullptr);
          }
        }
      }
    };

    std::vector<std::thread*> threads;
    // dispatch threads
    for (size_t i = 0; i < threadCount; i++) {
      uint64_t lower = i * chunkSize;
      uint64_t upper = ((i + 1) * chunkSize) - 1;
      threads.push_back(new std::thread(worker, lower, upper));
    }

    // join threads
    for (auto t : threads) {
      t->join();
      delete t;
    }

    manager.destroyCache(cache);
    RandomGenerator::shutdown();
  }

  SECTION("test hit rate statistics reporting") {
    uint64_t cacheLimit = 256ULL * 1024ULL;
    Manager manager(nullptr, 4ULL * cacheLimit);
    auto cacheMiss =
        manager.createCache(CacheType::Plain, cacheLimit, false, true);
    auto cacheHit =
        manager.createCache(CacheType::Plain, cacheLimit, false, true);
    auto cacheMixed =
        manager.createCache(CacheType::Plain, cacheLimit, false, true);

    for (uint64_t i = 0; i < 1024; i++) {
      CachedValue* value =
          CachedValue::construct(&i, sizeof(uint64_t), &i, sizeof(uint64_t));
      bool success = cacheHit->insert(value);
      REQUIRE(success);
      value =
          CachedValue::construct(&i, sizeof(uint64_t), &i, sizeof(uint64_t));
      success = cacheMiss->insert(value);
      REQUIRE(success);
      value =
          CachedValue::construct(&i, sizeof(uint64_t), &i, sizeof(uint64_t));
      success = cacheMixed->insert(value);
      REQUIRE(success);
    }

    for (uint64_t i = 0; i < 1024; i++) {
      auto f = cacheHit->find(&i, sizeof(uint64_t));
    }
    {
      auto cacheStats = cacheHit->hitRates();
      auto managerStats = manager.globalHitRates();
      REQUIRE(cacheStats.first == 100.0);
      REQUIRE(cacheStats.second == 100.0);
      REQUIRE(managerStats.first == 100.0);
      REQUIRE(managerStats.second == 100.0);
    }

    for (uint64_t i = 1024; i < 2048; i++) {
      auto f = cacheMiss->find(&i, sizeof(uint64_t));
    }
    {
      auto cacheStats = cacheMiss->hitRates();
      auto managerStats = manager.globalHitRates();
      REQUIRE(cacheStats.first == 0.0);
      REQUIRE(cacheStats.second == 0.0);
      REQUIRE(managerStats.first > 49.0);
      REQUIRE(managerStats.first < 51.0);
      REQUIRE(managerStats.second > 49.0);
      REQUIRE(managerStats.second < 51.0);
    }

    for (uint64_t i = 0; i < 1024; i++) {
      auto f = cacheMixed->find(&i, sizeof(uint64_t));
    }
    for (uint64_t i = 1024; i < 2048; i++) {
      auto f = cacheMixed->find(&i, sizeof(uint64_t));
    }
    {
      auto cacheStats = cacheMixed->hitRates();
      auto managerStats = manager.globalHitRates();
      REQUIRE(cacheStats.first > 49.0);
      REQUIRE(cacheStats.first < 51.0);
      REQUIRE(cacheStats.second > 49.0);
      REQUIRE(cacheStats.second < 51.0);
      REQUIRE(managerStats.first > 49.0);
      REQUIRE(managerStats.first < 51.0);
      REQUIRE(managerStats.second > 49.0);
      REQUIRE(managerStats.second < 51.0);
    }

    manager.destroyCache(cacheHit);
    manager.destroyCache(cacheMiss);
    manager.destroyCache(cacheMixed);
  }
}
