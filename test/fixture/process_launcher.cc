#include "process_launcher.h"

ProcessLauncher::ProcessLauncher() {
    void* mem = mmap(nullptr, sizeof(SharedStore), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANON, -1, 0);
    if (mem == MAP_FAILED) {
        perror("mmap"); 
        std::exit(1);
    }
    store_ = new(mem) SharedStore();
}

ProcessLauncher::~ProcessLauncher() {
    munmap(store_, sizeof(SharedStore));
}

uint32_t ProcessLauncher::addTask(const ProcessTask& f) {
    uint32_t id = store_->next.fetch_add(1);
    if (id >= SharedStore::MAX_TASK) {
        throw std::runtime_error("task overflow");
    }
    new (store_->at(id)) ProcessTask(f);
    std::atomic_thread_fence(std::memory_order_release);
    return id;
}

Processor ProcessLauncher::Launch(const std::string& name, ProcessTask task) {
    int cmdPipe[2], resPipe[2];
    if (pipe(cmdPipe) || pipe(resPipe)) {
        return {};
    }

    pid_t pid = fork();
    if (pid < 0) {
        return {};
    }

    // child process
    if (pid == 0) { 
        close(cmdPipe[1]); // child read
        close(resPipe[0]); // child write
        ChildLoop(cmdPipe[0], resPipe[1], store_);
        exit(0);
    }

    // parent process
    close(cmdPipe[0]); // parent write
    close(resPipe[1]); // parent read

    if (task) {
        uint32_t taskIdx = addTask(task);
        Cmd cmd{taskIdx, ProcessCmdType::RUN};
        write(cmdPipe[1], &cmd, sizeof(cmd));
    }

    return Processor(pid, name, cmdPipe[1], resPipe[0]);
}

bool ProcessLauncher::Dispatch(const Processor& p, ProcessTask task) {
    if (!p) {
        return false;
    }
    uint32_t idx = addTask(task);
    Cmd cmd{idx, ProcessCmdType::RUN};
    return write(p.GetCmdFd(), &cmd, sizeof(cmd)) == sizeof(cmd);
}

bool ProcessLauncher::SendStop(const Processor& p) {
    if (!p) {
        return false;
    }
    Cmd cmd{0, ProcessCmdType::STOP};
    ssize_t n = ::write(p.GetCmdFd(), &cmd, sizeof(cmd));
    if (n == -1 && errno == EPIPE) {
        return true;
    }
    return n == sizeof(cmd);    
}

std::vector<TaskResult> ProcessLauncher::Wait(const std::vector<Processor>& ps, std::chrono::milliseconds timeout) {
    std::vector<TaskResult> results(ps.size());
    std::vector<pollfd> fds;
    fds.reserve(ps.size());
    for (auto& p: ps) {
        pollfd fd{p.GetResFd(), POLLIN, 0};
        fds.push_back(fd);
    }

    auto start = std::chrono::steady_clock::now();
    size_t done = 0;
    while (done < ps.size()) {
        int ret = poll(fds.data(), fds.size(), 
            std::chrono::duration_cast<std::chrono::milliseconds>(timeout).count());

        auto now = std::chrono::steady_clock::now();
        bool overtime = (now - start) >= timeout;

        for (size_t i = 0; i < ps.size(); ++i) {
            if (results[i].name.empty()) {
                results[i].name = ps[i].GetName();
            }
            if (fds[i].revents & POLLIN) {
                Res res;
                ssize_t n = read(ps[i].GetResFd(), &res, sizeof(res));
                if (n == sizeof(res)) {
                    results[i].status = res.status;
                    results[i].detail = res.msg.ToString();
                    done++;
                }
            }

            int status;
            pid_t w = waitpid(ps[i].GetPid(), &status, WNOHANG);
            if (w == ps[i].GetPid() && results[i].status == shmap::Status::SUCCESS) {
                results[i].status = shmap::Status::CRASH;
                if (WIFSIGNALED(status)) {
                    results[i].detail = strsignal(WTERMSIG(status));
                }
                done++;
            }
        }

        if (overtime) {
            for (size_t i = 0; i < ps.size(); ++i) {
                if (results[i].status == shmap::Status::SUCCESS) {
                    kill(ps[i].GetPid(), SIGTERM);
                    results[i].status = shmap::Status::TIMEOUT;
                    results[i].detail = "timeout";
                    done++;
                }
            }
        }
    }
    return results;
}

void ProcessLauncher::ChildLoop(int cmdR, int resW, SharedStore* store) {
    while (true) {
        Cmd cmd;

        ssize_t n = read(cmdR, &cmd, sizeof(cmd));
        if (n != sizeof(cmd)) {
            break;
        }

        std::atomic_thread_fence(std::memory_order_acquire);

        if (cmd.type == ProcessCmdType::STOP) {
             break;
        }

        shmap::Status status = shmap::Status::SUCCESS;
        std::string msg = "success";
        try {
            auto* task = store->at(cmd.idx);
            if (task) {
                (*task)();
                task->~ProcessTask();  // recycle
            } else {
                shmap::Status status = shmap::Status::NOT_FOUND;
                std::string msg = "task nil";
            }
        } catch (const std::exception& e) {
            status  = shmap::Status::EXCEPTION;
            msg = e.what();
        } catch (...) {
            status  = shmap::Status::EXCEPTION;
            msg = "unknown";
        }
        Res res;
        res.idx   = cmd.idx;
        res.status = status;
        res.msg = msg;
        write(resW, &res, sizeof(res));
    }
}
