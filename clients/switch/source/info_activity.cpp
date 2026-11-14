#include "info_activity.hpp"

#include <cstdio>

#include "strings_generated.hpp"

// UNISON_APP_VERSION: `git describe --tags --always --dirty` at configure
// time, see CMakeLists.txt's own comment. Same mechanism as the Android/3DS
// clients' Info screens.
#ifndef UNISON_APP_VERSION
#define UNISON_APP_VERSION "unknown"
#endif

brls::View *InfoActivity::createContentView() {
    auto *column = new brls::Box();
    column->setAxis(brls::Axis::COLUMN);
    column->setAlignItems(brls::AlignItems::STRETCH);
    column->setPadding(24, 32, 24, 32);

    auto *nameLabel = new brls::Label();
    nameLabel->setText(strings::kAppName);
    nameLabel->setFontSize(28);
    column->addView(nameLabel);

    char versionLine[64];
    snprintf(versionLine, sizeof(versionLine), strings::kInfoVersion, UNISON_APP_VERSION);
    auto *versionLabel = new brls::Label();
    versionLabel->setText(versionLine);
    versionLabel->setMarginTop(12);
    column->addView(versionLabel);

    char licenseLine[64];
    snprintf(licenseLine, sizeof(licenseLine), strings::kInfoLicense, "PolyForm Noncommercial 1.0.0");
    auto *licenseLabel = new brls::Label();
    licenseLabel->setText(licenseLine);
    licenseLabel->setMarginTop(6);
    column->addView(licenseLabel);

    // Switch homebrew has no browser applet to hand a link to, same
    // reasoning as the 3DS Info screen -- shown as plain static text (repo
    // host/owner/name), not a clickable row like Android's.
    auto *repoLabel = new brls::Label();
    repoLabel->setText(strings::kInfoRepo);
    repoLabel->setMarginTop(18);
    column->addView(repoLabel);

    auto *repoUrlLabel = new brls::Label();
    repoUrlLabel->setText("github.com/alwe2710/Unison");
    column->addView(repoUrlLabel);

    auto *scroll = new brls::ScrollingFrame();
    scroll->setGrow(1.0f);
    scroll->setContentView(column);

    auto *frame = new brls::AppletFrame(scroll);
    frame->setTitle(strings::kSettingsInfo);
    return frame;
}
