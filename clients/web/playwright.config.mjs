// Config for the "universal-client: On-Screen-Overlay echte UI-Tests" test
// category -- real browser, real DOM events, real (locally-served)
// WebSocket connection; see overlay.spec.mjs for what's actually tested
// and why. Deliberately minimal: no reporters/retries setup beyond
// Playwright's own defaults, since this is meant to run the same way
// bridge_test.mjs/settings_test.mjs do (a single `npx playwright test`
// invocation in CI), not as a full end-user test suite with its own
// tooling conventions.
import { defineConfig } from '@playwright/test';

export default defineConfig({
  testDir: '.',
  testMatch: '*.spec.mjs',
  fullyParallel: false,
  reporter: 'list',
  use: {
    headless: true,
  },
});
