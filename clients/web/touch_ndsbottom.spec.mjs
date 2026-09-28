// Real-browser test for touch input on an NDS_BOTTOM_SCREEN (melonDS)
// session -- written to actually exercise the client-side half of a
// reported "touch doesn't work on melonDS" issue, since static reading of
// index.html's sendMappedTouch()/resizeDesktopCanvas() and melonDS's own
// server-side EmuThread.cpp touch-scaling code (both already carry
// extensive comments about earlier real scaling/clamping bugs having been
// fixed there) didn't turn up an obvious remaining defect. This either
// catches a real client-side regression or proves the client's own half of
// the pipe is solid, narrowing any real report to melonDS's own server code
// instead.
//
// Desktop-only (no hasTouch), like keybindings_ui.spec.mjs -- the pointer
// events under test (pointerdown/pointermove/pointerup on #screen) aren't
// gated on isMobile at all (see index.html), but a fixed, non-letterboxed
// desktop canvas size (resizeDesktopCanvas() keeps canvas.style.width/
// height exactly aspect-matched to canvas.width/height) makes the expected
// touch_x/touch_y trivial to compute, unlike mobile's CSS object-fit:
// contain letterboxing.

import { test, expect } from '@playwright/test';
import { createServer } from 'node:http';
import { readFile } from 'node:fs/promises';
import { extname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { deflateRawSync } from 'node:zlib';
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

const NDS_WIDTH = 256;
const NDS_HEIGHT = 192;

// melonDS's real hello (UnisonMessages.cpp BuildHelloMessage/kInputEncoding)
// -- NDS_BOTTOM_SCREEN, single slot, "touch_and_buttons" (not
// "n3ds_touch_and_buttons": the DS has no analog stick, see docs/protocol.md).
const HELLO_JSON = JSON.stringify({
  message: 'hello',
  protocol_version: 2,
  stream_type: 'NDS_BOTTOM_SCREEN',
  video: { width: NDS_WIDTH, height: NDS_HEIGHT, fps: 60 },
  input_encoding: 'touch_and_buttons',
});

// video_mode 'tiles' matches VIDEO_MODE_DEFAULT (index.html) so the
// fallback dialog never fires and steals focus/input here -- same reason
// test_server.mjs's own SESSION_READY_JSON uses 'tiles'.
const SESSION_READY_JSON = JSON.stringify({
  message: 'session_ready',
  slot: 0,
  video: { width: NDS_WIDTH, height: NDS_HEIGHT, fps: 60 },
  video_mode: 'tiles',
});

// [u8 type=1][u32le width][u32le height][u8 format][raw-deflate-compressed
// block] (docs/protocol.md's "WebSocket Binary Frames" table) -- format=0
// (neither INDEXED nor TILES): the compressed block is just width*height
// raw RGB565 pixels, row-major, nothing else. Content (all-zero) doesn't
// matter for this test, only that it decodes so canvas.width/height
// actually become NDS_WIDTH/NDS_HEIGHT -- the precondition
// sendMappedTouch() needs before it'll compute (and send) a touch at all.
function buildRawVideoFrame(width, height) {
  const pixels = Buffer.alloc(width * height * 2); // all-zero RGB565
  const compressed = deflateRawSync(pixels);
  const header = Buffer.alloc(1 + 4 + 4 + 1);
  header.writeUInt8(1, 0); // UNISON_MSG_VIDEO
  header.writeUInt32LE(width, 1);
  header.writeUInt32LE(height, 5);
  header.writeUInt8(0, 9); // format = 0 (raw, whole image)
  return Buffer.concat([header, compressed]);
}

async function startWsServer() {
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
        socket.send(buildRawVideoFrame(NDS_WIDTH, NDS_HEIGHT), { binary: true });
      }
    });
  });
  await new Promise((resolve) => wss.once('listening', resolve));
  const { port } = wss.address();
  return { wss, port, receivedFrames };
}

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

async function connect(page) {
  await page.goto(staticServer.url);
  await page.fill('#hostInput', '127.0.0.1');
  await page.fill('#portInput', String(wsServer.port));
  await page.click('#connectSubmit');
  await page.waitForFunction(() => document.getElementById('game').style.display === 'flex', null, {
    timeout: 15000,
  });
  // canvas.width/height only become NDS_WIDTH/NDS_HEIGHT once the raw video
  // frame sent right after session_ready has actually been decoded --
  // sendMappedTouch() itself no-ops (early return) until then, so this is
  // the real precondition for every assertion below, not just cosmetic.
  await page.waitForFunction(
    ({ w, h }) => document.getElementById('screen').width === w && document.getElementById('screen').height === h,
    { w: NDS_WIDTH, h: NDS_HEIGHT },
    { timeout: 15000 }
  );
}

function touchFrames() {
  // touch_and_buttons wire size: 1(type)+1(pressed)+2(touch_x)+2(touch_y)+4(buttons) = 10 bytes
  // (unison_build_touch_and_buttons_frame, core/include/unison/protocol.h).
  return wsServer.receivedFrames.filter((f) => f[0] === 2 && f.length === 10);
}

function parseTouchFrame(f) {
  return { pressed: f[1], touch_x: f.readUInt16LE(2), touch_y: f.readUInt16LE(4) };
}

test('tapping the canvas sends touch coordinates correctly scaled to the native 256x192 NDS resolution', async ({ page }) => {
  await connect(page);

  const box = await page.locator('#screen').boundingBox();
  expect(box).not.toBeNull();

  // Desktop canvas is CSS-sized to exactly match its backing-buffer aspect
  // ratio (resizeDesktopCanvas(), index.html) -- no letterbox offset, so a
  // tap at a given fraction of the box maps to that same fraction of
  // NDS_WIDTH/NDS_HEIGHT, independent of the box's actual on-screen size.
  const targetXFrac = 0.25;
  const targetYFrac = 0.75;
  const clientX = box.x + box.width * targetXFrac;
  const clientY = box.y + box.height * targetYFrac;

  await page.mouse.move(clientX, clientY);
  await page.mouse.down();
  await expect.poll(() => touchFrames().length, { timeout: 5000 }).toBeGreaterThanOrEqual(1);

  const press = parseTouchFrame(touchFrames()[0]);
  expect(press.pressed).toBe(1);
  // +/-1px rounding tolerance: box.width/height come back as sub-pixel
  // floats from getBoundingClientRect(), sendMappedTouch() rounds.
  expect(press.touch_x).toBeGreaterThanOrEqual(Math.round(NDS_WIDTH * targetXFrac) - 1);
  expect(press.touch_x).toBeLessThanOrEqual(Math.round(NDS_WIDTH * targetXFrac) + 1);
  expect(press.touch_y).toBeGreaterThanOrEqual(Math.round(NDS_HEIGHT * targetYFrac) - 1);
  expect(press.touch_y).toBeLessThanOrEqual(Math.round(NDS_HEIGHT * targetYFrac) + 1);

  await page.mouse.up();
  await expect.poll(() => touchFrames().length, { timeout: 5000 }).toBeGreaterThanOrEqual(2);
  const release = parseTouchFrame(touchFrames()[touchFrames().length - 1]);
  // "a release carries no meaningful position" (docs/protocol.md) --
  // releaseTouch() (index.html) always sends (0, 0), not the last-known spot.
  expect(release.pressed).toBe(0);
  expect(release.touch_x).toBe(0);
  expect(release.touch_y).toBe(0);
});

test('a drag past the canvas edge clamps to the last valid pixel, not the raw (possibly negative/overflowing) coordinate', async ({ page }) => {
  await connect(page);

  const box = await page.locator('#screen').boundingBox();
  await page.mouse.move(box.x + box.width / 2, box.y + box.height / 2);
  await page.mouse.down();
  await expect.poll(() => touchFrames().length, { timeout: 5000 }).toBeGreaterThanOrEqual(1);

  // Drag to a point well outside the canvas box on every side.
  await page.mouse.move(box.x + box.width + 500, box.y + box.height + 500);
  await expect.poll(() => touchFrames().length, { timeout: 5000 }).toBeGreaterThanOrEqual(2);
  const overshoot = parseTouchFrame(touchFrames()[touchFrames().length - 1]);
  expect(overshoot.touch_x).toBe(NDS_WIDTH - 1);
  expect(overshoot.touch_y).toBe(NDS_HEIGHT - 1);

  await page.mouse.move(box.x - 500, box.y - 500);
  await expect.poll(() => touchFrames().length, { timeout: 5000 }).toBeGreaterThanOrEqual(3);
  const undershoot = parseTouchFrame(touchFrames()[touchFrames().length - 1]);
  expect(undershoot.touch_x).toBe(0);
  expect(undershoot.touch_y).toBe(0);

  await page.mouse.up();
});
