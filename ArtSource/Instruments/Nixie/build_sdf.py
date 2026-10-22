"""Exact authored vector paths -> distance atlas; never rewrites the source artwork.

R is signed distance to the directional-width wire edge, in native 256-cell
pixels, encoded .5 + distance/16. G is float-convolved wire-local irradiance.
Positive distance = outside. 512px/cell, 4x4 cells. B stores wire radius / 2.
A (v5) is the distance along the wire from the cathode's lead (its lowest point,
where the pins enter from the base), normalized per glyph: the ignition front
runs along it and the discharge patches drift along it.
"""
import heapq
import ast
import hashlib
import json
from pathlib import Path
import numpy as np
from PIL import Image

def gaussian_filter(image,sigma):
    """Float Gaussian convolution, zero-padded beyond four standard deviations."""
    pad=int(np.ceil(sigma*4))
    padded=np.pad(image,pad)
    fy=np.fft.fftfreq(padded.shape[0])[:,None]
    fx=np.fft.rfftfreq(padded.shape[1])[None,:]
    kernel=np.exp(-2*np.pi*np.pi*sigma*sigma*(fx*fx+fy*fy))
    blurred=np.fft.irfft2(np.fft.rfft2(padded)*kernel,s=padded.shape)
    return np.maximum(blurred[pad:-pad,pad:-pad],0).astype(np.float32)

HERE=Path(__file__).resolve().parent
SOURCE=HERE.parents[1]/'Weapons/RMB93/v02/counter/build_counter.py'
tree=ast.parse(SOURCE.read_text(encoding='utf-8'))
# Read only the glyph dictionary, its normalization and curve sampler. Do not
# execute the original generator's filesystem outputs or change approved art.
selected=[]
for node in tree.body:
    if isinstance(node,ast.Assign) and any(isinstance(t,ast.Name) and t.id=='GLYPHS' for t in node.targets): selected.append(node)
    elif isinstance(node,ast.For) and ast.unparse(node.iter)=='GLYPHS.values()': selected.append(node)
    elif isinstance(node,ast.FunctionDef) and node.name=='sampled': selected.append(node)
scope={'np':np}
exec(compile(ast.Module(body=selected,type_ignores=[]),str(SOURCE),'exec'),scope)
glyphs=scope['GLYPHS']; sampled=scope['sampled']
def geodesic(paths):
    """Subdivided wire polylines and their distance along the wire from the lead.

    One cathode is one connected wire. Crossings (the 4's stem through its
    crossbar), T-joins (the 1's stem under its flag, the 3's lower bowl) and the
    0's closed loop become shared nodes; a piece that touches nothing (the 0's
    slash) is bridged across its gap. Dijkstra runs from the lowest point.
    """
    lines=[[np.asarray(p,dtype=np.float64) for p in sampled(path)] for path in paths]
    def cut(p,a,b):
        d=b-a; L=float(d@d)
        return 0. if L<1e-12 else float(np.clip((p-a)@d/L,0,1))
    extra=[[[] for _ in range(len(line)-1)] for line in lines]  # (t, point) per segment
    segs=[(li,si,line[si],line[si+1]) for li,line in enumerate(lines) for si in range(len(line)-1)]
    for i,(li,si,a,b) in enumerate(segs):
        for lj,sj,c,d in segs[i+1:]:
            if li==lj and abs(si-sj)<=1: continue
            r=b-a; s=d-c; den=r[0]*s[1]-r[1]*s[0]
            if abs(den)>1e-9:
                t=((c-a)[0]*s[1]-(c-a)[1]*s[0])/den; u=((c-a)[0]*r[1]-(c-a)[1]*r[0])/den
                if 0<t<1 and 0<u<1:
                    p=a+t*r; extra[li][si].append((t,p)); extra[lj][sj].append((u,p))
    for li,line in enumerate(lines):          # T-joins: an end resting on another wire
        for end in (line[0],line[-1]):
            for lj,sj,c,d in segs:
                if lj==li: continue
                u=cut(end,c,d); p=c+u*(d-c)
                if 0<u<1 and np.linalg.norm(end-p)<2.5: extra[lj][sj].append((u,p))
    polylines=[]
    for li,line in enumerate(lines):
        pts=[line[0]]
        for si in range(len(line)-1):
            pts+= [p for _,p in sorted(extra[li][si],key=lambda e:e[0])]+[line[si+1]]
        polylines.append(pts)
    nodes=[p for line in polylines for p in line]
    ids=[]; k=0
    for line in polylines:
        ids.append(list(range(k,k+len(line)))); k+=len(line)
    edges={i:[] for i in range(len(nodes))}
    for line in ids:
        for a,b in zip(line,line[1:]):
            w=float(np.linalg.norm(nodes[a]-nodes[b])); edges[a].append((b,w)); edges[b].append((a,w))
    for i in range(len(nodes)):
        for j in range(i+1,len(nodes)):
            if np.linalg.norm(nodes[i]-nodes[j])<2.5: edges[i].append((j,0.)); edges[j].append((i,0.))
    seed=max(range(len(nodes)),key=lambda i:(nodes[i][1],-nodes[i][0]))
    def run():
        dist=[np.inf]*len(nodes); dist[seed]=0.; heap=[(0.,seed)]
        while heap:
            d,i=heapq.heappop(heap)
            if d>dist[i]: continue
            for j,w in edges[i]:
                if d+w<dist[j]: dist[j]=d+w; heapq.heappush(heap,(d+w,j))
        return dist
    dist=run()
    while not all(np.isfinite(dist)):
        i,j=min(((i,j) for i in range(len(nodes)) if not np.isfinite(dist[i]) for j in range(len(nodes)) if np.isfinite(dist[j])),
                key=lambda e:np.linalg.norm(nodes[e[0]]-nodes[e[1]]))
        w=float(np.linalg.norm(nodes[i]-nodes[j])); edges[i].append((j,w)); edges[j].append((i,w))
        dist=run()
    return [(line,[dist[i] for i in index]) for line,index in zip(polylines,ids)]

atlas=np.ones((2048,2048,4),dtype=np.float32)
atlas[:,:,1:]=0
yy,xx=np.mgrid[:512,:512].astype(np.float32)
points=np.stack(((xx+.5)/2,(yy+.5)/2),axis=-1)
bounds=[]
for digit in range(10):
    distance=np.full((512,512),1000,dtype=np.float32)
    nearest_radius=np.zeros((512,512),dtype=np.float32)
    nearest_arc=np.zeros((512,512),dtype=np.float32)
    arcs=geodesic(glyphs[str(digit)])
    longest=max(max(arc) for _,arc in arcs)
    for pts,arc in arcs:
        for (a,b),(sa,sb) in zip(zip(pts,pts[1:]),zip(arc,arc[1:])):
            a=np.asarray(a); b=np.asarray(b); delta=b-a
            length=max(float(np.linalg.norm(delta)),1e-6)
            t=np.clip(np.sum((points-a)*delta,axis=-1)/(length*length),0,1)
            centre=np.linalg.norm(points-(a+t[...,None]*delta),axis=-1)
            radius=2.3*(.7+.6*abs(delta[1])/length)/2
            candidate=centre-radius
            along=np.minimum(sa+t*length,sb+(1-t)*length)/longest
            nearest_radius=np.where(candidate<distance,radius,nearest_radius)
            nearest_arc=np.where(candidate<distance,along,nearest_arc)
            distance=np.minimum(distance,candidate)
    # Chamber irradiance is a sum of local wire contributions, not minimum
    # distance: Gaussian convolution has no nearest-segment/Voronoi ridges.
    coverage=np.clip(.5-distance*2,0,1)
    near=gaussian_filter(coverage,3)     # 1.5 native pixels
    middle=gaussian_filter(coverage,8)   # 4 native pixels
    far=gaussian_filter(coverage,16)     # 8 native pixels, weakest component
    radiance=1-np.exp(-(near*1.8+middle+far*.35))
    # Wide field fades smoothly in the gutter rather than ending as a box.
    edge=np.minimum.reduce([points[:,:,0],256-points[:,:,0],points[:,:,1],256-points[:,:,1]])
    taper=np.clip(edge/16,0,1); taper=taper*taper*(3-2*taper)
    radiance*=taper
    encoded=np.stack((np.clip(.5+distance/16,0,1),radiance,nearest_radius/2,nearest_arc),axis=-1)
    row,col=divmod(digit,4)
    atlas[row*512:(row+1)*512,col*512:(col+1)*512]=encoded
    bounds.append([45,211])
path=HERE/'T_Jam_Nixie_Distance.png'
Image.fromarray(np.uint8(atlas*255+.5),'RGBA').save(path)
saved=np.asarray(Image.open(path))
for digit in range(10,16):
    row,col=divmod(digit,4); cell=saved[row*512:(row+1)*512,col*512:(col+1)*512]
    assert np.all(cell[:,:,0]==255) and not cell[:,:,1:3].any()
report={'source':'../../Weapons/RMB93/v02/counter/build_counter.py','source_sha256':hashlib.sha256(SOURCE.read_bytes()).hexdigest(),'distance_atlas_sha256':hashlib.sha256(path.read_bytes()).hexdigest(),'size':[2048,2048],'encoding':{'R':'.5 + signed edge distance / 16, native 256-cell pixels; positive outside','G':'Float32 Gaussian convolution of exact wire coverage, sigmas 1.5/4/8 native pixels, weights 1.8/1/.35, compressed as 1-exp(-energy)','B':'Nearest authored wire radius / 2; used only for analytic pixel coverage','A':'Distance along the wire from the lead (lowest point), per glyph 0..1; ignition front and drifting discharge patches'},'glyph_centerlines_and_directional_widths_preserved':True,'all_centerline_y_bounds':bounds,'off_and_reserved_distance':8,'off_and_reserved_readback_pass':True,'cell_size':512,'source_cell_size':256,'runtime':'Analytic fwidth edge AA; no blur baked into core'}
(HERE/'sdf_manifest.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
print(json.dumps(report,indent=2))
