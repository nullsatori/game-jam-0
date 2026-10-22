"""Coherent exterior art stock: stamped paired arms and distal contact shoe.

No internal mechanism. Folded presentation coordinates use the existing hinge
at (-.012, 0, .063). No files or scenes are saved by this helper.
"""
import math
from mathutils import Vector


def _surface_normals(ob, flat_axis=None):
    """Preserve main planes while making the authored rolled edges continuous."""
    me=ob.data
    me.update()
    axis=Vector(flat_axis) if flat_axis else None
    neighbors=[[] for _ in me.vertices]
    for face in me.polygons:
        ids=list(face.vertices)
        for i,vi in enumerate(ids):
            p=me.vertices[vi].co
            a=me.vertices[ids[i-1]].co-p
            b=me.vertices[ids[(i+1)%len(ids)]].co-p
            neighbors[vi].append((face.index,max(a.angle(b,0),1e-6)))
    normals=[(0,0,1)]*len(me.loops)
    for face in me.polygons:
        face.use_smooth=True
        planar=axis is not None and abs(face.normal.dot(axis))>.9999
        for li in face.loop_indices:
            adjacent=neighbors[me.loops[li].vertex_index]
            if planar:
                n=axis if face.normal.dot(axis)>0 else -axis
            else:
                caps=[me.polygons[fi].normal for fi,w in adjacent
                      if axis is not None and abs(me.polygons[fi].normal.dot(axis))>.9999
                      and me.polygons[fi].normal.dot(face.normal)>.25]
                if caps:
                    n=axis if caps[0].dot(axis)>0 else -axis
                else:
                    n=Vector((0,0,0))
                    for fi,w in adjacent:
                        candidate=me.polygons[fi].normal
                        if candidate.dot(face.normal)>.35:
                            n+=candidate*w
                    n=n.normalized() if n.length_squared>1e-15 else face.normal
            normals[li]=tuple(n)
    me.normals_split_custom_set(normals)
    ob['stock_custom_normals']=True


def _loft(mesh,name,rings,material,axis=None):
    count=len(rings[0])
    assert all(len(r)==count for r in rings)
    verts=[p for ring in rings for p in ring]
    faces=[tuple(range(count-1,-1,-1)),
           tuple(range((len(rings)-1)*count,len(rings)*count))]
    for j in range(len(rings)-1):
        for i in range(count):
            k=(i+1)%count
            faces.append((j*count+i,j*count+k,(j+1)*count+k,(j+1)*count+i))
    ob=mesh(name,verts,faces,material,'Stock')
    _surface_normals(ob,axis)
    return ob


def build_stock(mesh,prism,parts,steel,composite,rubber,bpy):
    """Return four custom-normal objects; exclude them from generic normals.

    Replaces old two rails, front bridge and near-hinge heel pad. The existing
    four Body hinge cylinders remain in build_model.py. All created meshes use
    assembly Stock and share the original game-space origin.
    """
    handled=[]

    # Tapered C-section stampings instead of long flat bars. The shallow return
    # edges face inward, leaving the middle open above the loading lid. A broad
    # hinge root transitions continuously into a slimmer distal arm.
    sections=[(-.018,.0032,.063),(-.014,.0053,.063),(-.006,.0058,.063),
              (.014,.0057,.0627),(.065,.0052,.0624),(.180,.0046,.0621),
              (.290,.0038,.0619),(.345,.0035,.0618),(.380,.0038,.0618),
              (.400,.0043,.0620),(.4035,.0045,.0620),(.4070,.0045,.0620)]
    for side in (-1,1):
        rings=[]
        for x,h,zc in sections:
            cross=[(.0010,h-.0003),(.0007,h),(-.0020,h),
                   (-.0020,h-.0012),(-.0006,h-.0012),
                   (-.0006,-h+.0012),(-.0020,-h+.0012),
                   (-.0020,-h),(.0007,-h),(.0010,-h+.0003)]
            flare=max(0,min(1,(x-.345)/(.411-.345)))
            center_y=.0198+.0064*(flare*flare*(3-2*flare))
            rings.append([(x,side*(center_y+y),zc+z) for y,z in cross])
        arm=_loft(mesh,'Folded_stock_rail_'+str(side),rings,steel,(0,1,0))
        arm['design_note']='Tapered stamped arm with inward return edges and integrated hinge root'
        handled.append(arm)

    # A true open frame shoulder plate, perpendicular to the arms. In the
    # folded presentation its +X contact surface faces forward; turning this
    # rigid assembly -195 degrees around the existing hinge faces it rearward.
    # The opening is real topology and clears the tube/sleeve envelope.
    def horseshoe(shrink,expand):
        ow=.0340-shrink; iw=.0215+expand
        top=.0684-shrink; inner_top=.0603+expand
        oc=.006; ic=.0025; toe=-.04075
        # One continuous U-shaped section, including rounded free contact
        # ends. There is deliberately no lower crossbar to trap the tubes.
        pts=[(ow,toe),(ow,top-oc),(ow-oc*.293,top-oc*.293),
             (ow-oc,top),(-ow+oc,top),(-ow+oc*.293,top-oc*.293),
             (-ow,top-oc),(-ow,toe)]
        radius=(ow-iw)/2; center=-(ow+iw)/2
        for angle in (225,270,315,360):
            a=math.radians(angle)
            pts.append((center+radius*math.cos(a),toe+radius*math.sin(a)))
        pts.extend([(-iw,inner_top-ic),(-iw+ic*.293,inner_top-ic*.293),
                    (-iw+ic,inner_top),(iw-ic,inner_top),
                    (iw-ic*.293,inner_top-ic*.293),(iw,inner_top-ic),(iw,toe)])
        center=(ow+iw)/2
        for angle in (225,270,315):
            a=math.radians(angle)
            pts.append((center+radius*math.cos(a),toe+radius*math.sin(a)))
        return pts

    def frame(name,layers,material):
        # Layer = x, exterior inset, aperture expansion. The concave cap
        # follows the horseshoe section, so the lower exit remains open.
        rings=[]
        for layer,(x,shrink,expand) in enumerate(layers):
            ring=[]
            for y,z in horseshoe(shrink,expand):
                # The upper corner is a pressed seating shoulder, blended
                # into the backing rather than a separate welded-on block.
                height=max(0,min(1,(z-.054)/.010))
                side=max(0,min(1,(abs(y)-.018)/.008))
                seat=height*height*(3-2*height)*side*side*(3-2*side)
                rear_relief=.0011*seat*(1 if layer==1 else 0)
                ring.append((x-rear_relief,y,z))
            rings.append(ring)
        ob=_loft(mesh,name,rings,material,(1,0,0))
        handled.append(ob)
        return ob

    plate=frame('Folded_stock_front_bridge',[
        (.4032,.0010,.00065),(.4050,0,0),
        (.4138,0,0),(.4146,.00045,.00035)],steel)
    plate['design_note']='Rolled steel horseshoe backing with integral pressed upper-corner seating shoulders, terminated arm tongues and exposed pad ledge'

    # Molded contact pad: sweep a flattened rounded section along the U.
    # The front is a real crowned surface, not a flat ngon. Crosswise channels
    # are integrated depth changes in that surface, never separate strips.
    straight_z={-.0405,-.034,.044,.0519}
    for z in (-.020,.005,.030):
        straight_z.update((z-.0012,z-.0006,z,z+.0006,z+.0012))
    path=[]
    # Rounded free toes taper into the full-width leg over four sections.
    for z,scale in [(-.0460,.12),(-.0456,.34),(-.0450,.55),(-.0430,.90)]:
        path.append((-.0277,z,.0060,scale))
    path.extend((-.0277,z,.0060,1) for z in sorted(straight_z))
    for angle,width in [(150,.0052),(135,.0046),(120,.0040),(105,.0035),(90,.0032)]:
        a=math.radians(angle)
        path.append((-.015+.0127*math.cos(a),.0519+.0127*math.sin(a),width,1))
    path.extend((y,.0646,.0032,1) for y in (-.0075,0,.0075,.015))
    for angle,width in [(75,.0035),(60,.0040),(45,.0046),(30,.0052),(0,.0060)]:
        a=math.radians(angle)
        path.append((.015+.0127*math.cos(a),.0519+.0127*math.sin(a),width,1))
    path.extend((.0277,z,.0060,1) for z in sorted(straight_z,reverse=True) if z<.0519)
    path.extend((.0277,z,.0060,scale) for z,scale in [(-.0430,.90),(-.0450,.55),(-.0456,.34),(-.0460,.12)])
    def channel(z):
        for center in (-.020,.005,.030):
            delta=z-center
            if abs(delta)<.0012:
                phase=math.pi*delta/.0012
                return .00025*(1+math.cos(phase)), -.00025*math.pi/.0012*math.sin(phase)
        return 0,0

    rings=[]
    for i,(y,z,width,scale) in enumerate(path):
        previous=Vector(path[max(0,i-1)][:2]);following=Vector(path[min(len(path)-1,i+1)][:2])
        tangent=(following-previous).normalized();normal=Vector((-tangent.y,tangent.x))
        # A restrained concave shoulder fit, with a subtle outward crown at
        # the rounded toes and bridge. Grooves affect only the contact face.
        fit=.00055*(abs(z-.008)/.055)**2
        groove=channel(z)[0] if abs(y)>.025 else 0
        section=[(0,-.72),(.0010,-1),(.0048,-1),(.0081,-.84),
                 (.0100,-.43),(.0105,0),(.0100,.43),(.0081,.84),
                 (.0048,1),(.0010,1),(0,.72)]
        ring=[]
        for depth,lateral in section:
            face_weight=max(0,min(1,(depth-.0048)/.0052))
            dx=(depth-.0048)*scale+.0048+(fit-groove)*face_weight
            q=Vector((y,z))+normal*(lateral*width*scale)
            ring.append((.41435+dx,q.x,q.y))
        rings.append(ring)
    pad=_loft(mesh,'Folded_stock_heel_pad',rings,rubber)
    # The narrow channels must not smear their face-normal slope across the
    # long neighboring quads. Analytic longitudinal derivatives give a zero
    # slope at each recess boundary and center; only the narrow ramp tilts.
    me=pad.data
    normals=[tuple(n.vector) for n in me.corner_normals]
    count=len(section)
    for loop in me.loops:
        ri,j=divmod(loop.vertex_index,count)
        y,z,width,scale=path[ri]
        if abs(abs(y)-.0277)<1e-7 and scale==1 and -.0405<z<.0519:
            depth,lateral=section[j]
            weight=max(0,min(1,(depth-.0048)/.0052))
            dxdz=(.0011*(z-.008)/(.055*.055)-channel(z)[1])*weight
            tangent=Vector((dxdz,0,1))
            cross=Vector(rings[ri][(j+1)%count])-Vector(rings[ri][(j-1)%count])
            n=cross.cross(tangent).normalized()
            if n.dot(Vector(normals[loop.index]))<0:n=-n
            normals[loop.index]=tuple(n)
    me.normals_split_custom_set(normals)
    pad['design_note']='Molded rubber shoulder pad with crowned contact face, rounded free toes, three shallow channels per leg and steel seating seam'
    handled.append(pad)
    assert all(o.get('assembly')=='Stock' for o in handled)
    assert max(v.co.z for o in handled for v in o.data.vertices)<.069
    return handled
