TinyEXR dependency folder

The first build runs tools/fetch_tinyexr.ps1 and downloads the pinned TinyEXR v1.0.8 single-header implementation plus matching miniz.h/miniz.c into this folder.

This avoids depending on TinyEXR's moving development branch and avoids cmd.exe batch-label download helpers.
