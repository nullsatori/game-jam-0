"""Low-poly exterior prop sleeve, with actual open holes and wall thickness."""
import math
import bmesh
from mathutils import Vector

def build_sleeve(mesh, material):
    vertices=[];faces=[]
    def point(x,a,r):return (x,r*math.sin(a),.0075+r*math.cos(a))
    def patch(points):
        base=len(vertices);vertices.extend(points);faces.append(tuple(range(base,base+len(points))))
    start,end=.338,.512
    nx,na=6,16
    outer_r,inner_r=.0131,.0118
    lip=.00012
    wall_r=outer_r-lip
    hole_x,hole_angle=.0028,.160
    inner_x,inner_angle=hole_x-lip,hole_angle-lip/outer_r
    hole_centres=[]
    for i in range(nx):
        x0=start+(end-start)*i/nx;x1=start+(end-start)*(i+1)/nx
        for j in range(na):
            a0=(j-.5)*2*math.pi/na;a1=(j+.5)*2*math.pi/na
            xc,ac=(x0+x1)/2,(a0+a1)/2
            hole=i in (1,2,3,4) and j in (3,5,11,13)
            if not hole:
                for r in (.0131,.0118):patch([point(x0,a0,r),point(x1,a0,r),point(x1,a1,r),point(x0,a1,r)])
            else:
                hole_centres.append((xc,ac))
                boundary=[(x1,a1),(x0,a1),(x0,a0),(x1,a0)]
                aperture=[(xc+hole_x*math.cos(k*math.pi/4),ac+hole_angle*math.sin(k*math.pi/4)) for k in range(8)]
                inside=[(xc+inner_x*math.cos(k*math.pi/4),ac+inner_angle*math.sin(k*math.pi/4)) for k in range(8)]
                for c in range(4):
                    k=2*c+1
                    for r in (.0131,.0118):
                        loop=aperture if r==outer_r else inside
                        patch([point(*boundary[c],r),point(*loop[(k-1)%8],r),point(*loop[k],r),point(*loop[(k+1)%8],r)])
                        patch([point(*boundary[c],r),point(*boundary[(c+1)%4],r),point(*loop[(k+1)%8],r)])
                for k in range(8):
                    q=(k+1)%8
                    # Micro edge roll lies entirely inside the old aperture.
                    # Existing outside opening, tube silhouette and hole pitch
                    # remain unchanged; this adds 16 triangles per aperture.
                    patch([point(*aperture[k],outer_r),point(*aperture[q],outer_r),point(*inside[q],wall_r),point(*inside[k],wall_r)])
                    patch([point(*inside[k],wall_r),point(*inside[q],wall_r),point(*inside[q],inner_r),point(*inside[k],inner_r)])
            if i==0:patch([point(x0,a0,.0131),point(x0,a1,.0131),point(x0,a1,.0118),point(x0,a0,.0118)])
            if i==nx-1:patch([point(x1,a0,.0131),point(x1,a1,.0131),point(x1,a1,.0118),point(x1,a0,.0118)])
    ob=mesh('Barrel_perforated_sleeve',vertices,faces,material,'Action')
    bm=bmesh.new();bm.from_mesh(ob.data)
    bmesh.ops.remove_doubles(bm,verts=list(bm.verts),dist=1e-7)
    bmesh.ops.recalc_face_normals(bm,faces=list(bm.faces))
    assert all(e.is_manifold for e in bm.edges),'Sleeve must have closed wall edges around its holes'
    bm.to_mesh(ob.data);bm.free();ob.data.update()
    # Radial tube normals stay exact. The tiny edge roll joins them tangentially
    # to a smooth unwrapped ellipse normal on each aperture wall, avoiding eight
    # unrelated sparkling facets around every opening.
    normals=[]
    def angle_delta(a,b):return math.atan2(math.sin(a-b),math.cos(a-b))
    def wall_normal(p,centre):
        xc,ac=centre
        r=math.hypot(p.y,p.z-.0075)
        a=math.atan2(p.y,p.z-.0075)
        nx=-(p.x-xc)/(inner_x*inner_x)
        nt=-angle_delta(a,ac)/(inner_angle*inner_angle*r)
        return Vector((nx,nt*math.cos(a),-nt*math.sin(a))).normalized()
    for face in ob.data.polygons:
        radii=[math.hypot(ob.data.vertices[i].co.y,ob.data.vertices[i].co.z-.0075) for i in face.vertices]
        radial=1 if all(abs(r-.0131)<1e-6 for r in radii) else -1 if all(abs(r-.0118)<1e-6 for r in radii) else 0
        endcap=all(abs(ob.data.vertices[i].co.x-start)<1e-6 for i in face.vertices) or all(abs(ob.data.vertices[i].co.x-end)<1e-6 for i in face.vertices)
        rolled=not radial and not endcap and min(radii)>wall_r-1e-6
        centre=None
        if not radial and not endcap:
            a=math.atan2(face.center.y,face.center.z-.0075)
            centre=min(hole_centres,key=lambda c:((face.center.x-c[0])/hole_x)**2+(angle_delta(a,c[1])/hole_angle)**2)
        face.use_smooth=not endcap
        for li in face.loop_indices:
            p=ob.data.vertices[ob.data.loops[li].vertex_index].co
            r=math.hypot(p.y,p.z-.0075)
            if radial:
                n=Vector((0,radial*p.y/r,radial*(p.z-.0075)/r))
            elif endcap:
                n=face.normal
            elif rolled and abs(r-outer_r)<1e-6:
                n=Vector((0,p.y/r,(p.z-.0075)/r))
            else:
                n=wall_normal(p,centre)
            normals.append(tuple(n))
    ob.data.normals_split_custom_set(normals)
    ob['design_note']='Sixteen open apertures with inward micro edge roll; radial tube and smooth aperture-wall normals; follows Action'
    return ob
