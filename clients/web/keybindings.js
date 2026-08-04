// Pure logic for the web client's desktop keyboard-rebind settings panel
// -- pulled out of index.html's inline <script> (renderSettings()'s button
// onclick handler, and the bindings/codeToButton setup above it) into its
// own file so it has one place to unit-test under Node (see
// keybindings_test.mjs) instead of only being exercisable in a real
// browser with real keyboard events. Loaded the same way as settings.js
// (plain <script src="keybindings.js">, classic script, with a
// module.exports guard at the bottom for Node).
//
// Deliberately excludes anything touching localStorage/DOM/keydown
// listeners directly (those stay in index.html's thin wrapper:
// saveBindings/rebuildCodeMap/renderSettings's button handler) -- these
// functions only take/return plain values, same reasoning as settings.js.

// stored: the parsed finlinkWebBindings localStorage object (name -> KeyboardEvent.code),
// possibly missing entries (a BUTTONS entry added after the value was
// saved) or, in principle, holding stale entries for a since-removed
// button (harmless, simply never looked up again). buttons: BUTTONS itself,
// [name, bit, defaultCode][] -- each entry's own default fills the gap for
// any name stored doesn't cover.
function loadBindings(buttons, stored) {
  const bindings = {};
  for (const [name, , defaultCode] of buttons) {
    bindings[name] = stored[name] || defaultCode;
  }
  return bindings;
}

// The actual "rebind" operation: name's code becomes newCode. Returns a
// new object rather than mutating bindings in place, so a caller that
// wants the old value for e.g. undo/display-diff purposes still has it.
function assignBinding(bindings, name, newCode) {
  return Object.assign({}, bindings, { [name]: newCode });
}

// The reverse lookup keydown handlers actually use: KeyboardEvent.code ->
// button bit. If two buttons were ever bound to the same code (shouldn't
// happen through the UI, which only ever sets one binding per click, but
// nothing stops two different localStorage edits from colliding), the
// last BUTTONS entry (in array order) silently wins -- documented here as
// the defined behavior this function is expected to produce, not
// considered an error case to reject.
function buildCodeMap(buttons, bindings) {
  const codeToButton = {};
  for (const [name, bit] of buttons) {
    codeToButton[bindings[name]] = bit;
  }
  return codeToButton;
}

if (typeof module !== 'undefined' && module.exports) {
  module.exports = { loadBindings, assignBinding, buildCodeMap };
}
