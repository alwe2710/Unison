#include "host_port.h"

#include <stdlib.h>
#include <string.h>

int unisonNdsSplitHostPort(char *host) {
    char *colon = strrchr(host, ':');
    if (colon == NULL || colon == host || colon[1] == '\0') {
        return 0;
    }
    char *end = NULL;
    long port = strtol(colon + 1, &end, 10);
    if (*end != '\0' || port <= 0 || port > 65535) {
        return 0;
    }
    *colon = '\0'; /* truncate host to just the host part */
    return (int)port;
}
