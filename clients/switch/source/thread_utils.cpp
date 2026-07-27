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
    pthread_attr_setstacksize(&attr, 256 * 1024);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

    auto *args = new ThreadArgs { std::move(fn) };
    pthread_t thread;
    if (pthread_create(&thread, &attr, trampoline, args) != 0) {
        delete args;
    }

    pthread_attr_destroy(&attr);
}

} // namespace thread_utils
