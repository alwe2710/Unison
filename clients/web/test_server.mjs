// Minimal real HTTP + WebSocket server for overlay.spec.mjs -- serves this
// directory's own static files (the same index.html/settings.js/
// keybindings.js/finlink_core.js a real finlink host's browser client
// would load) on one port, and accepts real WebSocket connections on
// another, recording every binary frame a connected page sends.
//
// Does run a real (if minimal) finlink app-level handshake -- sends
// `hello` on connect, replies `session_ready` to the client's `hello_ack`
// -- found to be necessary the hard way: #touchControls turns out to be
// nested *inside* #game (confirmed by counting divs in index.html, not
// assumed from the CSS alone), and #game only becomes visible via
// showGame() once session_ready actually arrives (see index.html's own
// msgType === 2 handler) -- reaching WebSocket.OPEN alone, as this file's
// own comment used to (wrongly) assume, is not enough for the overlay to
// ever actually render.

import { createServer } from 'node:http';
import { readFile } from 'node:fs/promises';
import { extname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { WebSocketServer } from 'ws';

const WEB_DIR = fileURLToPath(new URL('.', import.meta.url));

const MIME = {
  '.html': 'text/html',
  '.js': 'text/javascript',
  '.mjs': 'text/javascript',
};

export async function startStaticServer() {
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

// The minimal hello a real GC_GBA_LINK host sends first (docs/protocol.md)
// -- single slot (no "slots" array at all: the client's own slotCount ===
// 0 path sends hello_ack immediately with slot 0, same as a real single-
// slot host, see index.html's handleHandshakeMessage()), gba_buttons
// input_encoding to match overlay.spec.mjs's own BUTTONS-array assertions.
const HELLO_JSON = JSON.stringify({
  message: 'hello',
  protocol_version: 2,
  stream_type: 'GC_GBA_LINK',
  video: { width: 240, height: 160, fps: 59.7275 },
  input_encoding: 'gba_buttons',
});

// video_mode: "tiles" -- matches requestedVideoMode's own default
// (Prefs.videoMode-equivalent, VIDEO_MODE_DEFAULT in index.html) so
// showVideoModeFallback() never fires and gets in the way of these tests.
const SESSION_READY_JSON = JSON.stringify({
  message: 'session_ready',
  slot: 0,
  video: { width: 240, height: 160, fps: 59.7275 },
  video_mode: 'tiles',
});

// receivedFrames: array of Buffer, one per *binary* WS message the page
// sent us (Video/Audio/Input, finlink/protocol.h), in order -- the actual
// thing overlay.spec.mjs's "protocol-conformant bytes" assertions read.
// Runs the minimal real handshake above so the page actually reaches
// showGame() (see this file's own top comment on why that's required, not
// optional, for the overlay to ever become visible at all) -- doesn't
// inspect the client's own hello_ack content at all, since that's already
// covered by settings_test.mjs/keybindings_test.mjs and every host's own
// FinlinkMessages tests, not this test category's concern.
export async function startWsServer() {
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
      // First text frame after hello is hello_ack -- reply session_ready
      // exactly once (a real host wouldn't re-send it for a stray extra
      // text message either).
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
