/* Host-buildable (plain gcc/clang, no devkitARM) unit test for
 * unisonNdsSplitHostPort() (host_port.h/.c) -- the manual host-entry
 * field's "bare host" vs. "host:port" decision. See host_port.h's own
 * comment: this is what promptForIp() now checks before falling back to
 * the GC_GBA_LINK P1-P4 lobby picker. */

#include "host_port.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            fprintf(stderr, "FAILED %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            exit(1);                                                         \
        }                                                                    \
    } while (0)

static void TestBareHostHasNoPort(void) {
    char host[] = "192.168.1.5";
    int port = unisonNdsSplitHostPort(host);
    CHECK(port == 0);
    CHECK(strcmp(host, "192.168.1.5") == 0); /* untouched */
}

static void TestHostColonPortSplits(void) {
    char host[] = "192.168.1.5:6810";
    int port = unisonNdsSplitHostPort(host);
    CHECK(port == 6810);
    CHECK(strcmp(host, "192.168.1.5") == 0);
}

static void TestTrailingColonWithNoDigitsIsNotAPort(void) {
    char host[] = "192.168.1.5:";
    int port = unisonNdsSplitHostPort(host);
    CHECK(port == 0);
    CHECK(strcmp(host, "192.168.1.5:") == 0); /* untouched */
}

static void TestLeadingColonIsNotAPort(void) {
    char host[] = ":6810";
    int port = unisonNdsSplitHostPort(host);
    CHECK(port == 0);
}

static void TestNonNumericSuffixIsNotAPort(void) {
    char host[] = "some:thing";
    int port = unisonNdsSplitHostPort(host);
    CHECK(port == 0);
}

static void TestPortOutOfRangeIsRejected(void) {
    char host0[] = "192.168.1.5:0";
    CHECK(unisonNdsSplitHostPort(host0) == 0);
    char host1[] = "192.168.1.5:70000";
    CHECK(unisonNdsSplitHostPort(host1) == 0);
    char host2[] = "192.168.1.5:-1";
    CHECK(unisonNdsSplitHostPort(host2) == 0);
}

static void TestTrailingGarbageAfterDigitsIsRejected(void) {
    char host[] = "192.168.1.5:6810x";
    CHECK(unisonNdsSplitHostPort(host) == 0);
}

int main(void) {
    TestBareHostHasNoPort();
    TestHostColonPortSplits();
    TestTrailingColonWithNoDigitsIsNotAPort();
    TestLeadingColonIsNotAPort();
    TestNonNumericSuffixIsNotAPort();
    TestPortOutOfRangeIsRejected();
    TestTrailingGarbageAfterDigitsIsRejected();
    printf("host_port (nds): all tests passed\n");
    return 0;
}
