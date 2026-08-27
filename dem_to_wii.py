#!/usr/bin/env python3
"""Convert a cropped GeoTIFF DEM to a 256x256 Wii uint16 heightmap.
Requires rasterio + numpy. Samples are centimeters above the local minimum.
Usage: python tools/dem_to_wii.py input.tif data/terrain.bin
"""
import sys,numpy as np,rasterio
from rasterio.enums import Resampling
if len(sys.argv)!=3:raise SystemExit('usage: dem_to_wii.py input.tif output.bin')
with rasterio.open(sys.argv[1]) as src:
 a=src.read(1,out_shape=(256,256),resampling=Resampling.bilinear).astype(np.float32)
 if src.nodata is not None:a[a==src.nodata]=np.nan
 mn=float(np.nanmin(a));mx=float(np.nanmax(a));a=np.nan_to_num(a,nan=mn)
 cm=np.clip(np.rint((a-mn)*100),0,65535).astype('>u2') # big endian suits PowerPC
 cm.tofile(sys.argv[2])
with open(sys.argv[2]+'.txt','w') as f:f.write(f'grid=256x256\nbase_elevation_m={mn:.3f}\nmax_elevation_m={mx:.3f}\nencoding=uint16_be_centimeters_above_base\n')
