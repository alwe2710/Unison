# assets/logo

`finlink-logo.png` (1254×1254, opaque background) is the shared brand asset
for all clients — derive every platform's app/launcher icon from this one
file instead of maintaining separate art per platform.

Currently used by:

- `clients/android/app/src/main/res/mipmap-*/ic_launcher.png` +
  `ic_launcher_round.png` — plain resized copies (legacy square launcher
  icon, not an Android adaptive icon with separate foreground/background
  layers — the source has no transparent foreground to split out).
- `clients/3ds/icon.png` — source for the `.smdh` icon (24×24 + 48×48,
  platform-specific tiled RGB565 format, built via `bannertool`/`makerom`
  as part of the normal CMake build).
- `clients/switch/icon.jpg` — `.nacp` control data icon (256×256 JPEG),
  embedded into the `.nro` as part of the normal CMake build.

Still needed:

- **NDS**: banner icon (32×32, 4bpp indexed/paletted, built via `ndstool`)
  — the build currently falls back to devkitPro's generic default icon
  (`calico/share/nds-icon.bmp`, see `clients/nds/Makefile`).

Regenerate the Android icons after replacing `finlink-logo.png`:

```sh
python3 -c "
from PIL import Image
src = Image.open('assets/logo/finlink-logo.png').convert('RGB')
sizes = {'mipmap-mdpi': 48, 'mipmap-hdpi': 72, 'mipmap-xhdpi': 96,
         'mipmap-xxhdpi': 144, 'mipmap-xxxhdpi': 192}
import os
for folder, size in sizes.items():
    d = f'clients/android/app/src/main/res/{folder}'
    os.makedirs(d, exist_ok=True)
    resized = src.resize((size, size), Image.LANCZOS)
    resized.save(f'{d}/ic_launcher.png')
    resized.save(f'{d}/ic_launcher_round.png')
"
```
