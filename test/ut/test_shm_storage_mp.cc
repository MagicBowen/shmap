#include <gtest/gtest.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <array>
#include <cstring>
#include <iostream>

#include "shmap/shm_storage.h"
#include "shmap/shm_hash_table.h"
#include "shmap/fixed_string.h"

using namespace shmap;

namespace {
    struct Person {
        FixedString name;
        int age;
        long long padding[10240];
    };

    struct ShmPath { static constexpr const char* value = "/shm_storage_mp_test"; };
    using Storage = ShmStorage<ShmHashTable<FixedString, Person, 8>, ShmPath>;

    struct PersonRepo {
        static void Add(const char* name, int age) {
            Storage::GetInstance()->Visit(name, AccessMode::CreateIfMiss, [name, age](auto idx, auto& person, bool isNew) {
                if (isNew) {
                    person.name = FixedString::FromString(name);
                    person.age = age;
                } else {
                    person.age++;
                }
            });
        }
        static Person Get(const char* name) {
            Person person;
            Storage::GetInstance()->Visit(name, AccessMode::AccessExist, [&person](auto idx, auto& p, bool isNew) {
                if (isNew) {
                    throw std::runtime_error("Person not found");
                }
                person = p;
            });
            return person;
        }
        static std::unordered_map<std::string, Person> GetAll() {
            std::unordered_map<std::string, Person> result;
            Storage::GetInstance()->Travel([&result](auto idx, auto& key, auto& person) {
                result[key.ToString()] = person;
            });
            return result;
        }
    };
}

struct ShmStorgeMpTest : public testing::Test {
protected:
    void SetUp() override {
    }

    void TearDown() override {
        Storage::GetInstance().Destroy();
    }
};
    
TEST_F(ShmStorgeMpTest, shm_storage_mp_function_test) {
    pid_t pid = fork();
    if(pid==0)  {
        PersonRepo::Add("Bowen", 40);
        exit(0);
    } else if (pid > 0) {
        PersonRepo::Add("Jerry", 15);
        waitpid(pid, nullptr, 0); // Wait for child process to finish
    } else {
        FAIL() << "Fork failed";
    }

    auto persons = PersonRepo::GetAll();
    ASSERT_EQ(persons.size(), 2);
    ASSERT_EQ(persons["Bowen"].age, 40);
    ASSERT_EQ(persons["Jerry"].age, 15);
}
