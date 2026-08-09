# docs/altstore

An [AltStore](https://faq.altstore.io/create-your-own-source/general-source-schema)-
compatible source, served via GitHub Pages
(`https://alwe2710.github.io/Unison/altstore/apps.json`) so the iOS client
(`clients/ios`) can be installed via AltStore/SideStore instead of a Mac +
Xcode + cable.

## Why this works with no Apple Developer account

`apps.json`'s `downloadURL` points at an **unsigned** `.ipa`
(`.github/workflows/build.yml`'s `ios` job: `xcodebuild archive` with
`CODE_SIGNING_ALLOWED=NO`, then a plain `Payload/Unison.app` zip). AltStore/
SideStore don't need a pre-signed `.ipa` at all -- they re-sign it on-device
at install time using *your own* free Apple ID via a paired AltServer/
SideStore-companion, the same "sideloading" trick Xcode's own "Personal
Team" signing uses. That's also why this only ever needed the "unsigned/
Simulator for now" choice made when this client was started, not a paid
Developer account.

Consequence to know: a free-Apple-ID sideload is only valid for **7 days**,
after which AltStore/SideStore need to refresh it (automatic if AltServer/
the companion app is reachable, otherwise a manual re-add from this same
source).

## Keeping this current

**Bump the version first, before building anything** --
`clients/ios/project.yml`'s `MARKETING_VERSION`/`CURRENT_PROJECT_VERSION`
(that file's own comment next to them has the details). This step is easy
to skip since the build succeeds fine without it, but skipping it is a
real, silent failure mode: AltStore/SideStore only offer an "Update"
button when a version/buildVersion string actually differs from what's
already installed on the device -- refreshing `apps.json`'s `size`/`date`
alone, or even re-uploading a materially different `.ipa` to the same
`ios-latest` release asset, does nothing for anyone who already has the
previous build installed (this bit us for one release: the H.264/H.265
work shipped and was live on the release asset, but stayed invisible to
already-installed devices because `MARKETING_VERSION` was untouched).

Then, once CI (triggered by that commit) has produced a new `Unison.ipa`:

```sh
gh run download <run-id> --repo alwe2710/Unison --name Unison-ipa --dir /tmp/unison-ipa
gh release upload ios-latest /tmp/unison-ipa/Unison.ipa --repo alwe2710/Unison --clobber
```

`apps.json`'s `downloadURL`/`size` point at that GitHub Release asset (tag
`ios-latest`), not the CI workflow artifact directly -- workflow artifacts
need a signed-in GitHub session to download, which AltStore can't do; a
Release asset is a stable, public, unauthenticated URL. Update
`versions[0]`'s `version`/`buildVersion` to **exactly** match what
`project.yml` now declares (AltStore rejects the download outright on any
mismatch against the `.ipa`'s real `CFBundleShortVersionString`/
`CFBundleVersion` -- see that file's own comment), plus `size` (byte count
of the new `.ipa`) and `date`. This is a single rolling channel, not a
version history -- `versions` should only ever hold the one current entry,
since the shared `ios-latest` asset URL is clobbered on every update and
couldn't actually serve an older entry's declared bytes anyway.

## Status

Past the MVP scaffold now: touch input, extended input (buttons/sticks),
audio, and H.264/H.265 video decode (`VideoToolbox`/
`AVSampleBufferDisplayLayer`, see `clients/ios/Sources/Models/
CompressedVideoDecoder.swift`) are all implemented. Still missing: mic
input and on-screen text input (both still MVP-deferred, see
`unison_native_bridge.c`'s own top-of-file comment).
