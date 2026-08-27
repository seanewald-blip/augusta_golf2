# Augusta Golf Wii homebrew — expanded prototype

This package is an **original Wii homebrew golf game**, not a Wii Sports modification and not a Nintendo/Masters disc image. It is designed for a real Wii through the Homebrew Channel.

## Implemented in this revision

- All 18 holes with 2026 tournament par/yardage values.
- Hand-authored routing shapes for all 18 holes, including doglegs and changing fairway widths.
- Course-specific hazard zones: bunkers plus water on the Amen Corner / back-nine water holes.
- Macro elevation profiles and cross-slopes for every hole. These are intentionally marked as estimates until a public/appropriately licensed survey layer is substituted.
- Six airborne clubs plus putter; lie-dependent power and friction.
- 3D launch, gravity, bounce and roll; slopes influence rollout and putting.
- Tee/fairway/rough/green/bunker/water classification.
- Water penalty and last-safe-position restore.
- Wii Remote acceleration controls power and orientation affects club-face angle / dispersion.
- Putting uses the same motion system at lower speed.
- Hole completion, strokes and cumulative relative-to-par score.
- Lightweight GX top-down renderer and a font-free numeric HUD.
- USGS 3DEP terrain ingestion tools.

## Controls

- Hold **B**, make a golf swing, release B: hit shot.
- **Left / Right**: aim.
- **Up / Down**: club.
- **PLUS** after holing out: next hole.
- **HOME**: exit.

## Build for a real Wii

Install devkitPro's `wii-dev` group, then from this directory run:

```sh
make
make package
```

The packaged Homebrew Channel layout is produced under `dist/apps/augusta_golf/`. Copy that folder to the `apps` directory of an SD card.

## Making the topography genuinely survey-based

The playable source currently contains course-specific **estimated** macro slopes so it works without external data. To replace those with real ground elevations:

1. Download a USGS 3DEP DEM covering roughly the course area. Prefer the finest public product available (1 m where available, otherwise 3 m).
2. Crop the GeoTIFF tightly around the playing property.
3. Run `python tools/dem_to_wii.py cropped.tif data/terrain.bin`.
4. Add `terrain.bin` to the Makefile DATA embedding and replace `course_height_yards()` with a georeferenced heightmap sampler. The format is documented in `data/TERRAIN_FORMAT.txt`.

For a quick low-resolution proof, `tools/fetch_usgs_epqs.py` can query USGS EPQS into a resumable CSV grid. A downloaded DEM is strongly preferred for full fidelity.

## Accuracy / licensing boundary

The par and yardage table is current for the 2026 tournament setup. The included route and hazard geometry is an original approximation created for the game; it is **not** a survey and should not be represented as exact Augusta National geometry. Do not copy proprietary Masters, Google, broadcast, or commercial golf-simulator imagery/textures into the package. For exact shapes, substitute a dataset whose license permits redistribution and preserve its attribution.

## What remains before calling it a simulation-grade replica

- ingest and georeference a real 1 m/3 m DEM;
- replace approximate fairway/green/bunker polygons with licensed measured vectors;
- calibrate green contours/stimp, tree collision volumes and actual tee/pin positions;
- add audio, menus, multiple players, save data and a proper 3D follow camera.

The code intentionally stays small enough for Wii-era hardware and avoids texture-heavy rendering.
