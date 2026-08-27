#!/usr/bin/env python3
import math
clubs=[('Driver',285,12),('3W',240,14),('5I',190,19),('7I',160,23),('9I',130,29),('Wedge',95,38)]
for name,target,deg in clubs:
 a=math.radians(deg);speed=math.sqrt(target*10.72/max(math.sin(2*a),.18))*1.05;vx=speed*math.cos(a);vz=speed*math.sin(a);x=z=0;dt=1/240
 while z>=0:
  x+=vx*dt;z+=vz*dt;vz-=10.72*dt;vx*=0.999**(dt*60)
 print(f'{name:7s} target={target:5.1f} yd  ballistic={x:6.1f} yd  error={x-target:+5.1f}')
