#ifndef PROCESS_LAUNCHER_H
#define PROCESS_LAUNCHER_H

#include <atomic>
#include <thread>
#include <vector>
#include <string>
#include <chrono>
#include <sstream>
#include <iostream>
#include <functional>

#include <csignal>
#include <cstring>
#include <unistd.h>
#include <poll.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "shmap/status.h"
#include "shmap/fixed_string.h"

/* -------------------------------------------------------------------------- */
/*                                  Basic definitions                         */
/* -------------------------------------------------------------------------- */
struct TaskResult {
    TaskResult() : status(shmap::Status::SUCCESS) {}
    std::string   name;
    shmap::Status status;
    std::string   detail;
};

using ProcessTask = std::function<void()>;

enum class ProcessCmdType : uint32_t {
    RUN  = 1,
    STOP = 2,
};

/* -------------------------------------------------------------------------- */
/*                                     Processor                              */
/* -------------------------------------------------------------------------- */
class Processor {
public:
    Processor() = default;
    explicit Processor(pid_t p, std::string n, int cmdw, int resr) 
    : pid_(p), name_(std::move(n)), cmdWrite_(cmdw), resRead_(resr) {
    }

    operator bool() const { return pid_ > 0; }
    pid_t GetPid() const { return pid_;  }
    int   GetCmdFd() const { return cmdWrite_; }
    int   GetResFd() const { return resRead_;  }
    const std::string& GetName() const { return name_; }

private:
    pid_t pid_{-1};
    std::string name_;
    int cmdWrite_{-1};
    int resRead_{-1};

private:
    friend class ProcessLauncher;
};

/* -------------------------------------------------------------------------- */
/*                               ProcessLauncher                              */
/* -------------------------------------------------------------------------- */

class ProcessLauncher {
public:
    ProcessLauncher();
    ~ProcessLauncher();

    Processor Launch(const std::string& name, ProcessTask task = nullptr);

    bool Dispatch(const Processor& p, ProcessTask task);

    std::vector<TaskResult> wait(const std::vector<Processor>& ps, 
        std::chrono::milliseconds timeout = std::chrono::milliseconds(1000));

    template<typename ...P>
    bool Stop(const P&... ps) { 
        return (SendStop(ps) && ...); 
    }

    bool Stop(std::vector<Processor> ps) {
        bool allStopped = true;
        for (const auto& p : ps) {
            allStopped &= SendStop(p);
        }
        return allStopped;
    }

private:
    uint32_t addTask(const ProcessTask& f);
    bool SendStop(const Processor& p);

private:    
    struct Cmd { 
        Cmd() : idx(0), type(ProcessCmdType::RUN) {}
        Cmd(uint32_t i, ProcessCmdType t) : idx(i), type(t) {}
        uint32_t idx;  // only used in RUN.
        ProcessCmdType type; 
    };

    struct Res {
        Res() : idx(0), status(shmap::Status::SUCCESS), msg("") {}
        uint32_t idx;
        shmap::Status status;
        shmap::FixedString msg;
    };

private:
    struct SharedStore {
        static constexpr uint32_t MAX_TASK = 1024;

        ProcessTask* at(uint32_t idx) {
            return reinterpret_cast<ProcessTask*>(&buf[idx]);
        }

        std::aligned_storage_t<sizeof(ProcessTask), alignof(ProcessTask)> buf[MAX_TASK];
        std::atomic<uint32_t> next{0};
    };

    static void ChildLoop(int cmdR, int resW, SharedStore* store);

private:
    SharedStore* store_;
};

#endif // PROCESS_LAUNCHER_H