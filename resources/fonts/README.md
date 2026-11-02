# Bundled fonts

Unmodified upstream desktop fonts; no OS-installed font is required.

| Family | Official source | Bundled files |
|---|---|---|
| Inter 4.1 | https://github.com/rsms/inter/releases/tag/v4.1 | InterVariable.ttf and static Regular, Medium, SemiBold, Bold from extras/ttf |
| JetBrains Mono | https://github.com/JetBrains/JetBrainsMono/tree/19371302b95d218af43299bce79ddbddd0bc364d/fonts/ttf | Regular, Bold, Italic |

Copyright (c) 2016 The Inter Project Authors (https://github.com/rsms/inter).
Copyright 2020 The JetBrains Mono Project Authors (https://github.com/JetBrains/JetBrainsMono).
Both families use SIL Open Font License 1.1. See `Inter-OFL.txt` and
`JetBrainsMono-OFL.txt`; distribute these notices with the application.
SF Pro, SF Mono, Segoe UI and Consolas are **not** bundled.

JUCE 8.0.12 exposes OpenType features but no public variable weight-axis
selection. InterVariable is embedded and load-tested; actual role weights use
the upstream static faces, not synthesized bold or an unverified axis request.
The code font disables `liga` and `calt`; italic uses the actual italic asset.
Font data is embedded in the binary; runtime has no download or font install.
