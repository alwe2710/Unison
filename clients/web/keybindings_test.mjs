// Unit tests for keybindings.js's pure desktop keyboard-rebind logic --
// "web-client: Tastenzuweisung im Einstellungsmenü funktioniert" test
// category. Same check()-helper convention as bridge_test.mjs/
// settings_test.mjs.

import { loadBindings, assignBinding, buildCodeMap } from "./keybindings.js";

let failures = 0;
function check(name, cond) {
  if (cond) {
    console.log("OK  ", name);
  } else {
    console.error("FAIL", name);
    failures++;
  }
}

// A small stand-in for the real BUTTONS list (index.html), same shape
// ([name, bit, defaultCode]) but easier to eyeball in test failures.
const BUTTONS = [
  ["A", 1 << 0, "KeyX"],
  ["B", 1 << 1, "KeyZ"],
  ["Start", 1 << 3, "Enter"],
];

// ---------------- loadBindings ----------------

check(
  "loadBindings: empty stored (first run) uses every button's own default",
  JSON.stringify(loadBindings(BUTTONS, {})) === JSON.stringify({ A: "KeyX", B: "KeyZ", Start: "Enter" })
);
check(
  "loadBindings: a stored override wins over the default for that one button",
  JSON.stringify(loadBindings(BUTTONS, { A: "KeyJ" })) === JSON.stringify({ A: "KeyJ", B: "KeyZ", Start: "Enter" })
);
check(
  "loadBindings: a stored entry for a button that no longer exists in BUTTONS is simply ignored, not an error",
  JSON.stringify(loadBindings(BUTTONS, { A: "KeyJ", NoLongerAButton: "KeyQ" })) ===
    JSON.stringify({ A: "KeyJ", B: "KeyZ", Start: "Enter" })
);

// ---------------- assignBinding ----------------

{
  const before = loadBindings(BUTTONS, {});
  const after = assignBinding(before, "A", "KeyJ");
  check("assignBinding: rebinds only the named button", after.A === "KeyJ" && after.B === "KeyZ");
  check("assignBinding: does not mutate the input object (returns a new one)", before.A === "KeyX");
}

// ---------------- buildCodeMap ----------------

{
  const bindings = loadBindings(BUTTONS, {});
  const map = buildCodeMap(BUTTONS, bindings);
  check("buildCodeMap: every default code maps back to its button's bit", map["KeyX"] === (1 << 0));
  check("buildCodeMap: covers every button in BUTTONS", map["KeyZ"] === (1 << 1) && map["Enter"] === (1 << 3));
}
{
  // The actual "does rebinding work" end-to-end check: after assigning a
  // new code to A, the OLD code must no longer resolve to A's bit (unless
  // another button still legitimately owns it), and the NEW code must.
  let bindings = loadBindings(BUTTONS, {});
  bindings = assignBinding(bindings, "A", "KeyJ");
  const map = buildCodeMap(BUTTONS, bindings);
  check("buildCodeMap: after rebinding A, the new code resolves to A's bit", map["KeyJ"] === (1 << 0));
  check("buildCodeMap: after rebinding A, the old code no longer resolves to anything", map["KeyX"] === undefined);
}
{
  // Two buttons deliberately bound to the same code (e.g. a hand-edited
  // localStorage value) -- documented as "last BUTTONS entry wins", not a
  // crash or silently-ignored assignment.
  const bindings = assignBinding(loadBindings(BUTTONS, {}), "B", "KeyX"); // now same as A's default
  const map = buildCodeMap(BUTTONS, bindings);
  check("buildCodeMap: a collided code resolves to the later BUTTONS entry (B, not A)", map["KeyX"] === (1 << 1));
}

if (failures > 0) {
  console.error(`${failures} test(s) failed`);
  process.exit(1);
}
console.log("keybindings: all tests passed");
