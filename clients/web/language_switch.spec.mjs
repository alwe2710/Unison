// Real-browser test for the "language doesn't switch live" bug report:
// picking a language in #languagePanel only ever updated
// updateLanguageRowValues()'s own "current language" subtitle --
// everything else static on screen (panel titles, close buttons, column
// headers, ...) was either hardcoded German with no t() call at all, or
// only ever re-set the next time some panel-open function happened to run
// again. applyStaticStrings() (index.html) now runs once at load and again
// from setLangPref(), so a language pick retranslates what's already
// visible immediately, not just future panels.

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
  // Deterministic starting language -- the actual browser/CI default
  // locale resolves to English regardless of this file's own comments
  // about "the bug report's exact German strings", so force German
  // explicitly rather than relying on navigator.language.
  await page.addInitScript(() => localStorage.setItem('unisonWebLanguage', 'de'));
  await page.goto(staticServer.url);
  await page.fill('#hostInput', '127.0.0.1');
  await page.fill('#portInput', String(wsServer.port));
  await page.click('#connectSubmit');
  await page.waitForFunction(() => document.getElementById('game').style.display === 'flex', null, {
    timeout: 15000,
  });
}

async function pickLanguage(page, labelText) {
  await page.getByRole('button', { name: labelText, exact: true }).click();
}

test('picking a language retranslates already-visible static UI immediately, no reopen needed', async ({ page }) => {
  await connect(page);

  await page.click('#settingsButton');
  // Sanity: German is the default in this test environment (no explicit
  // pref set, no matching navigator.language) -- these are the exact
  // hardcoded-German strings the bug report described.
  await expect(page.locator('#settingsPanelTitle')).toHaveText('Einstellungen');
  await expect(page.locator('#closeSettings')).toHaveText('Schließen');

  await page.click('#languageRowDesktop');
  await pickLanguage(page, 'English');

  // Back on #settingsPanel (openLanguagePanel()'s own return-to-caller
  // flow) -- still the SAME already-rendered #settingsPanel DOM, not a
  // fresh openSettingsPanel() call, so this only passes if setLangPref()
  // itself retranslated it.
  await expect(page.locator('#settingsPanel')).toBeVisible();
  await expect(page.locator('#settingsPanelTitle')).toHaveText('Settings');
  await expect(page.locator('#closeSettings')).toHaveText('Close');
  await expect(page.locator('#languageLabelDesktop')).toHaveText('Language');

  // A panel that wasn't even open yet also picks it up correctly (not
  // just the one visible at the moment of the switch).
  await page.click('#keyBindingsRowDesktop');
  await expect(page.locator('#keyButtonColumnHeader')).toHaveText('Button');
});

test('the connect screen itself is translated on first load in a non-German browser', async ({ page }) => {
  await page.goto(staticServer.url);
  await expect(page.locator('#connectTitle')).toHaveText('Connect to server');
  await expect(page.locator('#connectStatus')).toHaveText("Enter the emulator's IP address");
  await expect(page.locator('#probePortsBtn')).toHaveText('Find port automatically');
  await expect(page.locator('#connectSubmit')).toHaveText('Connect');
});
