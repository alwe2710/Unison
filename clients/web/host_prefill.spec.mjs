// This page is normally served *by* the Unison host itself (a real
// emulator's lobby GET / response, not a separate static server the way
// these tests' own test_server.mjs serves it) -- the Host field should
// default to location.hostname (the address the browser used to reach
// this very page) rather than staying empty, since that's almost always
// the right server address too.

import { test, expect } from '@playwright/test';
import { startStaticServer } from './test_server.mjs';

let staticServer;

test.beforeAll(async () => {
  staticServer = await startStaticServer();
});
test.afterAll(async () => {
  staticServer.server.close();
});

test('the Host field defaults to location.hostname on first visit', async ({ page }) => {
  await page.goto(staticServer.url);
  await expect(page.locator('#hostInput')).toHaveValue('127.0.0.1');
});

test('a previously-used host (localStorage) still wins over location.hostname', async ({ page }) => {
  await page.addInitScript(() => localStorage.setItem('unisonLastHost', '10.0.0.5'));
  await page.goto(staticServer.url);
  await expect(page.locator('#hostInput')).toHaveValue('10.0.0.5');
});

test('an explicit ?host= query param still wins over everything else', async ({ page }) => {
  await page.addInitScript(() => localStorage.setItem('unisonLastHost', '10.0.0.5'));
  await page.goto(`${staticServer.url}?host=192.168.9.9`);
  await expect(page.locator('#hostInput')).toHaveValue('192.168.9.9');
});
