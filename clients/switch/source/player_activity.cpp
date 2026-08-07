#include "player_activity.hpp"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <malloc.h>

#include "audio_remix.hpp"
#include "frame_poller.hpp"
#include "gba_buttons.hpp"
#include "strings_generated.hpp"

namespace {
constexpr float kExitHoldSeconds = 0.6f;
}

PlayerActivity::PlayerActivity(std::string host, int port, std::string streamType)
    : host(std::move(host)), port(port), streamType(std::move(streamType)) {
}

PlayerActivity::~PlayerActivity() {
    // Must join the session thread (which disconnect() does) before this
    // object -- and everything its callbacks capture by `this` -- goes
    // away, or a callback firing mid-teardown would use freed memory.
    session.disconnect();
    closeAudio();
}

brls::View *PlayerActivity::createContentView() {
    auto *root = new brls::Box();
    root->setWidthPercentage(100);
    root->setHeightPercentage(100);

    // Without a focusable view somewhere in this tree, pushActivity()'s
    // Application::giveFocus(activity->getDefaultFocus()) call finds
    // nothing (Box::getDefaultFocus() returns nullptr when no descendant
    // is focusable) and giveFocus() leaves currentFocus untouched -- so
    // every button/dpad press kept going to whatever was still focused in
    // MenuActivity underneath, including its "B = back" AppletFrame
    // action, and its highlight box kept getting drawn on top of the
    // video (Application::frame() draws currentFocus's highlight globally,
    // regardless of which activity is actually on top). setHideHighlight
    // suppresses the highlight box borealis would otherwise draw around
    // this now-focused, screen-filling root.
    root->setFocusable(true);
    root->setHideHighlight(true);

    videoView = new VideoView();
    videoView->setBilinearFilter(prefs.bilinearFor(streamType));
    root->addView(videoView);

    statusLabel = new brls::Label();
    statusLabel->setText(strings::kStatusConnecting);
    statusLabel->setTextColor(nvgRGB(255, 255, 255));
    statusLabel->detach();
    statusLabel->setWidth(400);
    root->addView(statusLabel);
    statusLabel->setDetachedPosition(16, 16);

    auto *exitHint = new brls::Label();
    exitHint->setText("ZL+ZR halten zum Beenden");
    exitHint->setTextColor(nvgRGBA(255, 255, 255, 160));
    exitHint->setFontSize(14);
    exitHint->detach();
    exitHint->setWidth(300);
    root->addView(exitHint);
    // Fixed offset below statusLabel rather than anchored to the root's
    // height: layout hasn't run yet at construction time, so getHeight()
    // would still read 0 here.
    exitHint->setDetachedPosition(16, 48);

    auto *poller = new FramePoller([this]() { onFrameTick(); });
    root->addView(poller);

    session.connect(host, port, prefs.videoModeFor(streamType),
        GbaSession::Listener {
            .onConnected =
                [this](std::string grantedVideoMode) {
                    brls::sync([this, grantedVideoMode]() {
                        connected = true;
                        statusLabel->setVisibility(brls::Visibility::GONE);
                        // Empty grantedVideoMode means the server predates
                        // session_ready.video_mode entirely -- skip the
                        // comparison rather than assuming "tiles" was
                        // granted, see docs/protocol.md "Video-mode
                        // fallback" and GbaSession::Listener's own comment.
                        const std::string requested = prefs.videoModeFor(streamType);
                        if (!grantedVideoMode.empty() && grantedVideoMode != requested) {
                            showVideoModeFallbackDialog(requested, grantedVideoMode);
                        }
                    });
                },
            .onVideoFrame =
                [this](uint32_t width, uint32_t height, std::vector<uint8_t> rgb565) {
                    if (videoView) {
                        videoView->setFrame(width, height, rgb565);
                    }
                },
            .onCompressedVideoFrame =
                [this](uint32_t /*width*/, uint32_t /*height*/, bool isH265, std::vector<uint8_t> data) {
                    // Built on first use, not eagerly in onConnected --
                    // this handler is the first point isH265 is actually
                    // needed, and building it here means a session that
                    // negotiated h264/h265 but (for whatever reason) never
                    // gets a compressed frame at all never pays for a
                    // decoder it never used. Same thread as onVideoFrame
                    // above (the session's own background thread, not the
                    // render thread) -- H264Decoder::decode() and
                    // VideoView::setFrameRGBA() are both safe to call
                    // directly from here, no brls::sync() needed, same
                    // reasoning as onVideoFrame's own direct
                    // videoView->setFrame() call.
                    if (!compressedVideoDecoder) {
                        compressedVideoDecoder = std::make_unique<H264Decoder>(isH265);
                    }
                    if (!compressedVideoDecoder->isValid() || !videoView) {
                        return;
                    }
                    std::vector<uint8_t> rgba;
                    uint32_t decodedWidth = 0, decodedHeight = 0;
                    if (compressedVideoDecoder->decode(data.data(), data.size(), rgba, decodedWidth,
                                                        decodedHeight)) {
                        videoView->setFrameRGBA(decodedWidth, decodedHeight, rgba);
                    }
                },
            .onAudioFrame =
                [this](uint32_t sampleRate, uint8_t channels, std::vector<int16_t> pcm) {
                    playAudio(sampleRate, channels, std::move(pcm));
                },
            .onDisconnected =
                [this](std::string reason) {
                    brls::sync([this, reason]() {
                        // The session thread calls onDisconnected
                        // unconditionally whenever it exits, including the
                        // clean, intentional case (disconnect() from our
                        // own destructor, itself triggered by the ZL+ZR
                        // hold-to-exit popActivity()) -- `exiting` is set
                        // synchronously right when that starts, so by the
                        // time this fires (well after the pop, since it's
                        // the destructor that calls disconnect()), it
                        // reliably tells intentional exits apart from a
                        // real dropped connection.
                        if (exiting) {
                            return;
                        }
                        bool wasConnected = connected;
                        connected = false;
                        if (wasConnected) {
                            showDisconnectDialog(reason);
                        } else {
                            statusLabel->setVisibility(brls::Visibility::VISIBLE);
                            char buf[192];
                            snprintf(buf, sizeof(buf), strings::kStatusError, reason.c_str());
                            statusLabel->setText(buf);
                        }
                    });
                },
        });

    return root;
}

void PlayerActivity::showDisconnectDialog(const std::string &reason) {
    auto *dialog = new brls::Dialog(std::string(strings::kStreamLostTitle) + "\n" + reason);
    dialog->setCancelable(false);
    dialog->addButton("OK", [this]() { brls::Application::popActivity(); });
    dialog->open();
}

namespace {
// Wire-format string -> label, mirrors settings_activity.cpp's own
// labelForVideoMode() (kept local here rather than shared -- same
// duplication settings_activity.cpp itself already accepts for
// labelForStreamType() vs menu_activity.cpp's kStreamTypeGcGbaLink).
const char *videoModeLabel(const std::string &mode) {
    if (mode == "h264") return strings::kVideoModeH264;
    if (mode == "h265") return strings::kVideoModeH265;
    if (mode == "legacy") return strings::kVideoModeLegacy;
    return strings::kVideoModeTiles;
}
} // namespace

// Non-blocking: the stream is already live in the granted mode by the time
// this can even fire (session_ready already committed the server to
// `granted`) -- "Fortsetzen" just dismisses it, "Abbrechen" pops back to
// the menu same as a normal disconnect.
void PlayerActivity::showVideoModeFallbackDialog(const std::string &requested, const std::string &granted) {
    char message[256];
    snprintf(message, sizeof(message), strings::kVideoModeFallbackMessage,
              videoModeLabel(requested), videoModeLabel(granted));
    auto *dialog = new brls::Dialog(std::string(strings::kVideoModeFallbackTitle) + "\n" + message);
    dialog->setCancelable(false);
    dialog->addButton(strings::kVideoModeFallbackAbort, [this]() { brls::Application::popActivity(); });
    dialog->addButton(strings::kVideoModeFallbackContinue, []() {});
    dialog->open();
}

void PlayerActivity::sendCombinedInput() {
    session.sendInput(physicalMask);
}

void PlayerActivity::onFrameTick() {
    if (!connected || exiting) {
        return;
    }

    brls::ControllerState state {};
    brls::Application::getPlatform()->getInputManager()->updateUnifiedControllerState(&state);

    uint16_t newPhysicalMask = 0;
    for (const auto &button : GBA_BUTTONS) {
        if (state.buttons[button.defaultController]) {
            newPhysicalMask |= button.bit;
        }
    }
    physicalMask = newPhysicalMask;
    sendCombinedInput();

    // Hold-to-exit: ZL+ZR (the triggers) -- see header comment for why
    // this replaces Android's system back button/gesture here.
    static auto lastTick = std::chrono::steady_clock::now();
    auto now = std::chrono::steady_clock::now();
    float dt = std::chrono::duration<float>(now - lastTick).count();
    lastTick = now;

    if (state.buttons[brls::BUTTON_LT] && state.buttons[brls::BUTTON_RT]) {
        exitHoldSeconds += dt;
        if (exitHoldSeconds >= kExitHoldSeconds) {
            exiting = true;
            // popActivity() mutates the activity/view tree (hides this
            // activity, eventually erases+frees it) -- calling it directly
            // from here is calling it mid-frame, from inside the very
            // draw() pass that's currently iterating that same tree.
            // brls::sync() defers it to Threading::performSyncTasks()'s
            // dedicated, safe point in the main loop instead, same as
            // every other place in this app that mutates views outside of
            // a normal click handler.
            brls::sync([]() { brls::Application::popActivity(); });
        }
    } else {
        exitHoldSeconds = 0.0f;
    }
}

void PlayerActivity::reclaimAudioBuffers() {
    for (;;) {
        AudioOutBuffer *released = nullptr;
        u32 count = 0;
        if (R_FAILED(audoutGetReleasedAudioOutBuffer(&released, &count)) || count == 0 || released == nullptr) {
            break;
        }
        free(released->buffer);
        delete released;
    }
}

void PlayerActivity::playAudio(uint32_t sampleRate, uint8_t channels, std::vector<int16_t> pcm) {
    if (sampleRate == 0 || channels == 0 || pcm.empty()) {
        return;
    }

    if (!audioOpen) {
        if (R_FAILED(audoutInitialize()) || R_FAILED(audoutStartAudioOut())) {
            return;
        }
        audioOpen = true;
    }

    reclaimAudioBuffers();

    // The device is fixed at 48000Hz/stereo/s16 (audoutGetSampleRate() /
    // audoutGetChannelCount()) -- unison_switch_remix_and_resample_to_stereo()
    // (audio_remix.hpp/.cpp) does the actual remix/resample math, kept free
    // of borealis/libnx types so it has one place to unit-test, see that
    // file's own comment.
    std::vector<int16_t> resampled = unison_switch_remix_and_resample_to_stereo(pcm, sampleRate, channels);
    if (resampled.empty()) {
        return;
    }

    size_t dataSize = resampled.size() * sizeof(int16_t);
    size_t bufferSize = (dataSize + 0xFFF) & ~static_cast<size_t>(0xFFF);

    void *mem = memalign(0x1000, bufferSize);
    if (!mem) {
        return;
    }
    memcpy(mem, resampled.data(), dataSize);

    auto *buf = new AudioOutBuffer();
    memset(buf, 0, sizeof(*buf));
    buf->buffer = mem;
    buf->buffer_size = bufferSize;
    buf->data_size = dataSize;

    if (R_FAILED(audoutAppendAudioOutBuffer(buf))) {
        free(mem);
        delete buf;
    }
}

void PlayerActivity::closeAudio() {
    if (!audioOpen) {
        return;
    }
    audoutStopAudioOut();
    reclaimAudioBuffers();
    audoutExit();
    audioOpen = false;
}
