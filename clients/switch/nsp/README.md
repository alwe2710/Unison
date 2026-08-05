# NSP build (optional)

Off by default. Enable with:

```
cmake .. -DPLATFORM_SWITCH=ON -DBUILD_SWITCH_NSP=ON -DSWITCH_KEYS_FILE=/path/to/prod.keys
```

`SWITCH_KEYS_FILE` must point at a `prod.keys`/`keys.dat` you dumped yourself
from your own console (e.g. via
[Lockpick_RCM](https://github.com/shchmue/Lockpick_RCM)) or otherwise
legitimately obtained. This repo does not (and will not) bundle or fetch
Nintendo's NCA encryption keys — `header_key` and
`key_area_key_application_*` are the same across every console (not
console-unique), but they're still Nintendo's key material, not something
this project can distribute. `hacbrewpack` (the packing tool itself,
fetched/built from source by `build_hacbrewpack.sh`) contains no keys —
only the code to use whatever keyset you provide.

For everyday testing, the plain `.nro` (built unconditionally, run via the
Homebrew Menu) is the normal path and doesn't need any of this — NSP is
only relevant if you specifically want to install Unison as a system
title through custom firmware.
