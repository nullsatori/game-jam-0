"""Small authored seating profiles shared by exterior inlays."""
from mathutils import Vector
from control_geometry import _normals


def inset_outline(points,distance):
    area=sum(a[0]*b[1]-b[0]*a[1] for a,b in zip(points,points[1:]+points[:1]))
    sign=1 if area>0 else -1
    lines=[]
    for a,b in zip(points,points[1:]+points[:1]):
        a,b=Vector(a),Vector(b);d=(b-a).normalized()
        lines.append((a+Vector((-d.y,d.x))*distance*sign,d))
    result=[]
    for i,(b,v) in enumerate(lines):
        a,u=lines[i-1];delta=b-a;cross=u.x*v.y-u.y*v.x
        result.append(tuple(a+u*((delta.x*v.y-delta.y*v.x)/cross)))
    return result


def seated_panel(mesh,name,outline,side,layers,material,group='Action'):
    """Build a closed inset panel with explicit edge layers, no modifiers.

    layers are (absolute side depth, inset from the supplied outline).
    """
    n=len(outline)
    vertices=[(x,side*depth,z) for depth,inset in layers
              for x,z in inset_outline(outline,inset)]
    faces=[tuple(range(n-1,-1,-1)),tuple(range((len(layers)-1)*n,len(layers)*n))]
    for j in range(len(layers)-1):
        for i in range(n):
            k=(i+1)%n;a=j*n;b=(j+1)*n
            faces.append((a+i,a+k,b+k,b+i))
    ob=mesh(name,vertices,faces,material,group)
    _normals(ob,True)
    del ob['control_custom_normals']
    ob['joinery_custom_normals']=True
    return ob
