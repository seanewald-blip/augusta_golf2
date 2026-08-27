# Build on GitHub

This repository is arranged so GitHub Actions can compile the Wii executable without installing devkitPro locally.

1. Create a new GitHub repository.
2. Upload **the contents of this folder** to the repository root. The `.github` folder must be at the repository root.
3. Open **Actions** in GitHub.
4. Select **Build Augusta Golf for Wii / vWii**.
5. Choose **Run workflow**.
6. When the build finishes, download the artifact named **augusta-golf-vwii-sd-ready**.
7. Extract `augusta_golf_SD_CARD.zip` to the root of a FAT32 SD card.

The resulting card layout is:

```
SD:/apps/augusta_golf/boot.dol
SD:/apps/augusta_golf/meta.xml
SD:/apps/augusta_golf/icon.png
SD:/apps/augusta_golf/data/course_2026.csv
```

Launch it from the Homebrew Channel in Wii or Wii U vWii mode.
