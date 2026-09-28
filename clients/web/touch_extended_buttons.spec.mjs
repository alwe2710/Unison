// Real-browser tests for the on-screen X/Y/ZL/ZR overlay buttons
// (#touchControls .ext-btn, index.html) -- these never existed in this
// client's touch overlay at all (it was carried over unmodified from the
// original Dolphin-only GC_GBA_LINK web UI, see index.html's own comment
// on BUTTONS/UNISON_BUTTON_BIT), a real gap for any touch_and_buttons/
// n3ds_touch_and_buttons session (melonDS, azahar, Cemu) on a phone: no way
// to press X, Y, ZL, or ZR at all without a Bluetooth keyboard. Verifies
// both halves: the extended group shows up (and works) for a session that
// needs it, and stays hidden for one that doesn't (GC_GBA_LINK/gba_buttons,
// which has no room for these four on the wire at all -- see
// UNISON_BUTTON_BIT's own comment).
//
// hasTouch: true (unlike keybindings_ui.spec.mjs/touch_ndsbottom.spec.mjs):
// #touchControls' whole press-wiring, .ext class included, only happens
// inside index.html's `if (isMobile) {...}` block, and isMobile is gated on
// hasTouch/maxTouchPoints (see overlay.spec.mjs's own comment on that).

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

// video_mode 'tiles' matches VIDEO_MODE_DEFAULT (index.html), same reason
// every other mock server here uses it -- keeps the fallback dialog from
// firing and stealing focus.
function makeMocks(streamType, inputEncoding) {
  const HELLO_JSON = JSON.stringify({
    message: 'hello',
    protocol_version: 2,
    stream_type: streamType,
    video: { width: 256, height: 192, fps: 60 },
    input_encoding: inputEncoding,
  });
  const SESSION_READY_JSON = JSON.stringify({
    message: 'session_ready',
    slot: 0,
    video: { width: 256, height: 192, fps: 60 },
    video_mode: 'tiles',
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

// UNISON_MSG_INPUT, 10-byte touch_and_buttons frames only (filters out the
// client's own ping frames, same reasoning as every other spec here).
function touchFrames() {
  return wsServer.receivedFrames.filter((f) => f[0] === 2 && f.length === 10);
}

test('a touch_and_buttons session (melonDS) shows X/Y/ZL/ZR and they send the right wire bits', async ({ page }) => {
  await connect(page, 'NDS_BOTTOM_SCREEN', 'touch_and_buttons');

  await expect(page.locator('#touchControls')).toHaveClass(/\bext\b/);
  for (const id of ['#touchZL', '#touchZR']) {
    await expect(page.locator(id)).toBeVisible();
  }
  await expect(page.locator('#touchXY [data-name="X"]')).toBeVisible();
  await expect(page.locator('#touchXY [data-name="Y"]')).toBeVisible();

  // UNISON_BUTTON_X = 1<<2 (core/include/unison/protocol.h, matches
  // index.html's own UNISON_BUTTON_BIT.X).
  await page.locator('[data-name="X"]').dispatchEvent('touchstart');
  await expect.poll(() => touchFrames().length, { timeout: 5000 }).toBeGreaterThanOrEqual(1);
  expect(touchFrames()[0].readUInt32LE(6)).toBe(1 << 2);
  await page.locator('[data-name="X"]').dispatchEvent('touchend');

  // UNISON_BUTTON_ZR = 1<<7.
  await page.locator('[data-name="ZR"]').dispatchEvent('touchstart');
  await expect.poll(() => touchFrames().length, { timeout: 5000 }).toBeGreaterThanOrEqual(3);
  expect(touchFrames()[touchFrames().length - 1].readUInt32LE(6)).toBe(1 << 7);
});

test('a plain gba_buttons session (Dolphin) keeps X/Y/ZL/ZR hidden', async ({ page }) => {
  await connect(page, 'GC_GBA_LINK', 'gba_buttons');

  await expect(page.locator('#touchControls')).not.toHaveClass(/\bext\b/);
  for (const id of ['#touchZL', '#touchZR']) {
    await expect(page.locator(id)).toBeHidden();
  }
  await expect(page.locator('#touchXY [data-name="X"]')).toBeHidden();
  await expect(page.locator('#touchXY [data-name="Y"]')).toBeHidden();
});
