// Real-browser tests for the on-screen analog sticks (#touchStickL/R,
// index.html) -- previously always sent (0, 0) regardless of drag input
// ("no physical-stick input built out for this client yet"), a real gap
// reported after the X/Y/ZL/ZR fix landed. Verifies: the left stick shows
// for any n3ds_touch_and_buttons session and drags produce correctly
// scaled/clamped left_x/left_y on the wire with the same Y-sign convention
// Android's own VirtualStick uses; the right stick only shows for
// WIIU_GAMEPAD (Cemu, the only console with two sticks); and touch_and_
// buttons (melonDS, no stick fields on the wire at all) shows neither.

import { test, expect } from '@playwright/test';
import { createServer } from 'node:http';
import { readFile } from 'node:fs/promises';
import { extname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { WebSocketServer } from 'ws';

const WEB_DIR = fileURLToPath(new URL('.', import.meta.url));
const MIME = { '.html': 'text/html', '.js': 'text/javascript', '.mjs': 'text/javascript' };

async function startStaticServer() {
  const server = createServer(async (req, res) => {
    const urlPath = req.url === '/' ? '/index.html' : req.url;
    try {
      const filePath = join(WEB_DIR, urlPath);
      const body = await readFile(filePath);
      res.writeHead(200, { 'Content-Type': MIME[extname(filePath)] || 'application/octet-stream' });
      res.end(body);
    } catch {
      res.writeHead(404);
      res.end('not found');
    }
  });
  await new Promise((resolve) => server.listen(0, '127.0.0.1', resolve));
  const { port } = server.address();
  return { server, port, url: `http://127.0.0.1:${port}/` };
}

function makeMocks(streamType, inputEncoding) {
  const HELLO_JSON = JSON.stringify({
    message: 'hello',
    protocol_version: 2,
    stream_type: streamType,
    video: { width: 256, height: 192, fps: 60 },
    input_encoding: inputEncoding,
  });
  // Matches whatever videoModeFor() (index.html) actually requests for
  // this streamType, so grantedVideoMode === requestedVideoMode and
  // showVideoModeFallback() never fires and steals focus from the sticks
  // under test here -- WIIU_GAMEPAD no longer has a raw fallback at all
  // (videoModeFor()'s own normalizing, matching Cemu's real WiiuGamepad-
  // Stream.cpp), so VIDEO_MODE_DEFAULT ('tiles') would otherwise mismatch.
  const SESSION_READY_JSON = JSON.stringify({
    message: 'session_ready',
    slot: 0,
    video: { width: 256, height: 192, fps: 60 },
    video_mode: streamType === 'WIIU_GAMEPAD' ? 'h264' : 'tiles',
  });
  return { HELLO_JSON, SESSION_READY_JSON };
}

async function startWsServer(streamType, inputEncoding) {
  const { HELLO_JSON, SESSION_READY_JSON } = makeMocks(streamType, inputEncoding);
  const receivedFrames = [];
  const wss = new WebSocketServer({ host: '127.0.0.1', port: 0 });
  wss.on('connection', (socket) => {
    socket.send(HELLO_JSON);
    let sentReady = false;
    socket.on('message', (data, isBinary) => {
      if (isBinary) {
        receivedFrames.push(Buffer.from(data));
        return;
      }
      if (!sentReady) {
        sentReady = true;
        socket.send(SESSION_READY_JSON);
      }
    });
  });
  await new Promise((resolve) => wss.once('listening', resolve));
  const { port } = wss.address();
  return { wss, port, receivedFrames };
}

let staticServer;
let wsServer;

test.use({ hasTouch: true, viewport: { width: 400, height: 800 } });

test.beforeAll(async () => {
  staticServer = await startStaticServer();
});
test.afterAll(async () => {
  staticServer.server.close();
});
test.afterEach(async () => {
  await wsServer.wss.close();
});

async function connect(page, streamType, inputEncoding) {
  wsServer = await startWsServer(streamType, inputEncoding);
  await page.goto(staticServer.url);
  await page.fill('#hostInput', '127.0.0.1');
  await page.fill('#portInput', String(wsServer.port));
  await page.click('#connectSubmit');
  await page.waitForFunction(() => document.getElementById('game').style.display === 'flex', null, {
    timeout: 15000,
  });
}

// UNISON_MSG_INPUT, 18-byte extended_input frames only.
function stickFrames() {
  return wsServer.receivedFrames.filter((f) => f[0] === 2 && f.length === 18);
}
function parseStickFrame(f) {
  return { leftX: f.readInt16LE(10), leftY: f.readInt16LE(12), rightX: f.readInt16LE(14), rightY: f.readInt16LE(16) };
}

test('N3DS_BOTTOM_SCREEN (azahar) shows only the left stick, dragging up-right reports positive x and positive y', async ({ page }) => {
  await connect(page, 'N3DS_BOTTOM_SCREEN', 'n3ds_touch_and_buttons');

  await expect(page.locator('#touchStickL')).toBeVisible();
  await expect(page.locator('#touchStickR')).toBeHidden();

  const box = await page.locator('#touchStickL').boundingBox();
  const cx = box.x + box.width / 2;
  const cy = box.y + box.height / 2;

  // Up-right, inside the radius -- screen-space dy is negative (up), which
  // must come out as a *positive* leftY on the wire (Android's VirtualStick
  // convention: "y = -clamped.y / radius * 32767").
  await page.mouse.move(cx, cy);
  await page.mouse.down();
  await page.mouse.move(cx + 20, cy - 20);
  await expect.poll(() => stickFrames().length, { timeout: 5000 }).toBeGreaterThanOrEqual(1);
  const dragged = parseStickFrame(stickFrames()[stickFrames().length - 1]);
  expect(dragged.leftX).toBeGreaterThan(0);
  expect(dragged.leftY).toBeGreaterThan(0);

  await page.mouse.up();
  await expect.poll(() => stickFrames().length, { timeout: 5000 }).toBeGreaterThanOrEqual(2);
  const released = parseStickFrame(stickFrames()[stickFrames().length - 1]);
  expect(released.leftX).toBe(0);
  expect(released.leftY).toBe(0);
});

test('dragging past the stick radius clamps to the max magnitude, not an unbounded value', async ({ page }) => {
  await connect(page, 'N3DS_BOTTOM_SCREEN', 'n3ds_touch_and_buttons');

  const box = await page.locator('#touchStickL').boundingBox();
  const cx = box.x + box.width / 2;
  const cy = box.y + box.height / 2;

  await page.mouse.move(cx, cy);
  await page.mouse.down();
  // Straight right, but 500px out -- far past the ~48px radius.
  await page.mouse.move(cx + 500, cy);
  await expect.poll(() => stickFrames().length, { timeout: 5000 }).toBeGreaterThanOrEqual(1);
  const dragged = parseStickFrame(stickFrames()[stickFrames().length - 1]);
  expect(dragged.leftX).toBe(32767);
  expect(dragged.leftY).toBe(0);
  await page.mouse.up();
});

test('WIIU_GAMEPAD (Cemu) shows both sticks, and each drives its own axis pair independently', async ({ page }) => {
  await connect(page, 'WIIU_GAMEPAD', 'n3ds_touch_and_buttons');

  await expect(page.locator('#touchStickL')).toBeVisible();
  await expect(page.locator('#touchStickR')).toBeVisible();

  const boxL = await page.locator('#touchStickL').boundingBox();
  const boxR = await page.locator('#touchStickR').boundingBox();

  await page.mouse.move(boxL.x + boxL.width / 2, boxL.y + boxL.height / 2);
  await page.mouse.down();
  await page.mouse.move(boxL.x + boxL.width / 2 + 20, boxL.y + boxL.height / 2);
  await expect.poll(() => stickFrames().length, { timeout: 5000 }).toBeGreaterThanOrEqual(1);
  let f = parseStickFrame(stickFrames()[stickFrames().length - 1]);
  expect(f.leftX).toBeGreaterThan(0);
  expect(f.rightX).toBe(0);
  await page.mouse.up();

  const beforeRight = stickFrames().length;
  await page.mouse.move(boxR.x + boxR.width / 2, boxR.y + boxR.height / 2);
  await page.mouse.down();
  await page.mouse.move(boxR.x + boxR.width / 2 - 20, boxR.y + boxR.height / 2);
  await expect.poll(() => stickFrames().length, { timeout: 5000 }).toBeGreaterThan(beforeRight);
  f = parseStickFrame(stickFrames()[stickFrames().length - 1]);
  expect(f.rightX).toBeLessThan(0);
  // Left stayed released (0,0) from the first drag -- this drag only ever
  // touched the right stick's own live values.
  expect(f.leftX).toBe(0);
  await page.mouse.up();
});

test('touch_and_buttons (melonDS) shows neither stick', async ({ page }) => {
  await connect(page, 'NDS_BOTTOM_SCREEN', 'touch_and_buttons');

  await expect(page.locator('#touchStickL')).toBeHidden();
  await expect(page.locator('#touchStickR')).toBeHidden();
});

// N3DS_BOTTOM_SCREEN's own second stick is opt-in (n3dsSecondStickEnabled(),
// #consoleDetailSecondStickToggle) -- unlike WIIU_GAMEPAD, the real 3DS only
// has one circle pad, so the default (covered by the first test above) is
// off. This covers the opt-in path itself, through the real settings UI
// (menu -> console-specific settings -> 3DS), not just the localStorage key
// directly -- and that toggling it live-updates the already-connected
// session, not just the next connect.
test('enabling the 3DS-specific "second analog stick" setting live-reveals and wires up the right stick', async ({ page }) => {
  await connect(page, 'N3DS_BOTTOM_SCREEN', 'n3ds_touch_and_buttons');
  await expect(page.locator('#touchStickR')).toBeHidden();

  await page.click('#menuButton');
  await page.click('#consoleSettingsRowMobile');
  // N3DS_BOTTOM_SCREEN is KNOWN_STREAM_TYPES' first entry (index.html).
  await page.locator('#consoleRows button').first().click();
  await expect(page.locator('#consoleDetailSecondStickRow')).toBeVisible();
  await page.locator('#consoleDetailSecondStickToggle').check();
  await page.click('#closeConsoleDetail');
  await page.click('#closeConsoleSettings');
  await page.click('#closeMenu');

  await expect(page.locator('#touchStickR')).toBeVisible();

  const box = await page.locator('#touchStickR').boundingBox();
  await page.mouse.move(box.x + box.width / 2, box.y + box.height / 2);
  await page.mouse.down();
  await page.mouse.move(box.x + box.width / 2 + 20, box.y + box.height / 2);
  await expect.poll(() => stickFrames().length, { timeout: 5000 }).toBeGreaterThanOrEqual(1);
  const f = parseStickFrame(stickFrames()[stickFrames().length - 1]);
  expect(f.rightX).toBeGreaterThan(0);
  await page.mouse.up();
});
