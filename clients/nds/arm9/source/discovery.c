#include "discovery.h"

#include <dswifi9.h>
#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>

#define DISCOVERY_LOBBY_PORT 6800
#define DISCOVERY_CONNECT_TIMEOUT_TICKS 20  /* ~333ms at 60Hz */
#define DISCOVERY_RESPONSE_TIMEOUT_TICKS 20 /* ~333ms at 60Hz */

/* NOTE: an earlier version of this gated the send() probe below on
 * getsockopt(SOL_SOCKET, SO_ERROR, ...) to detect when a non-blocking
 * connect() had finished, since dswifi has no select()/poll(). That
 * compiled and linked fine, but real-hardware testing showed discovery
 * never found anything -- getsockopt() apparently doesn't report
 * SO_ERROR usefully on this socket layer, so every slot just sat in
 * DISCOVERY_SLOT_CONNECTING until its timeout. Switched to attempting
 * send() unconditionally every tick instead: it returns EAGAIN/
 * EWOULDBLOCK on its own if the connection (or the send buffer) isn't
 * ready yet, and a real error otherwise -- the same EAGAIN pattern
 * main.c's runSession() already relies on for recv(), which *is* proven
 * working on real hardware. */

static void finishSlot(DiscoveryScan *scan, int i, bool success, uint32_t hostIp) {
    DiscoverySlot *slot = &scan->slots[i];
    closesocket(slot->fd);
    slot->fd = -1;
    slot->state = DISCOVERY_SLOT_FREE;
    scan->doneCount++;
    if (success && !scan->found) {
        scan->found = true;
        scan->foundIp = hostIp;
    }
}

static bool launchSlot(DiscoverySlot *slot, uint32_t hostIp) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        return false;
    }
    int nonblocking = 1;
    ioctl(fd, FIONBIO, &nonblocking);

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(DISCOVERY_LOBBY_PORT);
    addr.sin_addr.s_addr = hostIp;

    int rc = connect(fd, (struct sockaddr *)&addr, sizeof(addr));
    if (rc != 0 && errno != EINPROGRESS && errno != EAGAIN) {
        closesocket(fd);
        return false;
    }

    slot->fd = fd;
    slot->hostIp = hostIp;
    slot->ticks = 0;
    slot->respLen = 0;
    slot->state = DISCOVERY_SLOT_CONNECTING;
    return true;
}

static void advanceSlot(DiscoveryScan *scan, int i) {
    DiscoverySlot *slot = &scan->slots[i];
    slot->ticks++;

    if (slot->state == DISCOVERY_SLOT_CONNECTING) {
        static const char kReq[] = "GET / HTTP/1.0\r\n\r\n";
        ssize_t n = send(slot->fd, kReq, sizeof(kReq) - 1, 0);
        if (n > 0) {
            slot->state = DISCOVERY_SLOT_WAITING_RESPONSE;
            slot->ticks = 0;
            return;
        }
        if (!(n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))) {
            finishSlot(scan, i, false, 0);
            return;
        }
        /* connect() (or the send buffer) not ready yet -- retry next
         * tick, bounded by the timeout below. */
        if (slot->ticks >= DISCOVERY_CONNECT_TIMEOUT_TICKS) {
            finishSlot(scan, i, false, 0);
        }
        return;
    }

    /* DISCOVERY_SLOT_WAITING_RESPONSE */
    char chunk[32];
    ssize_t n = recv(slot->fd, chunk, sizeof(chunk), 0);
    if (n > 0) {
        size_t room = sizeof(slot->respBuf) - 1 - slot->respLen;
        size_t copy = (size_t)n < room ? (size_t)n : room;
        memcpy(slot->respBuf + slot->respLen, chunk, copy);
        slot->respLen += copy;
        slot->respBuf[slot->respLen] = '\0';
        if (slot->respLen >= 5 && memcmp(slot->respBuf, "HTTP/", 5) == 0 &&
            strstr(slot->respBuf, "200") != NULL) {
            finishSlot(scan, i, true, slot->hostIp);
            return;
        }
    } else if (n == 0) {
        finishSlot(scan, i, false, 0);
        return;
    } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
        finishSlot(scan, i, false, 0);
        return;
    }

    if (slot->ticks >= DISCOVERY_RESPONSE_TIMEOUT_TICKS) {
        finishSlot(scan, i, false, 0);
    }
}

void discovery_start(DiscoveryScan *scan, uint32_t ownIp) {
    memset(scan, 0, sizeof(*scan));
    for (int i = 0; i < DISCOVERY_CONCURRENCY; i++) {
        scan->slots[i].fd = -1;
        scan->slots[i].state = DISCOVERY_SLOT_FREE;
    }

    uint32_t hostIp = ntohl(ownIp);
    uint32_t network = hostIp & 0xFFFFFF00u; /* assume /24, see 3ds discovery.cpp */
    int n = 0;
    for (uint32_t offset = 1; offset <= 254 && n < DISCOVERY_MAX_HOSTS; offset++) {
        uint32_t candidate = network + offset;
        if (candidate == hostIp) {
            continue; /* skip self */
        }
        scan->hosts[n++] = htonl(candidate);
    }
    scan->hostCount = n;
}

bool discovery_tick(DiscoveryScan *scan) {
    if (scan->found) {
        return true;
    }

    for (int i = 0; i < DISCOVERY_CONCURRENCY; i++) {
        if (scan->slots[i].state == DISCOVERY_SLOT_FREE && scan->nextIndex < scan->hostCount) {
            uint32_t host = scan->hosts[scan->nextIndex];
            scan->nextIndex++;
            if (!launchSlot(&scan->slots[i], host)) {
                scan->doneCount++;
            }
        }
    }

    for (int i = 0; i < DISCOVERY_CONCURRENCY; i++) {
        if (scan->slots[i].state != DISCOVERY_SLOT_FREE) {
            advanceSlot(scan, i);
        }
    }

    if (scan->found) {
        /* A match was found this tick (or a previous one) -- close any
         * other still-open in-flight sockets now rather than leaving them
         * held past the scan (e.g. into runSession()'s own connect()). */
        for (int i = 0; i < DISCOVERY_CONCURRENCY; i++) {
            if (scan->slots[i].state != DISCOVERY_SLOT_FREE) {
                closesocket(scan->slots[i].fd);
                scan->slots[i].fd = -1;
                scan->slots[i].state = DISCOVERY_SLOT_FREE;
            }
        }
        return true;
    }

    return scan->doneCount >= scan->hostCount;
}

void discovery_abort(DiscoveryScan *scan) {
    for (int i = 0; i < DISCOVERY_CONCURRENCY; i++) {
        if (scan->slots[i].state != DISCOVERY_SLOT_FREE) {
            closesocket(scan->slots[i].fd);
            scan->slots[i].fd = -1;
            scan->slots[i].state = DISCOVERY_SLOT_FREE;
        }
    }
}
