/* LAN discovery for the finlink lobby, mirroring
 * clients/3ds/source/discovery.cpp / clients/switch/source/discovery.cpp's
 * "scan the local /24 for something answering GET / on port 6800" approach,
 * but reworked into a non-blocking step function instead of a blocking
 * call on a background thread -- the NDS build has neither threads nor
 * select()/poll() (see ../../README.md and main.c's runSession() comment),
 * so the caller drives this one step per swiWaitForVBlank() tick instead. */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define DISCOVERY_MAX_HOSTS 254
#define DISCOVERY_CONCURRENCY 4

typedef enum {
    DISCOVERY_SLOT_FREE,
    DISCOVERY_SLOT_CONNECTING,
    DISCOVERY_SLOT_WAITING_RESPONSE,
} DiscoverySlotState;

typedef struct {
    DiscoverySlotState state;
    int fd;
    uint32_t hostIp; /* network byte order, the candidate this slot is probing */
    int ticks;
    size_t respLen;
    char respBuf[48];
} DiscoverySlot;

typedef struct {
    uint32_t hosts[DISCOVERY_MAX_HOSTS]; /* network byte order */
    int hostCount;
    int nextIndex;
    int doneCount;
    bool found;
    uint32_t foundIp; /* network byte order, valid iff found */
    DiscoverySlot slots[DISCOVERY_CONCURRENCY];
} DiscoveryScan;

/* Builds the candidate host list (this console's /24, minus its own
 * address) from ownIp (network byte order, e.g. straight from
 * Wifi_GetIP()) and resets scan for a fresh run. */
void discovery_start(DiscoveryScan *scan, uint32_t ownIp);

/* Advances the scan by one tick (call once per swiWaitForVBlank()).
 * Returns true once the scan is over -- either scan->found is true and
 * scan->foundIp names the first host that answered, or every candidate
 * was tried with no match. */
bool discovery_tick(DiscoveryScan *scan);

/* Closes any still-open in-flight sockets and marks all slots free.
 * Call this if giving up on a scan before discovery_tick() returns true
 * (e.g. the user skipped it), so those fds aren't left open. */
void discovery_abort(DiscoveryScan *scan);
