"""Authored exterior control surfaces; no functional internal mechanisms."""
import math
from mathutils import Vector


def _normals(ob, planar_y=False):
    me=ob.data
    adjacent=[[] for _ in me.vertices]
    for face in me.polygons:
        ids=list(face.vertices)
        face.use_smooth=True
        for i,vi in enumerate(ids):
            p=me.vertices[vi].co
            angle=(me.vertices[ids[i-1]].co-p).angle(me.vertices[ids[(i+1)%len(ids)]].co-p,0)
            adjacent[vi].append((face.index,max(angle,1e-6)))
    result=[]
    for face in me.polygons:
        for li in face.loop_indices:
            if planar_y and abs(face.normal.y)>.9999:
                n=Vector((0,1 if face.normal.y>0 else -1,0))
            else:
                neighbors=adjacent[me.loops[li].vertex_index]
                caps=[me.polygons[fi].normal for fi,w in neighbors
                      if planar_y and abs(me.polygons[fi].normal.y)>.9999
                      and me.polygons[fi].normal.dot(face.normal)>.3]
                if caps:n=Vector((0,1 if caps[0].y>0 else -1,0))
                else:
                    n=sum((me.polygons[fi].normal*w for fi,w in neighbors
                           if me.polygons[fi].normal.dot(face.normal)>.25),Vector())
                    n=n.normalized() if n.length_squared>1e-15 else face.normal
            result.append(tuple(n))
    me.normals_split_custom_set(result)
    ob['control_custom_normals']=True


def _rings(mesh,name,rings,material,closed=False,planar_y=False):
    n=len(rings[0]);count=len(rings)
    vertices=[v for ring in rings for v in ring]
    faces=[]
    if not closed:
        faces=[tuple(range(n-1,-1,-1)),tuple(range((count-1)*n,count*n))]
    for i in range(count if closed else count-1):
        a=i*n;b=((i+1)%count)*n
        for j in range(n):
            k=(j+1)%n
            faces.append((a+j,a+k,b+k,b+j))
    ob=mesh(name,vertices,faces,material,'Body')
    _normals(ob,planar_y)
    return ob


def build_controls(mesh,prism,panel,cyl,rr,parts,steel,edge,black,bpy):
    """Replace five old controls; return six custom-normal editable meshes."""
    handled=[]
    # Smooth D/oval perimeter, with a real broad side plane and softly rolled
    # section edges. Original finger opening and receiver anchors are retained.
    path=rr(-.009,-.0145,.057,.031,.011,7)
    rings=[]
    for i,(x,z) in enumerate(path):
        tangent=(Vector(path[(i+1)%len(path)])-Vector(path[i-1])).normalized()
        radial=Vector((tangent.y,-tangent.x))
        # Two broad side flats and four restrained rolled edge facets.
        section=[(.002,-.0043),(.0013,-.005),(-.0013,-.005),(-.002,-.0043),
                 (-.002,.0043),(-.0013,.005),(.0013,.005),(.002,.0043)]
        rings.append([(x+radial.x*r,y,z+radial.y*r) for r,y in section])
    guard=_rings(mesh,'Trigger_guard',rings,steel,True,True)
    guard['design_note']='Continuous blued-steel guard with broad side flats and rolled contact edges'
    handled.append(guard)

    # Curved trigger blade: narrow at its receiver anchor, a calm broad finger
    # surface through the arc, and a subtly fuller rounded lower tip.
    curve=[(-.0132,.0040,.0012,.0028),(-.0143,.0010,.0013,.0030),
           (-.0154,-.0030,.0014,.0032),(-.0160,-.0075,.0015,.0033),
           (-.0159,-.0120,.0015,.0033),(-.0150,-.0160,.0015,.0032),
           (-.0135,-.0195,.0016,.0031),(-.0117,-.0220,.0017,.0030),
           (-.0102,-.0230,.0013,.0026),(-.0098,-.0231,.0007,.0016)]
    rings=[]
    for i,(x,z,width,hw) in enumerate(curve):
        tangent=(Vector(curve[min(i+1,len(curve)-1)][:2])-Vector(curve[max(0,i-1)][:2])).normalized()
        radial=Vector((-tangent.y,tangent.x))
        section=[(width*.6,-hw),(width,-hw*.65),(width,hw*.65),(width*.6,hw),
                 (-width*.6,hw),(-width,hw*.65),(-width,-hw*.65),(-width*.6,-hw)]
        rings.append([(x+radial.x*r,y,z+radial.y*r) for r,y in section])
    trigger=_rings(mesh,'Trigger',rings,steel,False,True)
    trigger['design_note']='Curved solid trigger blade with a broad finger face and softly terminated lower tip'
    handled.append(trigger)

    # The left lever has a purposeful root-to-tip taper and is visibly seated.
    # Its broad face stays quiet; the dark seam is a single thin base, not trim.
    profile=[(-.0340,.0100),(-.0340,.0167),(-.0315,.0186),(-.0260,.0182),
             (.0155,.0151),(.0200,.0131),(.0213,.0100),(.0195,.0083),
             (-.0280,.0080),(-.0320,.0086)]
    center=Vector((-.006,.013))
    def lever_layers(name,layers,material):
        rings=[]
        for y,scale in layers:
            rings.append([(center.x+(x-center.x)*scale,y,center.y+(z-center.y)*scale)
                          for x,z in profile])
        ob=_rings(mesh,name,rings,material,False,True)
        handled.append(ob)
        return ob
    base=lever_layers('Left_control_seat',[(.01725,1.010),(.01815,1.010),(.01860,.996)],black)
    base['design_note']='Thin recessed seating seam against receiver; left side only'
    lever=lever_layers('Left_control_lever',[(.01845,.985),(.0190,1),(.0214,1),(.0221,.962)],steel)
    lever['design_note']='Tapered control paddle with integrated root and broad restrained thumb-contact plane'

    def seated_pin(name,x,z,r,y0,y1,segments):
        rings=[]
        for y,scale in [(y0,.90),(y0+.00025,1),(y1-.00020,1),(y1,.83)]:
            rings.append([(x+r*scale*math.cos(i*math.tau/segments),y,
                           z+r*scale*math.sin(i*math.tau/segments)) for i in range(segments)])
        ob=_rings(mesh,name,rings,steel,False,True)
        handled.append(ob)
        return ob
    # A quiet flush root pivot and the original small forward pin, without
    # screw slots, bolt heads, or extra decorative hardware.
    seated_pin('Left_control_pivot',-.0300,.0135,.0025,.02175,.02235,16)
    seated_pin('Left_small_pin',.0230,.0068,.0013,.01755,.01830,12)
    return handled
