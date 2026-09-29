// Cemu's WIIU_GAMEPAD stream removed both raw video modes (legacy/TILES)
// entirely (WiiuGamepadStream.cpp's own SendVideoFrame() comment) -- the
// per-console video-mode picker must stop offering either one there, while
// every other console keeps all four (none of them removed anything).

import { test, expect } from '@playwright/test';
import { startStaticServer, startWsServer } from './test_server.mjs';

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
}

// KNOWN_STREAM_TYPES' own order (index.html): N3DS_BOTTOM_SCREEN,
// GC_GBA_LINK, NDS_BOTTOM_SCREEN, WIIU_GAMEPAD -- index 3 is Wii U.
async function openVideoModePickerFor(page, consoleRowIndex) {
  await page.click('#settingsButton');
  await page.click('#consoleSettingsRowDesktop');
  await page.locator('#consoleRows button').nth(consoleRowIndex).click();
  await page.click('#consoleDetailVideoModeRow');
}

test('the WIIU_GAMEPAD video-mode picker offers only h264/h265, not the raw modes', async ({ page }) => {
  await connect(page);
  await openVideoModePickerFor(page, 3);

  const options = await page.locator('#videoModeOptionsList button').allTextContents();
  expect(options).toHaveLength(2);
  expect(options).toContain('H.264');
  expect(options).toContain('H.265');
});

test('every other console still offers all four video modes', async ({ page }) => {
  // Fresh connect() per console rather than navigating back through the
  // close-button chain -- picking an option (the only way back from
  // #videoModePanel without a dedicated "cancel") would itself change the
  // very state this test is only trying to observe.
  for (const consoleRowIndex of [0, 1, 2]) {
    await connect(page);
    await openVideoModePickerFor(page, consoleRowIndex);
    const options = await page.locator('#videoModeOptionsList button').allTextContents();
    expect(options).toHaveLength(4);
  }
});

test('a video-mode preference saved as "tiles" before this change is normalized to h264 for WIIU_GAMEPAD', async ({ page }) => {
  await page.addInitScript(() => {
    localStorage.setItem('unisonWebVideoModeByStreamType', JSON.stringify({ WIIU_GAMEPAD: 'tiles' }));
  });
  await connect(page);
  await page.click('#settingsButton');
  await page.click('#consoleSettingsRowDesktop');
  await page.locator('#consoleRows button').nth(3).click(); // WIIU_GAMEPAD

  // #consoleDetailVideoModeRow's own subtitle (videoModeValue) -- reads
  // videoModeFor() (index.html), which normalizes a stale legacy/tiles
  // pref to h264 for this streamType specifically.
  await expect(page.locator('#videoModeValue')).toHaveText('H.264');
});
