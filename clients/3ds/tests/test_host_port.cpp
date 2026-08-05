// Host-buildable (plain g++/clang++, no devkitARM) unit test for
// splitHostPort() (host_port.hpp/.cpp) -- the manual host-entry field's
// "bare host" vs. "host:port" decision. See host_port.hpp's own comment:
// this is what drawMenuScreen()'s Connect button now checks before falling
// back to the GC_GBA_LINK P1-P4 lobby probe.

#include "host_port.hpp"

#include <cstdio>
#include <cstdlib>

#define CHECK(cond)                                                                    \
    do {                                                                                \
        if (!(cond)) {                                                                  \
            std::fprintf(stderr, "FAILED %s:%d: %s\n", __FILE__, __LINE__, #cond);      \
            std::exit(1);                                                               \
        }                                                                                \
    } while (0)

static void TestBareHostHasNoPort() {
    auto hp = splitHostPort("192.168.1.5");
    CHECK(!hp.has_value());
}

static void TestHostColonPortSplits() {
    auto hp = splitHostPort("192.168.1.5:6801");
    CHECK(hp.has_value());
    CHECK(hp->host == "192.168.1.5");
    CHECK(hp->port == 6801);
}

static void TestTrailingColonWithNoDigitsIsNotAPort() {
    // A bare trailing colon (nothing after it) must not be treated as a
    // valid split -- falls back to the whole string as a bare host, same
    // as before this feature existed, rather than a bogus port of 0.
    auto hp = splitHostPort("192.168.1.5:");
    CHECK(!hp.has_value());
}

static void TestLeadingColonIsNotAPort() {
    // A colon at position 0 has no host in front of it.
    auto hp = splitHostPort(":6801");
    CHECK(!hp.has_value());
}

static void TestNonNumericSuffixIsNotAPort() {
    // Guards against misreading something that merely contains a colon but
    // isn't "host:port" at all (e.g. an IPv6 literal, out of scope here --
    // see host_port.hpp).
    auto hp = splitHostPort("some:thing");
    CHECK(!hp.has_value());
}

static void TestPortOutOfRangeIsRejected() {
    CHECK(!splitHostPort("192.168.1.5:0").has_value());
    CHECK(!splitHostPort("192.168.1.5:70000").has_value());
    CHECK(!splitHostPort("192.168.1.5:-1").has_value());
}

static void TestTrailingGarbageAfterDigitsIsRejected() {
    // strtol() would happily parse the leading digits of "6801x" and stop
    // there -- *end must be '\0', not just "some digits found somewhere".
    CHECK(!splitHostPort("192.168.1.5:6801x").has_value());
}

int main() {
    TestBareHostHasNoPort();
    TestHostColonPortSplits();
    TestTrailingColonWithNoDigitsIsNotAPort();
    TestLeadingColonIsNotAPort();
    TestNonNumericSuffixIsNotAPort();
    TestPortOutOfRangeIsRejected();
    TestTrailingGarbageAfterDigitsIsRejected();
    std::printf("host_port (3ds): all tests passed\n");
    return 0;
}
