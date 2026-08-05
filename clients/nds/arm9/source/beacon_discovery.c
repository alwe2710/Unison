#include "beacon_discovery.h"

#include <dswifi9.h>
#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>

#include "unison/handshake.h" /* UNISON_PROTOCOL_VERSION */

/* UNISON_BEACON_STALE_MS (6000) converted to ticks at the 60Hz
 * swiWaitForVBlank() rate this scan is driven at. */
#define BEACON_STALE_TICKS (UNISON_BEACON_STALE_MS * 60 / 1000)

bool beaconScan_start(BeaconScan *scan) {
    memset(scan, 0, sizeof(*scan));
    scan->fd = -1;

    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        return false;
    }
    int nonblocking = 1;
    ioctl(fd, FIONBIO, &nonblocking);

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(UNISON_BEACON_PORT);
    addr.sin_addr.s_addr = INADDR_ANY;
    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        closesocket(fd);
        return false;
    }

    scan->fd = fd;
    return true;
}

static BeaconServer *findByHost(BeaconScan *scan, const char *host) {
    for (int i = 0; i < BEACON_MAX_SERVERS; i++) {
        if (scan->servers[i].inUse && strcmp(scan->servers[i].beacon.host, host) == 0) {
            return &scan->servers[i];
        }
    }
    return NULL;
}

static BeaconServer *findFreeOrOldest(BeaconScan *scan) {
    BeaconServer *oldest = &scan->servers[0];
    for (int i = 0; i < BEACON_MAX_SERVERS; i++) {
        if (!scan->servers[i].inUse) {
            return &scan->servers[i];
        }
        if (scan->servers[i].lastSeenTick < oldest->lastSeenTick) {
            oldest = &scan->servers[i];
        }
    }
    /* All slots full and none free: overwrite whichever was heard from
     * longest ago -- still-live entries all get refreshed every ~2s
     * (docs/protocol.md), so this only ever evicts something already
     * on its way to going stale anyway. */
    return oldest;
}

void beaconScan_tick(BeaconScan *scan) {
    scan->tickCounter++;
    if (scan->fd >= 0) {
        uint8_t buf[512];
        for (;;) {
            ssize_t n = recv(scan->fd, buf, sizeof(buf), 0);
            if (n <= 0) {
                break; /* EAGAIN/EWOULDBLOCK (nothing waiting) or a real error -- either way, stop draining */
            }
            unison_beacon beacon;
            if (!unison_parse_beacon(buf, (size_t)n, &beacon)) {
                continue; /* not a well-formed Unison beacon -- ignore, per unison/discovery.h */
            }
            BeaconServer *slot = findByHost(scan, beacon.host);
            if (!slot) {
                slot = findFreeOrOldest(scan);
            }
            slot->beacon = beacon;
            slot->compatible = (beacon.protocol_version == UNISON_PROTOCOL_VERSION);
            slot->lastSeenTick = scan->tickCounter;
            slot->inUse = true;
        }
    }

    for (int i = 0; i < BEACON_MAX_SERVERS; i++) {
        if (scan->servers[i].inUse && (scan->tickCounter - scan->servers[i].lastSeenTick) > BEACON_STALE_TICKS) {
            scan->servers[i].inUse = false;
        }
    }
}

void beaconScan_stop(BeaconScan *scan) {
    if (scan->fd >= 0) {
        closesocket(scan->fd);
        scan->fd = -1;
    }
}
