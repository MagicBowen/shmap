#include <gtest/gtest.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <array>
#include <cstring>
#include <iostream>

#include "shmap.h"
#include "fixed_string.h"

using namespace shmap;

namespace {
    struct Person {
        FixedString name;
        int age;
    };

    struct ShmPath { static constexpr const char* value = "/shm_storage_mp_test"; };
    using Storage = ShmStorage<ShmHashTable<FixedString, Person, 8>, ShmPath>;
}

struct ShmStorgeMpTest : public testing::Test {
protected:
    void SetUp() override {
    }

    void TearDown() override {
        Storage::GetInstance().Destroy();
    }

protected:
   void WorkerProcess() {
        Storage::GetInstance()->Visit("Bowen", AccessMode::CreateIfMiss, [](auto idx, auto& person, bool isNew) {
            if (isNew) {
                person.name = "Bowen";
                person.age = 40;
            } else {
                person.age++;
            }
        });
        _exit(0);
    }
};
    
TEST_F(ShmStorgeMpTest, shm_storage_mp_function_test) {
    pid_t pid = fork();
    if(pid==0)  {
        WorkerProcess();
    } else {
        Storage::GetInstance()->Visit("Jerry", AccessMode::CreateIfMiss, [](auto idx, auto& person, bool isNew) {
            if (isNew) {
                person.name = "Jerry";
                person.age = 15;
            } else {
                person.age++;
            }
        });        
    }

    while(wait(nullptr) > 0) {}

    std::vector<Person> persons;
    Storage::GetInstance()->Travel([&persons](auto idx, auto& key, auto& person) {
        persons.push_back(person);
    });

    ASSERT_EQ(persons.size(), 2);
}
