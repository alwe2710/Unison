#include "thread_utils.hpp"

#include <pthread.h>

namespace {

struct ThreadArgs {
    std::function<void()> fn;
};

void *trampoline(void *arg) {
    auto *args = static_cast<ThreadArgs *>(arg);
    args->fn();
    delete args;
    return nullptr;
}

} // namespace

namespace thread_utils {

void spawnDetached(std::function<void()> fn) {
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    // 0x100000 (1 MiB), not the original 256 KiB: a real-hardware crash
    // report (Program 010000000000100D, PC/LR both inside _malloc_r, called
    // via threadCreate's memalign while allocating *this* function's own new
    // thread's stack) pointed here as the only structurally different thread
    // spawn in the whole codebase -- every other thread (including this
    // same client's own BeaconListener thread) uses std::thread's default
    // attributes instead of a custom pthread_attr_t. 1 MiB isn't a random
    // guess: it's this NSP's own main_thread_stack_size (see
    // finlink-switch_npdm.json), i.e. a size already proven to allocate
    // successfully as a thread stack on this exact title/firmware. Root
    // cause is still unconfirmed without a hardware retest -- see this
    // file's git history for the crash report this was investigated from.
    pthread_attr_setstacksize(&attr, 0x100000);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

    auto *args = new ThreadArgs { std::move(fn) };
    pthread_t thread;
    if (pthread_create(&thread, &attr, trampoline, args) != 0) {
        delete args;
    }

    pthread_attr_destroy(&attr);
}

} // namespace thread_utils
