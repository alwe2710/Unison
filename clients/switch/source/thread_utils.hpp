#pragma once

#include <functional>

// Spawns a detached background thread with an explicitly larger stack than
// std::thread's devkitA64 default. Used for the discovery/search work in
// menu_activity.cpp: that default has been observed to be too small once
// std::string/std::vector/std::function-heavy work (host scanning, HTTP
// parsing, capturing several variables per lambda) runs on it, corrupting
// the heap in a way that only surfaces later, in unrelated code -- see the
// crash report/backtrace in the commit this was introduced in, which
// pointed at borealis's Logger of all things, several calls removed from
// anything this file touches.
namespace thread_utils {
void spawnDetached(std::function<void()> fn);
} // namespace thread_utils
