# Project Documentation

This directory contains setup guides, architectural notes, and operational workflows for the **QNX 8.0 Resource Manager Driver** project.

## Available Guides

* **[Target Bring-Up Guide](TARGET_BRINGU_GUIDE.md)** — Step-by-step instructions for initializing the QNX SDP 8.0 environment in Windows Command Prompt (`cmd.exe`), cross-compiling with `qcc`, packaging the IFS image with `mkqnximage`, and verifying driver execution on the target.

## Contributing Documentation

* Keep guides concise, actionable, and tested against the current `main` branch structure (`src/`, `bsp/`, `scripts/`).
* Place new markdown guides in `docs/` and link them in the list above.