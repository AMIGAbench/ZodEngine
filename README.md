# Zod Engine — AmigaOS port

A native AmigaOS 3.2 port of the **Zod Engine**, an open source
re-implementation of *Z* (Bitmap Brothers, 1996) written by Nighsoft.

**No game data is in this repository.** The graphics, sounds and music belong
to the Bitmap Brothers. They are read from the user's own CD of *Z*, on the
Amiga, during installation.

Target hardware is a Vampire V1200 (68080) with RTG; 68060 and 68040 builds
are produced from the same sources. AGA is not supported yet.

## Licence

GPLv3, see [LICENSE](LICENSE). The Zod Engine by Nighsoft is GPLv3 and this
port inherits it.

Third party code included here under its own terms:

* `upstream/ZodEgine_Libs/QZod_DnSeparate/SDL_rotozoom.*` — zlib licence
  (Andreas Schiffler)
* `port/third_party/stb_image.h` — public domain / MIT (Sean Barrett)

The game data is **not** covered by that licence and is not distributed.
