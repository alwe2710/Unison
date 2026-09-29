// Real-browser tests for the general (not per-console) hardware/software
// decoder toggle -- mirrors Android's Prefs.hardwareDecodeEnabled/Settings'
// "Hardware-Decoder" switch, and feeds WebCodecs' own
// VideoDecoderConfig.hardwareAcceleration hint (index.html's
// ensureVideoDecoder()). Two variants of the same row exist
// (#toggleHardwareDecodeDesktop in #settingsPanel, #toggleHardwareDecodeMobile
// in #menuPanel) since exactly one of those two panels is ever shown,
// depending on isMobile -- same split as overlay.spec.mjs (mobile-only) and
// console_settings_desktop.spec.mjs (desktop-only) test, but this preference
// applies on both, so both get covered here.

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

test.describe('desktop', () => {
  test('hardware decode is enabled by default and persists across a reload', async ({ page }) => {
    await connect(page);
    await page.click('#settingsButton');
    const toggle = page.locator('#toggleHardwareDecodeDesktop');
    await expect(toggle).toBeChecked();

    await toggle.uncheck();
    expect(await page.evaluate(() => localStorage.getItem('unisonWebHardwareDecode'))).toBe('false');

    await connect(page); // reload + reconnect
    await page.click('#settingsButton');
    await expect(page.locator('#toggleHardwareDecodeDesktop')).not.toBeChecked();
  });
});

test.describe('mobile', () => {
  test.use({ hasTouch: true, viewport: { width: 400, height: 800 } });

  test('hardware decode is enabled by default and persists across a reload', async ({ page }) => {
    await connect(page);
    await page.click('#menuButton');
    const toggle = page.locator('#toggleHardwareDecodeMobile');
    await expect(toggle).toBeChecked();

    await toggle.uncheck();
    expect(await page.evaluate(() => localStorage.getItem('unisonWebHardwareDecode'))).toBe('false');

    await connect(page); // reload + reconnect
    await page.click('#menuButton');
    await expect(page.locator('#toggleHardwareDecodeMobile')).not.toBeChecked();
  });
});
