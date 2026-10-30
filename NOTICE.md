# Notices and identity

AuroraLauncher is an independent, clean-room C++ launcher project. Its product
name, Windows metadata, user agent, configuration directory, and authentication
application identity are its own.

It is **not** affiliated with, endorsed by, or produced by:

- HMCL or its maintainers;
- Mojang Studios or Microsoft;
- Fabric, Quilt, Forge, or NeoForge;
- any other launcher project.

The archived Java tree under `D:\AuroraLauncher-archive\2026-10-26\HMCL-main` is
retained only as a reference and for license/compliance purposes. The C++ core
does not copy its source code, branding, OAuth client ID, signing certificate,
or update metadata.

The desktop layout follows the interaction patterns of PCL2 CE (frameless blue
title bar, centered tabs, left player/launch column, card-based home), observed
from a locally installed copy. No PCL2 code, XAML, artwork, or other assets are
included. The launcher background (`src/gui/ui/assets/bg.jpg`) was generated for
this project. Grass-block icons and the player head are composed at runtime in
the WebView from textures inside the user's own Minecraft client JAR; no Mojang
textures are redistributed with this repository.

Microsoft login, when enabled, must use an Azure application registration owned
by the AuroraLauncher project. Release binaries should be signed with the
project's own code-signing certificate. See `SIGNING.md`.

Minecraft is a trademark of Mojang Synergies AB. Microsoft, Xbox, and Xbox Live
are trademarks of the Microsoft group of companies. Other names are used only
for identification and belong to their respective owners.

This project is licensed under GPL-3.0-or-later; see `LICENSE`.
