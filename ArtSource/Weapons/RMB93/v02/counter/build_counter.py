"""Authored wire numerals and optical counter textures. No external imagery.

Run with the bundled Python (Pillow and NumPy). Output is deliberately native
model texture artwork, not a generated concept or a claim of UE acceptance.
"""
from pathlib import Path
import json
import hashlib
import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageFont

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT / "textures"
HERE = Path(__file__).resolve().parent
OUT.mkdir(parents=True, exist_ok=True)
S = 4

# Revision 5: Matrix-inspired instrument numerals with long diagonals, cropped
# terminals and unequal wire weights. The 4 has a clean crossbar end; compact 3
# retains its accepted aspect ratio. Supersedes the old 4-6px core guidance.
# Coordinates are relative to one 256px atlas cell.
GLYPHS = {
    "0": [[("M",102,45), ("L",141,45), ("L",177,81), ("L",177,181), ("L",147,211), ("L",112,211), ("L",79,178), ("L",79,76), ("L",102,45)], [("M",111,152), ("L",145,102)]],
    "1": [[("M",108,73), ("L",128,53), ("L",140,53)], [("M",128,53), ("L",128,209)], [("M",110,209), ("L",146,209), ("L",151,204)]],
    "2": [[("M",79,83), ("L",97,52), ("Q",101,45,109,45), ("L",147,45), ("L",178,76), ("L",178,99), ("Q",178,106,171,113), ("L",83,196), ("L",83,208), ("L",172,208), ("L",182,197)]],
    "3": [[("M",76,72), ("L",94,54), ("L",147,54), ("L",183,84), ("L",183,106), ("L",153,127), ("L",117,127), ("L",112,132)], [("M",153,127), ("L",184,147), ("L",184,177), ("L",157,203), ("L",100,203), ("L",76,183)]],
    "4": [[("M",119,45), ("L",78,151), ("Q",75,159,82,160), ("L",182,160)], [("M",151,76), ("L",151,204), ("Q",151,211,158,211), ("L",166,211)]],
    "5": [[("M",180,45), ("L",102,45), ("L",83,118), ("L",145,118), ("L",179,147), ("L",179,182), ("L",150,211), ("L",104,211), ("L",80,191), ("L",80,181)]],
    "6": [[("M",170,52), ("L",164,45), ("L",137,45), ("L",83,103), ("L",83,182), ("L",111,211), ("L",147,211), ("L",178,180), ("L",178,149), ("L",150,120), ("L",109,120), ("L",83,146)]],
    "7": [[("M",80,60), ("L",88,47), ("L",179,47), ("L",110,211), ("L",99,211)]],
    "8": [[("M",106,45), ("L",144,45), ("L",174,75), ("L",174,98), ("L",87,155), ("L",80,165), ("L",80,182), ("L",108,211), ("L",146,211), ("L",178,180), ("L",178,164), ("L",88,100), ("L",88,74), ("L",106,45)]],
    "9": [[("M",178,103), ("L",151,131), ("L",109,131), ("L",80,102), ("L",80,77), ("L",109,45), ("L",149,45), ("L",178,74), ("L",178,151), ("L",125,211), ("L",88,211), ("L",81,204)]],
}

# Shared cap line and baseline. Scale both axes equally so the compact 3 does
# not become stretched; keep directional wire widths independent of glyph size.
for paths in GLYPHS.values():
    ys=[cmd[j] for path in paths for cmd in path for j in range(2,len(cmd),2)]
    top,bottom=min(ys),max(ys)
    scale=166/(bottom-top)
    for path in paths:
        for i,cmd in enumerate(path):
            path[i]=(cmd[0],*(round(128+(v-128)*scale if j%2 else 45+(v-top)*scale,6)
                              for j,v in enumerate(cmd[1:],1)))
    normalized_y=[cmd[j] for path in paths for cmd in path for j in range(2,len(cmd),2)]
    assert min(normalized_y)==45 and max(normalized_y)==211

# Compact native vector centreline source for manual font edits. The raster
# generator above/below remains authoritative for directional stroke weights.
svg=['<svg xmlns="http://www.w3.org/2000/svg" width="1280" height="600" viewBox="0 0 1280 600">',
     '<title>RMB93 instrument glyphs revision 5 — equal-height editable centerlines</title>',
     '<desc>Each labelled group contains original vector glyph paths. Constant stroke here shows editable centerlines; production width varies from 1.61 to 2.99 pixels by segment direction in build_counter.py.</desc>',
     '<rect width="1280" height="600" fill="#111816"/>']
for i in range(10):
    x0,y0=(i%5)*256,(i//5)*300
    svg.append(f'<g id="glyph-{i}" transform="translate({x0},{y0})">')
    svg.append(f'<text x="20" y="22" fill="#929b90" font-family="monospace" font-size="13">{i}</text>')
    for j,path in enumerate(GLYPHS[str(i)]):
        d=' '.join(cmd[0]+' '+' '.join(str(v) for v in cmd[1:]) for cmd in path)
        svg.append(f'<path id="glyph-{i}-wire-{j}" d="{d}" transform="translate(0,34)" fill="none" stroke="#e8a354" stroke-width="2.3" stroke-linecap="round" stroke-linejoin="round"/>')
    svg.append('</g>')
svg.append('</svg>')
(HERE/'Glyphs_Authored_Paths.svg').write_text('\n'.join(svg),encoding='utf-8')

def sampled(commands):
    result=[]
    p=np.zeros(2)
    for cmd in commands:
        if cmd[0] in ("M", "L"):
            p=np.array(cmd[1:], dtype=float)
            result.append(tuple(p))
        elif cmd[0] == "C":
            a,b,c=np.array(cmd[1:3]),np.array(cmd[3:5]),np.array(cmd[5:7])
            for t in np.linspace(0,1,35)[1:]:
                q=(1-t)**3*p+3*(1-t)**2*t*a+3*(1-t)*t*t*b+t**3*c
                result.append(tuple(q))
            p=c
        elif cmd[0] == "Q":
            a,b=np.array(cmd[1:3]),np.array(cmd[3:5])
            for t in np.linspace(0,1,15)[1:]:
                q=(1-t)**2*p+2*(1-t)*t*a+t*t*b
                result.append(tuple(q))
            p=b
    return result

def wire(digit, width=2.3):
    im=Image.new("L",(256*S,256*S))
    d=ImageDraw.Draw(im)
    for commands in GLYPHS[digit]:
        pts=[(x*S,y*S) for x,y in sampled(commands)]
        # 1.61px horizontal / 2.99px vertical at native cell resolution.
        # The intermediate diagonal and curved weights follow wire direction,
        # not random noise. Short arcs make deliberate soft chamfer terminals.
        for (ax,ay),(bx,by) in zip(pts,pts[1:]):
            length=max(.00001,np.hypot(bx-ax,by-ay))
            weight=.7+.6*abs(by-ay)/length
            radius=width*S*weight/2
            d.line(((ax,ay),(bx,by)),fill=255,width=max(1,round(radius*2)))
            for xx,yy in ((ax,ay),(bx,by)):
                d.ellipse((xx-radius,yy-radius,xx+radius,yy+radius),fill=255)
    return im.resize((256,256),Image.Resampling.LANCZOS)

def farr(im):
    return np.asarray(im,dtype=np.float32)/255

def rgb_save(name, arr):
    p=OUT/(name+".png")
    Image.fromarray(np.uint8(np.clip(arr,0,1)*255+.5),"RGB").save(p)
    return p

atlas=np.zeros((1024,1024,3),np.float32)
cells=[]
for i in range(10):
    core=wire(str(i))
    discharge=wire(str(i),4.5).filter(ImageFilter.GaussianBlur(.7))
    halo=wire(str(i),3.5).filter(ImageFilter.GaussianBlur(4.2))
    # Mild organic intensity modulation follows the same physical wire. No
    # individual bright particles: the gas envelope remains continuous.
    yy,xx=np.mgrid[:256,:256]
    modulation=.94+.06*np.sin(xx*.047+yy*.031)**2
    cell=np.stack((farr(core)*modulation*.84,farr(discharge)*.65,farr(halo)*.70),-1)
    cell[:16]=0; cell[240:]=0; cell[:,:16]=0; cell[:,240:]=0
    row,col=divmod(i,4)
    atlas[row*256:(row+1)*256,col*256:(col+1)*256]=cell
    cells.append(cell)
rgb_save("T_Jam_RMB93_DigitAtlas_M", atlas)

N=512
y,x=np.mgrid[:N,:N].astype(np.float32)
u=(x+.5)/N; v=(y+.5)/N
cx=(u-.5)/.45; cy=(v-.5)/.45
distance=np.sqrt(cx*cx+cy*cy)
ap=Image.new("L",(N,N))
ImageDraw.Draw(ap).rounded_rectangle((22,22,489,489),radius=46,fill=255)
aperture=farr(ap.filter(ImageFilter.GaussianBlur(1.1)))

# Cathodes sit on several planes. Their subtle offsets provide dark, nested
# outlines rather than a flat collection of fully illuminated numerals.
ghost=np.zeros((N,N),np.float32)
for i,digit in enumerate(("0","8","2","5","9","6","3","7","1")):
    g=wire(digit,.95).crop((16,16,240,240)).resize((N,N),Image.Resampling.LANCZOS)
    a=farr(g)
    a=np.roll(a, round((i-4)*2.7), axis=1)
    a=np.roll(a, (i-4), axis=0)
    ghost=np.maximum(ghost,a*(.45+i*.035))
ghost*=aperture

rng=np.random.default_rng(93)
low=Image.fromarray(np.uint8(rng.uniform(0,255,(32,32))),"L").resize((N,N),Image.Resampling.BICUBIC).filter(ImageFilter.GaussianBlur(8))
noise=farr(low)
fine=rng.normal(0,.0007,(N,N))
edge=np.clip((distance-.70)*1.8,0,1)
dirt=(.03+edge*.26+noise*.09)*aperture

# Anode wire grid, 1px, subdued and behind the layered cathodes.
grid=(np.minimum(np.mod(x+y,31),31-np.mod(x+y,31))<.65) | (np.minimum(np.mod(x-y,31),31-np.mod(x-y,31))<.65)
grid=grid.astype(np.float32)*aperture
base=np.empty((N,N,3),np.float32)
for c,value in enumerate((.048,.057,.052)):
    base[:,:,c]=value+noise*.007+fine
base+=grid[:,:,None]*np.array((.012,.014,.012))
base+=ghost[:,:,None]*np.array((.083,.086,.067))
# Cathode leads and support wires stay nonemissive. They make the numeral
# visibly sit inside a small instrument rather than float on plain black.
support=(np.abs(x-114)<.65)|(np.abs(x-399)<.65)
support=support & (y>58) & (y<451)
leads=np.zeros_like(x,dtype=bool)
for lead_x in np.linspace(147,367,10):
    leads |= (np.abs(x-lead_x)<.6) & (y>441) & (y<471)
base+=(support|leads)[:,:,None]*np.array((.024,.029,.024))*aperture[:,:,None]
base*=1-edge[:,:,None]*.32
# Soft in-glass edge reflection: broad and dim, no bright baked hotspots.
reflection=np.exp(-((u-.19-.025*np.sin(v*3))/.018)**2)*np.exp(-((v-.4)/.30)**8)*.016
base+=reflection[:,:,None]*np.array((.65,.75,.65))
base=base*aperture[:,:,None]+(1-aperture[:,:,None])*np.array((.018,.023,.022))
rgb_save("T_Jam_RMB93_Counter_BC",base)

# Smooth, shallow convex lens; low amplitude is important for believable size.
# Tangent-space DX convention is red=-dH/dU, green=+dH/dV, blue outward.
height=.0015*np.maximum(0,1-cx*cx)*np.maximum(0,1-cy*cy)
dy,dx=np.gradient(height,1/N,1/N)
normal=np.stack((-dx,dy,np.ones_like(dx)),-1)
normal/=np.linalg.norm(normal,axis=-1,keepdims=True)
rgb_save("T_Jam_RMB93_Counter_N",normal*.5+.5)
ao=np.clip(.98-edge*.32,0,1)*aperture+(1-aperture)*.32
rough=np.clip(.23+dirt*.32+noise*.015,0,1)*aperture+(1-aperture)*.66
metal=np.zeros_like(ao)
rgb_save("T_Jam_RMB93_Counter_ORM",np.stack((ao,rough,metal),-1))
rgb_save("T_Jam_RMB93_Counter_Masks",np.stack((aperture,ghost,dirt),-1))

# Static glTF preview maps. Sample the quantized production atlas with the
# same texel-center inset as the UE contract, not an approximate whole-cell
# crop. These derivatives live outside the nine production UE texture maps.
stored_atlas=np.asarray(Image.open(OUT/"T_Jam_RMB93_DigitAtlas_M.png"),dtype=np.float32)/255
digit=4; row,col=divmod(digit,4)
sx=col*256+16+u*223; sy=row*256+16+v*223
ix=np.floor(sx).astype(int); iy=np.floor(sy).astype(int)
fx=(sx-ix)[:,:,None]; fy=(sy-iy)[:,:,None]
sample=(stored_atlas[iy,ix]*(1-fx)*(1-fy)
       +stored_atlas[iy,ix+1]*fx*(1-fy)
       +stored_atlas[iy+1,ix]*(1-fx)*fy
       +stored_atlas[iy+1,ix+1]*fx*fy)
stored_ap=np.asarray(Image.open(OUT/"T_Jam_RMB93_Counter_Masks.png"),dtype=np.float32)[:,:,0]/255
normalization=1.67 # max possible R + 0.55G + 0.12B contribution
emission=(sample[:,:,0,None]*np.array((1,.32,.055))
         +sample[:,:,1,None]*np.array((1,.10,.006))*.55
         +sample[:,:,2,None]*np.array((1,.045,.003))*.12)
emission=emission*stored_ap[:,:,None]/normalization
assert emission.max()<=1 and emission.min()>=0
Image.fromarray(np.uint8(np.clip(emission,0,1)*255+.5),"RGB").save(HERE/"Counter_Emission4.png")
srgb_emission=np.where(emission<=.0031308,emission*12.92,1.055*emission**(1/2.4)-.055)
Image.fromarray(np.uint8(np.clip(srgb_emission,0,1)*255+.5),"RGB").save(HERE/"Counter_Emission4_sRGB.png")
ogl=np.array(Image.open(OUT/"T_Jam_RMB93_Counter_N.png"))
ogl[:,:,1]=255-ogl[:,:,1]
Image.fromarray(ogl,"RGB").save(HERE/"Counter_N_OpenGL.png")

# Diagnostic contact sheet uses the exact atlas channels and documented linear
# color formula, then a display-only exposure curve. Not an engine screenshot.
sheet=Image.new("RGB",(6*256,2*288),(16,20,19))
draw=ImageDraw.Draw(sheet)
smallbase=np.asarray(Image.fromarray(np.uint8(np.clip(base,0,1)*255)).resize((256,256)),dtype=np.float32)/255
for i in range(11):
    if i<10:
        c=cells[i][16:240,16:240]
        c=np.asarray(Image.fromarray(np.uint8(c*255)).resize((256,256),Image.Resampling.BILINEAR),dtype=np.float32)/255
        linear=(c[:,:,0,None]*np.array((1,.32,.055))+c[:,:,1,None]*np.array((1,.10,.006))*.55+c[:,:,2,None]*np.array((1,.045,.003))*.12)*5
    else: linear=np.zeros((256,256,3))
    linear+=smallbase**2.2
    rendered=np.clip(linear/(linear+1),0,1)**(1/2.2)
    tile=Image.fromarray(np.uint8(rendered*255),"RGB")
    col,row=i%6,i//6
    sheet.paste(tile,(col*256,row*288+24))
    draw.text((col*256+10,row*288+7),"OFF / 10" if i==10 else str(i),fill=(170,169,151))
sheet.save(HERE/"Counter_Digit_States.png")

checks=[]
files=sorted(OUT.glob("*Counter*.png"))+[OUT/"T_Jam_RMB93_DigitAtlas_M.png"]
for p in files:
    im=Image.open(p)
    expected=(1024,1024) if "DigitAtlas" in p.name else (512,512)
    assert im.mode=="RGB" and im.size==expected, (p,im.mode,im.size)
    checks.append({"file":p.name,"size":list(im.size),"mode":im.mode,"sha256":hashlib.sha256(p.read_bytes()).hexdigest()})
saved=np.asarray(Image.open(OUT/"T_Jam_RMB93_DigitAtlas_M.png"))
core_bounds=[]
for i in range(10):
    core=saved[(i//4)*256:(i//4+1)*256,(i%4)*256:(i%4+1)*256,0]
    y,x=np.where(core>80)
    core_bounds.append([int(y.min()),int(y.max())])
# Same vector height; round caps/antialiasing may add up to one pixel per edge.
assert max(b[0] for b in core_bounds)-min(b[0] for b in core_bounds)<=1
assert max(b[1] for b in core_bounds)-min(b[1] for b in core_bounds)<=1
for i in range(16):
    r,c=divmod(i,4); a=saved[r*256:(r+1)*256,c*256:(c+1)*256]
    assert not a[:16].any() and not a[240:].any() and not a[:,:16].any() and not a[:,240:].any()
    assert bool(a.any())==(i<10)
assert len({hashlib.sha256(saved[(i//4)*256:(i//4+1)*256,(i%4)*256:(i%4+1)*256].tobytes()).hexdigest() for i in range(10)})==10
assert max(np.asarray(Image.open(OUT/"T_Jam_RMB93_Counter_BC.png")).max(axis=(0,1)))<60
mip_checks=[]
for level in range(4):
    size=1024//(2**level); cellsize=size//4
    mip=np.asarray(Image.fromarray(saved).resize((size,size),Image.Resampling.BOX))
    cores=[mip[(i//4)*cellsize:(i//4+1)*cellsize,(i%4)*cellsize:(i%4+1)*cellsize,0] for i in range(16)]
    assert all(a.max()>25 for a in cores[:10])
    assert all(not a.any() for a in cores[10:])
    assert len({hashlib.sha256(a.tobytes()).hexdigest() for a in cores[:10]})==10
    mip_checks.append({"level":level,"unique_active_glyphs":10,"off_and_reserved_black":True,"lowest_core_peak":int(min(a.max() for a in cores[:10]))})
report={"status":"texture_contract_checks_pass","art_revision":5,"revision_authority":"Explicit user request for a Matrix-inspired full numeric alphabet following approved 4, with artistic stable 1 and compact 3","approved_four_preserved":False,"four_revision":"User requested removal of the upturned crossbar endpoint; all other segments retained","shared_centerline_y_bounds":[45,211],"glyph_height_rule":"Uniform XY scaling to common cap line and baseline; aspect ratios and wire widths preserved","core_width_pixels_at_256_cell":[1.61,2.99],"core_width_rule":"2.3 * (0.7 + 0.6 * abs(segment_dy) / segment_length)","supersedes":"v01 initial core 4-6px and discharge 10-16px art guidance only; runtime acceptance remains pending","readability_note":"At 30px glyph height, unfiltered geometric core is approximately 0.29-0.54px. The orange envelope improves apparent width; this does not pass the old 2px core requirement. Validate actual first-person AA or increase presentation size.","source":"hand_authored_vector_paths_and_native_optical_material_fields","atlas_origin":"top_left","off_index":10,"reserved_indices":[11,12,13,14,15],"black_gutter_pixels":16,"glyphs_unique":10,"normal_convention":"DirectX_Y_minus","srgb":["T_Jam_RMB93_Counter_BC"],"files":checks,"mip_checks":mip_checks,"static_preview":{"emission":"Counter_Emission4.png","encoding":"linear RGB; do not apply sRGB decoding","normalization_factor":normalization,"material_emissive_strength_for_contract_intensity_5":5*normalization,"normal":"Counter_N_OpenGL.png","atlas_sampling":"exact contract texel center formula, bilinear, aperture applied"},"unreal_imported":False,"runtime_acceptance":False,"preview":"Counter_Digit_States.png is a CPU diagnostic, not an engine screenshot"}
report['raster_core_y_bounds_at_threshold_80']=core_bounds
(HERE/"validation.json").write_text(json.dumps(report,indent=2),encoding="utf-8")
print(json.dumps({"status":report["status"],"files":[x["file"] for x in checks]}))
