#!/usr/bin/env python3
"""Fetch a rectangular USGS 3DEP elevation sample grid through EPQS.
This uses only Python stdlib and stores a resumable CSV cache. It is intentionally
low-rate. For a production terrain build, a downloaded 1 m/3 m DEM is much faster.

Example:
  python tools/fetch_usgs_epqs.py --west -82.033 --east -82.006 --south 33.489 --north 33.518 --size 33 --out data/usgs_grid.csv
"""
import argparse,csv,json,time,urllib.parse,urllib.request
p=argparse.ArgumentParser();p.add_argument('--west',type=float,required=True);p.add_argument('--east',type=float,required=True);p.add_argument('--south',type=float,required=True);p.add_argument('--north',type=float,required=True);p.add_argument('--size',type=int,default=33);p.add_argument('--out',required=True);p.add_argument('--delay',type=float,default=.12);a=p.parse_args()
seen={}
try:
 with open(a.out,newline='') as f:
  for r in csv.DictReader(f):seen[(int(r['ix']),int(r['iy']))]=r
except FileNotFoundError:pass
rows=list(seen.values())
for iy in range(a.size):
 lat=a.south+(a.north-a.south)*iy/(a.size-1)
 for ix in range(a.size):
  if (ix,iy) in seen:continue
  lon=a.west+(a.east-a.west)*ix/(a.size-1)
  q=urllib.parse.urlencode({'x':lon,'y':lat,'wkid':4326,'units':'Meters','includeDate':'false'})
  with urllib.request.urlopen('https://epqs.nationalmap.gov/v1/json?'+q,timeout=30) as resp:obj=json.load(resp)
  val=float(obj['value']);rows.append({'ix':ix,'iy':iy,'lon':lon,'lat':lat,'elevation_m':val});seen[(ix,iy)]=rows[-1]
  with open(a.out,'w',newline='') as f:
   w=csv.DictWriter(f,fieldnames=['ix','iy','lon','lat','elevation_m']);w.writeheader();w.writerows(sorted(rows,key=lambda r:(int(r['iy']),int(r['ix']))))
  print(ix,iy,lon,lat,val);time.sleep(a.delay)
