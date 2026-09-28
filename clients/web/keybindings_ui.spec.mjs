// Real-browser tests for the "web-client key-remap" test category
// (checklist #66/67): keybindings_test.mjs already unit-tests
// loadBindings/assignBinding/buildCodeMap in isolation, but nothing
// previously drove the actual #keyBindingsPanel settings UI through a
// real browser -- clicking "Bind", pressing a physical key, and checking
// that the rebind (a) shows up in the table, (b) persists across a
// reload, and (c) actually rewires live input dispatch (old key stops
// working, new key works), not just index.html's in-memory `bindings`
// object.
//
// Desktop-only (no hasTouch), deliberately unlike overlay.spec.mjs:
// index.html only wires up #settingsButton/#keyBindingsRowDesktop inside
// its `if (!isMobile)` block (see beginSession()), and isMobile is gated
// on hasTouch/maxTouchPoints -- setting hasTouch here would route into the
// mobile branch instead, where #settingsButton is never shown at all.

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

// UNISON_MSG_INPUT only -- same ping-frame filtering rationale as
// overlay.spec.mjs's own inputFrames().
function inputFrames() {
  return wsServer.receivedFrames.filter((f) => f[0] === 2);
}

async function openKeyBindings(page) {
  await page.click('#settingsButton');
  await expect(page.locator('#settingsPanel')).toBeVisible();
  await page.click('#keyBindingsRowDesktop');
  await expect(page.locator('#keyBindingsPanel')).toBeVisible();
}

// Locates the row for a given button name by its exact nameCell text
// (renderSettings()'s first <td>, index.html) -- exact match matters since
// e.g. "A" would otherwise substring-match inside "Start"'s row too. The
// row's first <button> is always the keyboard "Bind" button (see
// renderSettings(): nameCell, keyCell, keyActionCell(button), padCell,
// padActionCell(button)) -- locale-independent, unlike matching on the
// button's own (translated) label text.
function rowFor(page, name) {
  return page.locator('#settingsTableBody tr').filter({
    has: page.locator('td', { hasText: new RegExp(`^${name}$`) }),
  });
}

test('binding the A button to a new key updates the table and persists across a reload', async ({ page }) => {
  await connect(page);
  await openKeyBindings(page);

  const rowA = rowFor(page, 'A');
  await expect(rowA.locator('td').nth(1)).toHaveText('KeyX'); // BUTTONS' own default for A, index.html

  await rowA.locator('button').first().click();
  await page.keyboard.press('p'); // -> code 'KeyP', not any other button's default

  await expect(rowA.locator('td').nth(1)).toHaveText('KeyP');
  expect(await page.evaluate(() => JSON.parse(localStorage.getItem('unisonWebBindings')).A)).toBe('KeyP');

  await connect(page); // reload + reconnect (goto() again inside connect())
  await openKeyBindings(page);
  await expect(rowFor(page, 'A').locator('td').nth(1)).toHaveText('KeyP');
});

test('rebinding A live-rewires input dispatch: the old key goes dead, the new key works, other buttons are untouched', async ({ page }) => {
  await connect(page);
  await openKeyBindings(page);

  await rowFor(page, 'A').locator('button').first().click();
  await page.keyboard.press('p');
  await expect(rowFor(page, 'A').locator('td').nth(1)).toHaveText('KeyP');

  await page.click('#closeKeyBindings');
  await expect(page.locator('#keyBindingsPanel')).toBeHidden();

  const before = inputFrames().length;
  await page.keyboard.press('x'); // old default (KeyX) -- must no longer resolve to anything
  await page.waitForTimeout(300);
  expect(inputFrames().length).toBe(before); // no frame at all: codeToButton['KeyX'] no longer set

  // down()/up() separately, not press() -- press() fires keydown+keyup
  // back to back, so the *last* recorded frame would already be the
  // release (keyState back to 0), not the press this assertion cares about.
  await page.keyboard.down('p'); // new binding -- must resolve to A's bit (1<<0)
  await expect.poll(() => inputFrames().length, { timeout: 5000 }).toBeGreaterThan(before);
  const pressFrame = inputFrames()[inputFrames().length - 1];
  expect(pressFrame.readUInt16LE(1)).toBe(1 << 0);
  await page.keyboard.up('p');

  // B's own default (KeyZ) must still work -- confirms the rebind only
  // touched A's entry in `bindings`/codeToButton, not the whole map.
  const beforeB = inputFrames().length;
  await page.keyboard.down('z');
  await expect.poll(() => inputFrames().length, { timeout: 5000 }).toBeGreaterThan(beforeB);
  const bFrame = inputFrames()[inputFrames().length - 1];
  expect(bFrame.readUInt16LE(1)).toBe(1 << 1); // UNISON_KEY_B
  await page.keyboard.up('z');
});

test('binding a gamepad-less controller "Bind" button is disabled, and the hint is shown', async ({ page }) => {
  await connect(page);
  await openKeyBindings(page);

  await expect(page.locator('#gamepadHint')).not.toBeEmpty();
  const padBtn = rowFor(page, 'A').locator('button').nth(1);
  await expect(padBtn).toBeDisabled();
});

// X/Y/ZL/ZR get their own "Extended" section + hint here (renderSettings(),
// index.html) -- same console-dependent set #touchControls' on-screen .ext
// group gates (see touch_extended_buttons.spec.mjs), but always listed here
// regardless of the connected session's actual stream_type: this table is
// reachable before a stream_type is even known (the pre-connect settings
// menu), unlike that overlay which only exists once connected.
test('the key-bindings table splits standard and extended (X/Y/ZL/ZR) buttons into two labeled sections', async ({ page }) => {
  await connect(page);
  await openKeyBindings(page);

  const rows = page.locator('#settingsTableBody tr');
  const sectionRows = rows.filter({ hasNot: page.locator('td:nth-child(2)') });
  // Two section headers (Standard/Extended) + one hint row under Extended.
  await expect(sectionRows).toHaveCount(3);

  // Standard section (first) lists A first, no X/Y/ZL/ZR row anywhere
  // before the "Extended" header -- BUTTONS' own order (index.html) starts
  // with A.
  const firstDataRow = rows.filter({ has: page.locator('td:nth-child(2)') }).first();
  await expect(firstDataRow.locator('td').first()).toHaveText('A');

  for (const name of ['X', 'Y', 'ZL', 'ZR']) {
    await expect(rowFor(page, name)).toHaveCount(1);
  }
});

// Regression guard: applyStaticStrings() (index.html, the language-live-
// switch fix) once emptied this button's whole content -- including its
// gear glyph, which isn't translatable text at all -- while clearing its
// hardcoded title tooltip for i18n, leaving a visible-but-blank button.
test('the settings gear button actually renders its icon', async ({ page }) => {
  await connect(page);
  await expect(page.locator('#settingsButton')).toHaveText('⚙');
});
