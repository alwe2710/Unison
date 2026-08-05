// Real-browser tests for the "universal-client: On-Screen-Overlay echte
// UI-Tests" test category, web client: does the on-screen touch overlay
// actually show/hide, and does tapping a button actually dispatch a
// protocol-conformant Unison input frame -- real Chromium, real DOM
// events (touchstart/touchend, not synthesized clicks -- the overlay's
// own button handlers only listen for touch events, see index.html), and
// a real (locally-served, no Unison app-handshake needed -- see
// test_server.mjs's own comment) WebSocket connection actually receiving
// the bytes.
//
// isMobile (index.html) gates the whole touch-overlay/menu setup on
// ('ontouchstart' in window) || navigator.maxTouchPoints > 0 -- Playwright's
// hasTouch: true browser context option is what makes that true here, not
// a viewport-size heuristic.

import { test, expect } from '@playwright/test';
import { startStaticServer, startWsServer } from './test_server.mjs';

test.use({ hasTouch: true, viewport: { width: 400, height: 800 } });

let staticServer;
let wsServer;

test.beforeAll(async () => {
  staticServer = await startStaticServer();
});

test.afterAll(async () => {
  staticServer.server.close();
});

test.beforeEach(async () => {
  wsServer = await startWsServer();
});

test.afterEach(async () => {
  wsServer.wss.close();
});

// Fills the connect form and submits it -- beginSession() runs
// synchronously from there (see test_server.mjs's own comment on why no
// handshake response is needed for what these tests check), reaching
// WebSocket.OPEN as soon as the real WS server above accepts the
// connection.
async function connect(page) {
  await page.goto(staticServer.url);
  await page.fill('#hostInput', '127.0.0.1');
  await page.fill('#portInput', String(wsServer.port));
  await page.click('#connectSubmit');
  // #touchControls turns out to be nested *inside* #game (confirmed by
  // counting divs in index.html, not assumed from the CSS alone) -- its
  // own style.display gets set by beginSession()'s isMobile block right
  // away, well before #game itself becomes visible via showGame(), which
  // only runs once session_ready actually arrives (see index.html's own
  // msgType === 2 handler). Waiting on #game's own display is therefore
  // the real gate for "is the overlay actually visible now", regardless
  // of the persisted show/hide preference this same helper is reused for
  // below (that only affects touchControls' own display, not #game's).
  await page.waitForFunction(() => document.getElementById('game').style.display === 'flex', null, {
    timeout: 15000,
  });
}

test.beforeEach(async ({ page }) => {
  // beginSession() keeps its WebSocket in a module-scope `ws` variable,
  // not reachable from outside the closure -- exposing it here (via a
  // page-side wrapper around the real WebSocket constructor) is purely a
  // test hook for connect()'s own readiness wait above, not something
  // that changes index.html's own send/receive behavior at all.
  await page.addInitScript(() => {
    const RealWebSocket = window.WebSocket;
    window.WebSocket = class extends RealWebSocket {
      constructor(...args) {
        super(...args);
        window.__unisonTestWs = this;
      }
    };
  });
});

test('touch overlay is visible by default on a first visit', async ({ page }) => {
  await connect(page);
  await expect(page.locator('#touchControls')).toBeVisible();
});

test('menu toggle hides and re-shows the touch overlay', async ({ page }) => {
  await connect(page);
  const overlay = page.locator('#touchControls');
  await expect(overlay).toBeVisible();

  await page.click('#menuButton');
  const toggle = page.locator('#toggleOverlay');
  await expect(toggle).toBeChecked();

  await toggle.uncheck();
  await expect(overlay).toBeHidden();

  await toggle.check();
  await expect(overlay).toBeVisible();
});

test('the overlay preference persists across a reload', async ({ page }) => {
  await connect(page);
  await page.click('#menuButton');
  await page.locator('#toggleOverlay').uncheck();
  await expect(page.locator('#touchControls')).toBeHidden();

  await connect(page); // reload + reconnect (goto() again inside connect())
  await expect(page.locator('#touchControls')).toBeHidden();
});

// UNISON_MSG_INPUT -- filters out the client's own ping frames (type 4,
// sent right after showGame(), see index.html's own sendPing() call),
// which otherwise land in receivedFrames ahead of (or interleaved with)
// the actual button-press frames these assertions care about (found by
// running this for real: the very first frame received was a 9-byte ping,
// not the 3-byte press this test expected at index 0).
function inputFrames() {
  return wsServer.receivedFrames.filter((f) => f[0] === 2);
}

test('tapping the A button sends a protocol-conformant input frame', async ({ page }) => {
  await connect(page);

  await page.locator('[data-name="A"]').dispatchEvent('touchstart');
  await expect.poll(() => inputFrames().length, { timeout: 5000 }).toBeGreaterThanOrEqual(1);

  // type=2 (UNISON_MSG_INPUT), keyState=0x0001 (UNISON_KEY_A, bit 0) as
  // u16le -- see unison/protocol.h's own unison_key enum and
  // unison_build_input_frame()'s 3-byte wire layout, which
  // unison_wasm_build_input_frame() (sendKeys(), index.html) wraps.
  const pressFrame = inputFrames()[0];
  expect(pressFrame.length).toBe(3);
  expect(pressFrame.readUInt16LE(1)).toBe(1 << 0);

  await page.locator('[data-name="A"]').dispatchEvent('touchend');
  await expect.poll(() => inputFrames().length, { timeout: 5000 }).toBeGreaterThanOrEqual(2);

  // Release: keyState back to 0 -- the bit must actually clear, not just
  // stop being sent (a client that silently stopped resending held keys
  // would look identical from the frame *count* alone).
  const releaseFrame = inputFrames()[1];
  expect(releaseFrame.readUInt16LE(1)).toBe(0);
});

test('tapping two buttons sets both bits, releasing one leaves the other held', async ({ page }) => {
  await connect(page);

  await page.locator('[data-name="A"]').dispatchEvent('touchstart');
  await page.locator('[data-name="B"]').dispatchEvent('touchstart');
  // >= 2, not necessarily === 2: sendKeys() fires once per press (A, then
  // A|B), so both bits being set only guarantees the *second* call already
  // landed -- a stray ping (setInterval(sendPing, 1000), index.html) could
  // in principle land in between, which is exactly why this reads
  // inputFrames() (filtered by type) rather than trusting frame count/
  // position directly.
  await expect.poll(() => inputFrames().length, { timeout: 5000 }).toBeGreaterThanOrEqual(2);
  const bothHeld = inputFrames()[inputFrames().length - 1];
  expect(bothHeld.readUInt16LE(1)).toBe((1 << 0) | (1 << 1)); // UNISON_KEY_A | UNISON_KEY_B

  await page.locator('[data-name="A"]').dispatchEvent('touchend');
  await expect.poll(() => inputFrames().length, { timeout: 5000 }).toBeGreaterThanOrEqual(3);
  const onlyBHeld = inputFrames()[inputFrames().length - 1];
  expect(onlyBHeld.readUInt16LE(1)).toBe(1 << 1); // UNISON_KEY_B only
});
