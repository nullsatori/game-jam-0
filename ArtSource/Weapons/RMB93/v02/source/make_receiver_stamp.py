"""Rasterize the small authored maker/batch lettering for the source shader."""
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

root=Path(__file__).resolve().parents[1]
image=Image.new('RGB',(1024,128),'black')
draw=ImageDraw.Draw(image)
font=ImageFont.truetype('C:/Windows/Fonts/bahnschrift.ttf',108)
label='МК / 047'
bounds=draw.textbbox((0,0),label,font=font)
draw.text(((1024-(bounds[2]-bounds[0]))/2-bounds[0],(128-(bounds[3]-bounds[1]))/2-bounds[1]),label,font=font,fill='white')
image.save(root/'material_sources/Receiver_Stamp_M.png')
