// Desktop-only (no hasTouch) counterpart to touch_sticks.spec.mjs's own
// "enabling the 3DS-specific setting" test: the second-analog-stick toggle
// in #consoleDetailPanel must only ever appear on a touch-capable browser,
// since #touchControls (the on-screen overlay it actually controls) never
// exists on desktop at all -- desktop only ever gets the keyboard-rebind
// table instead (see openConsoleDetailPanel()'s own comment, index.html).
// Showing the toggle there would change a value with no visible effect.

import { test, expect } from '@playwright/test';

let servers;

test.afterEach(async () => {
  servers.wss.close();
  servers.server.close();
});

// A custom mock, unlike test_server.mjs's own GC_GBA_LINK/gba_buttons one --
// needs an N3DS_BOTTOM_SCREEN/n3ds_touch_and_buttons session so the console
// row under test is actually the one with a stick at all.
async function connectAsN3ds(page) {
  const { createServer } = await import('node:http');
  const { readFile } = await import('node:fs/promises');
  const { extname, join } = await import('node:path');
  const { WebSocketServer } = await import('ws');
  const WEB_DIR = new URL('.', import.meta.url);
  const MIME = { '.html': 'text/html', '.js': 'text/javascript', '.mjs': 'text/javascript' };

  const server = createServer(async (req, res) => {
    const urlPath = req.url === '/' ? '/index.html' : req.url;
    try {
      const filePath = join(WEB_DIR.pathname, urlPath);
      const body = await readFile(filePath);
      res.writeHead(200, { 'Content-Type': MIME[extname(filePath)] || 'application/octet-stream' });
      res.end(body);
    } catch {
      res.writeHead(404);
      res.end('not found');
    }
  });
  await new Promise((resolve) => server.listen(0, '127.0.0.1', resolve));

  const wss = new WebSocketServer({ host: '127.0.0.1', port: 0 });
  wss.on('connection', (socket) => {
    socket.send(JSON.stringify({
      message: 'hello', protocol_version: 2, stream_type: 'N3DS_BOTTOM_SCREEN',
      video: { width: 256, height: 192, fps: 60 }, input_encoding: 'n3ds_touch_and_buttons',
    }));
    let sentReady = false;
    socket.on('message', (data, isBinary) => {
      if (isBinary) return;
      if (!sentReady) {
        sentReady = true;
        socket.send(JSON.stringify({
          message: 'session_ready', slot: 0, video: { width: 256, height: 192, fps: 60 }, video_mode: 'tiles',
        }));
      }
    });
  });
  await new Promise((resolve) => wss.once('listening', resolve));

  await page.goto(`http://127.0.0.1:${server.address().port}/`);
  await page.fill('#hostInput', '127.0.0.1');
  await page.fill('#portInput', String(wss.address().port));
  await page.click('#connectSubmit');
  await page.waitForFunction(() => document.getElementById('game').style.display === 'flex', null, {
    timeout: 15000,
  });
  return { server, wss };
}

test('the 3DS second-stick toggle stays hidden on a desktop (non-touch) browser', async ({ page }) => {
  servers = await connectAsN3ds(page);

  await page.click('#settingsButton');
  await page.click('#consoleSettingsRowDesktop');
  await page.locator('#consoleRows button').first().click(); // N3DS_BOTTOM_SCREEN, KNOWN_STREAM_TYPES' first entry
  await expect(page.locator('#consoleDetailPanel')).toBeVisible();
  await expect(page.locator('#consoleDetailSecondStickRow')).toBeHidden();
  await expect(page.locator('#consoleDetailSecondStickHint')).toBeHidden();
});
