"""Rebuild the original vector-style project icon. Requires Pillow (development only)."""
from pathlib import Path
from PIL import Image, ImageDraw
image=Image.new('RGBA',(1024,1024))
draw=ImageDraw.Draw(image)
draw.rounded_rectangle((32,32,992,992),radius=260,fill='#304574')
points=[]
import math
for i in range(301):
    t=i/300
    x=320+370*math.sin(t*math.pi*1.9)*(1-t*0.3)
    y=850-650*t
    points.append((x,y))
draw.line(points,fill='#ffffff',width=28,joint='curve')
draw.line(points[:35],fill='#37b34c',width=55,joint='curve')
image.save(Path(__file__).resolve().parents[1]/'resources'/'whip.ico',sizes=[(16,16),(20,20),(24,24),(32,32),(48,48),(64,64),(128,128),(256,256)])
