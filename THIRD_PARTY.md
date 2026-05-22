# Third-Party Libraries

This project uses the following third-party header-only libraries:

## stb_image.h

- **Author**: Sean Barrett (nothings)
- **Version**: v2.30
- **License**: Public Domain / MIT (dual-licensed)
- **Source**: https://github.com/nothings/stb
- **Download**: https://raw.githubusercontent.com/nothings/stb/master/stb_image.h
- **Usage**: Image loading for texture decoding (PNG, JPEG, BMP, etc.)

## OBJ_Loader.h

- **Author**: Robert Smith (Bly7)
- **License**: MIT
- **Source**: https://github.com/Bly7/OBJ-Loader
- **Download**: https://raw.githubusercontent.com/Bly7/OBJ-Loader/master/Source/OBJ_Loader.h
- **Usage**: OBJ/MTL file parsing for 3D model loading

---

## How to add license headers

Both libraries are header-only single files placed in `include/`.
Their license text is embedded at the top of each file.
No additional configuration or linking is required.

For academic submissions, cite as:

```
[1] Sean Barrett, "stb_image.h - Single-file public domain image loader",
    https://github.com/nothings/stb, accessed 2026.

[2] Robert Smith, "OBJ-Loader - A single header OBJ model loader",
    https://github.com/Bly7/OBJ-Loader, MIT License, accessed 2026.
```
