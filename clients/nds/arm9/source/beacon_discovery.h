/* UDP discovery beacon listener for the finlink lobby (finlink/discovery.h,
 * docs/protocol.md "Discovery-Beacon (UDP)"), replacing the old subnet-sweep
 * discovery.c (renamed subnet_discovery.c -- see its own top comment) now
 * that a server announces itself periodically instead of needing to be
 * found by probing every host on the /24. Reworked into a non-blocking
 * step function the same way subnet_discovery.h was: the NDS build has
 * neither threads nor select()/poll(), so the caller drives this one step
 * per swiWaitForVBlank() tick. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "finlink/discovery.h"

#define BEACON_MAX_SERVERS 4

typedef struct {
    finlink_beacon beacon;
    bool compatible; /* beacon.protocol_version == FINLINK_PROTOCOL_VERSION */
    int lastSeenTick; /* BeaconScan.tickCounter value as of the last refresh */
    bool inUse;
} BeaconServer;

typedef struct {
    int fd; /* -1 if the socket couldn't be opened/bound */
    BeaconServer servers[BEACON_MAX_SERVERS];
    int tickCounter; /* one tick == one swiWaitForVBlank(), ~16.7ms */
} BeaconScan;

/* Opens and binds the UDP socket (FINLINK_BEACON_PORT), resets scan for a
 * fresh run. Returns false if the socket couldn't be created/bound --
 * scan->fd stays -1 and beaconScan_tick() becomes a no-op in that case. */
bool beaconScan_start(BeaconScan *scan);

/* Advances the scan by one tick (call once per swiWaitForVBlank()):
 * drains any beacon datagrams currently waiting (non-blocking) and prunes
 * entries not refreshed within FINLINK_BEACON_STALE_MS. Never "finishes"
 * the way subnet_discovery's scan does -- a beacon listener just keeps
 * running for as long as the caller wants a live server list. */
void beaconScan_tick(BeaconScan *scan);

/* Closes the socket. Call when leaving the screen that shows this scan's
 * results, matching subnet_discovery's discovery_abort(). */
void beaconScan_stop(BeaconScan *scan);
