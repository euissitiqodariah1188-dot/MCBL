/* McBL# Virtual Processor XT300 - C++ implementation
 * Handles function dispatch + multi-function concurrency.
 * All public API is exposed as extern "C" via vp_xt300.h.
 */
#include "vp_xt300.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <deque>
#include <unordered_map>
#include <string>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <memory>
#include <algorithm>

/* -----------------------------------------------------------------------
   Task descriptor
   ----------------------------------------------------------------------- */
struct VPTask {
    int         id;
    std::string func_name;

    std::vector<uint8_t> args;
    std::vector<uint8_t> result_data;

    std::atomic<bool> running;
    std::atomic<bool> done;
    int               error_code;

    VPTask(int tid, const char *fn, const void *a, size_t alen)
        : id(tid), func_name(fn ? fn : "")
        , running(false), done(false), error_code(0)
    {
        if (a && alen > 0) {
            args.resize(alen);
            std::memcpy(args.data(), a, alen);
        }
    }
};

/* -----------------------------------------------------------------------
   XT300 processor core
   ----------------------------------------------------------------------- */
struct VPxt300 {
    int                    max_concurrent;
    std::atomic<int>       next_task_id{0};
    std::atomic<int>       active_count{0};

    std::deque<std::shared_ptr<VPTask>>               pending_queue;
    std::unordered_map<int, std::shared_ptr<VPTask>>  all_tasks;

    std::mutex              mtx;
    std::condition_variable cv;
    std::condition_variable done_cv;

    std::vector<std::thread> workers;
    std::atomic<bool>        shutdown_flag{false};

    /* Stats */
    std::atomic<size_t>  submitted{0};
    std::atomic<size_t>  completed{0};
    std::atomic<size_t>  failed{0};

    explicit VPxt300(int max_conc) : max_concurrent(max_conc) {
        /* Start worker threads equal to max_concurrent */
        int nw = std::max(1, max_concurrent);
        workers.reserve((size_t)nw);
        for (int i = 0; i < nw; i++) {
            workers.emplace_back([this] { worker_loop(); });
        }
    }

    ~VPxt300() {
        {
            std::lock_guard<std::mutex> lk(mtx);
            shutdown_flag = true;
        }
        cv.notify_all();
        for (auto &t : workers) {
            if (t.joinable()) t.join();
        }
    }

    void worker_loop() {
        while (true) {
            std::shared_ptr<VPTask> task;
            {
                std::unique_lock<std::mutex> lk(mtx);
                cv.wait(lk, [this] {
                    return shutdown_flag.load() || !pending_queue.empty();
                });
                if (shutdown_flag && pending_queue.empty()) break;
                if (!pending_queue.empty()) {
                    task = pending_queue.front();
                    pending_queue.pop_front();
                    active_count++;
                }
            }
            if (task) {
                task->running = true;
                execute_task(task);
                task->running = false;
                task->done    = true;
                active_count--;
                completed++;
                done_cv.notify_all();
            }
        }
    }

    void execute_task(std::shared_ptr<VPTask> &task) {
        /* Dispatch based on function name.
           Each function may represent a McBL# dev block, inc block,
           or built-in operation.  The args are raw bytes. */

        const std::string &fn = task->func_name;

        if (fn == "__mcbl_noop") {
            /* no-op task for testing */
        } else if (fn == "__mcbl_loop_stress") {
            /* simulate a heavy loop to validate that XT300 handles load */
            volatile long long acc = 0;
            const int64_t *count_ptr = reinterpret_cast<const int64_t *>(
                task->args.empty() ? nullptr : task->args.data());
            int64_t count = count_ptr ? *count_ptr : 1000LL;
            for (int64_t i = 0; i < count; i++) acc += i;
            int64_t res = acc;
            task->result_data.resize(sizeof(res));
            std::memcpy(task->result_data.data(), &res, sizeof(res));
        } else {
            /* Generic McBL# function — result is a stub int64 = 0 */
            int64_t result = 0;
            task->result_data.resize(sizeof(result));
            std::memcpy(task->result_data.data(), &result, sizeof(result));
        }
    }

    int submit(const char *func_name, const void *args, size_t alen) {
        int tid = next_task_id.fetch_add(1);
        auto task = std::make_shared<VPTask>(tid, func_name, args, alen);
        {
            std::lock_guard<std::mutex> lk(mtx);
            all_tasks[tid] = task;
            pending_queue.push_back(task);
            submitted++;
        }
        cv.notify_one();
        return tid;
    }

    int join_task(int task_id) {
        std::unique_lock<std::mutex> lk(mtx);
        done_cv.wait(lk, [this, task_id] {
            auto it = all_tasks.find(task_id);
            if (it == all_tasks.end()) return true;
            return it->second->done.load();
        });
        auto it = all_tasks.find(task_id);
        if (it == all_tasks.end()) return -1;
        return it->second->error_code;
    }

    void join_all() {
        std::unique_lock<std::mutex> lk(mtx);
        done_cv.wait(lk, [this] {
            for (auto &kv : all_tasks)
                if (!kv.second->done) return false;
            return pending_queue.empty() && active_count.load() == 0;
        });
    }

    int running_task(int task_id) {
        std::lock_guard<std::mutex> lk(mtx);
        auto it = all_tasks.find(task_id);
        if (it == all_tasks.end()) return 0;
        return it->second->running.load() ? 1 : 0;
    }

    void *result(int task_id, size_t *out_len) {
        std::lock_guard<std::mutex> lk(mtx);
        auto it = all_tasks.find(task_id);
        if (it == all_tasks.end() || it->second->result_data.empty()) {
            if (out_len) *out_len = 0;
            return nullptr;
        }
        size_t sz = it->second->result_data.size();
        if (out_len) *out_len = sz;
        void *copy = std::malloc(sz);
        if (!copy) return nullptr;
        std::memcpy(copy, it->second->result_data.data(), sz);
        return copy;
    }

    void print_stats() const {
        std::printf("McBL# VPxt300 stats:\n");
        std::printf("  Max concurrent : %d\n",      max_concurrent);
        std::printf("  Workers        : %zu\n",      workers.size());
        std::printf("  Submitted      : %zu\n", submitted.load());
        std::printf("  Completed      : %zu\n", completed.load());
        std::printf("  Failed         : %zu\n", failed.load());
        std::printf("  Active now     : %d\n",  active_count.load());
    }
};

/* -----------------------------------------------------------------------
   Opaque handle wrapper
   ----------------------------------------------------------------------- */
struct VPxt300Handle {
    VPxt300 *impl;
};

/* -----------------------------------------------------------------------
   C API implementation
   ----------------------------------------------------------------------- */
extern "C" {

VPxt300Handle *vpxt300_create(int max_concurrent) {
    if (max_concurrent <= 0) max_concurrent = 4;
    VPxt300Handle *h = (VPxt300Handle *)std::malloc(sizeof(VPxt300Handle));
    if (!h) return nullptr;
    h->impl = new VPxt300(max_concurrent);
    return h;
}

void vpxt300_destroy(VPxt300Handle *vp) {
    if (!vp) return;
    delete vp->impl;
    vp->impl = nullptr;
    std::free(vp);
}

int vpxt300_submit(VPxt300Handle *vp,
                   const char    *func_name,
                   const void    *args,
                   size_t         arg_len) {
    if (!vp || !vp->impl) return -1;
    return vp->impl->submit(func_name, args, arg_len);
}

int vpxt300_join(VPxt300Handle *vp, int task_id) {
    if (!vp || !vp->impl) return -1;
    return vp->impl->join_task(task_id);
}

void vpxt300_join_all(VPxt300Handle *vp) {
    if (!vp || !vp->impl) return;
    vp->impl->join_all();
}

int vpxt300_running(VPxt300Handle *vp, int task_id) {
    if (!vp || !vp->impl) return 0;
    return vp->impl->running_task(task_id);
}

void *vpxt300_result(VPxt300Handle *vp, int task_id, size_t *out_len) {
    if (!vp || !vp->impl) { if (out_len) *out_len = 0; return nullptr; }
    return vp->impl->result(task_id, out_len);
}

void vpxt300_stats(const VPxt300Handle *vp) {
    if (!vp || !vp->impl) return;
    vp->impl->print_stats();
}

} /* extern "C" */
