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

`apps.json`'s `downloadURL`/`size` point at a GitHub Release asset (tag
`ios-latest`), not the CI workflow artifact directly -- workflow artifacts
need a signed-in GitHub session to download, which AltStore can't do; a
Release asset is a stable, public, unauthenticated URL. After a CI run
produces a new `Unison.ipa`:

```sh
gh run download <run-id> --repo alwe2710/Unison --name Unison-ipa --dir /tmp/unison-ipa
gh release upload ios-latest /tmp/unison-ipa/Unison.ipa --repo alwe2710/Unison --clobber
```

then update `apps.json`'s `size` (byte count of the new `.ipa`) and
`version`/`date` to match.

## Status

This is still the MVP scaffold (see `clients/ios/README.md`'s "Phasing"
section) -- installable, but there's no real streaming session, video, or
audio behind it yet, just the connect-form placeholder screen.
