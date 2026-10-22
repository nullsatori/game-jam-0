"""Sculpted grip shell with actual recessed side panels; exterior art only."""
from mathutils import Vector
from refine_contact_normals import refine_contact_normals


def _inset(points,distance):
    area=sum(a[0]*b[1]-b[0]*a[1] for a,b in zip(points,points[1:]+points[:1]))
    sign=1 if area>0 else -1
    lines=[]
    for a,b in zip(points,points[1:]+points[:1]):
        a,b=Vector(a),Vector(b);direction=(b-a).normalized()
        lines.append((a+Vector((-direction.y,direction.x))*distance*sign,direction))
    result=[]
    for i,(b,v) in enumerate(lines):
        a,u=lines[i-1];delta=b-a;cross=u.x*v.y-u.y*v.x
        t=(delta.x*v.y-delta.y*v.x)/cross
        result.append(tuple(a+u*t))
    return result


def _chamfer(points,distance):
    result=[]
    for i,p in enumerate(points):
        p=Vector(p)
        result.extend([tuple(p+(Vector(points[i-1])-p).normalized()*distance),
                       tuple(p+(Vector(points[(i+1)%len(points)])-p).normalized()*distance)])
    return result


def build_grip(mesh,prism,parts,composite,check,steel,black,bpy):
    """Return five editable grip meshes. Materials are supplied by the caller."""
    def px(points):return [((1345-x)*.000415,(451-z)*.000415) for x,z in points]
    outline=px([(1465,400),(1508,401),(1522,420),(1571,516),(1622,632),
                (1620,658),(1522,698),(1492,693),(1483,681),(1489,660),
                (1460,552),(1446,522),(1457,469),(1446,445)])
    body=prism('Pistol_grip',outline,.017,composite,bevel=.003)
    bpy.context.view_layer.objects.active=body
    for mod in list(body.modifiers):bpy.ops.object.modifier_apply(modifier=mod.name)
    handled=[body]
    raw=px([(1498,471),(1513,475),(1606,635),(1602,648),
            (1536,674),(1524,665),(1485,503),(1485,484)])
    pocket=_chamfer(raw,.00055)

    def layer_mesh(name,layers,material):
        n=len(pocket);vertices=[]
        for y,inset in layers:
            vertices.extend((x,y,z) for x,z in _inset(pocket,inset))
        faces=[tuple(range(n-1,-1,-1)),tuple(range((len(layers)-1)*n,len(layers)*n))]
        for j in range(len(layers)-1):
            for i in range(n):
                k=(i+1)%n;a=j*n;b=(j+1)*n
                faces.append((a+i,a+k,b+k,b+i))
        return mesh(name,vertices,faces,material,'Body')

    for side in (-1,1):
        # The cutter is deliberately unregistered: only the final five art
        # meshes belong to parts. The pocket floor is 2.7 mm below the shell.
        n=len(pocket)
        verts=[(x,side*y,z) for y in (.0143,.022) for x,z in pocket]
        faces=[tuple(range(n-1,-1,-1)),tuple(range(n,2*n))]
        faces.extend((i,(i+1)%n,(i+1)%n+n,i+n) for i in range(n))
        me=bpy.data.meshes.new('_Grip_recess_cutter');me.from_pydata(verts,[],faces);me.update()
        me.materials.append(composite)
        cutter=bpy.data.objects.new('_Grip_recess_cutter',me);bpy.context.scene.collection.objects.link(cutter)
        # Mirror changes orientation; ensure cutters are consistently outward.
        import bmesh
        bm=bmesh.new();bm.from_mesh(me);bmesh.ops.recalc_face_normals(bm,faces=list(bm.faces));bm.to_mesh(me);bm.free()
        bpy.context.view_layer.objects.active=body
        mod=body.modifiers.new('Recessed side panel','BOOLEAN');mod.operation='DIFFERENCE';mod.solver='EXACT';mod.object=cutter
        bpy.ops.object.modifier_apply(modifier=mod.name)
        bpy.data.objects.remove(cutter,do_unlink=True);bpy.data.meshes.remove(me)
        gasket=layer_mesh('Grip_inlay_edge_'+str(side),
                         [(side*.01438,.00015),(side*.01465,.00015),(side*.01472,.00025)],black)
        panel=layer_mesh('Grip_checkered_phenolic_'+str(side),
                        [(side*.0148,.00065),(side*.01585,.00065),
                         (side*.01645,.00080),(side*.01670,.00110)],check)
        gasket['design_note']='Recessed dark seating reveal, no metal picture-frame'
        panel['design_note']='Seated traction panel with narrow rolled perimeter; face 0.3 mm below shell'
        handled.extend((gasket,panel))

    # The existing contact-normal helper preserves planar side faces and
    # smooths the actual shell bevel. Apply the same rule to the small panels.
    for ob in handled:
        if ob is body:
            refine_contact_normals(ob)
        else:
            # The helper dispatches by name; avoid renaming live objects.
            me=ob.data
            adjacent=[[] for _ in me.vertices]
            for f in me.polygons:
                for vi in f.vertices:adjacent[vi].append(f)
            normals=[]
            for f in me.polygons:
                f.use_smooth=True
                for li in f.loop_indices:
                    entries=adjacent[me.loops[li].vertex_index]
                    caps=[p.normal for p in entries if abs(p.normal.y)>.9999 and p.normal.dot(f.normal)>.25]
                    if abs(f.normal.y)>.9999:n=Vector((0,1 if f.normal.y>0 else -1,0))
                    elif caps:n=Vector((0,1 if caps[0].y>0 else -1,0))
                    else:
                        n=sum((p.normal for p in entries if p.normal.dot(f.normal)>.35),Vector()).normalized()
                    normals.append(tuple(n))
            me.normals_split_custom_set(normals)
        ob['grip_custom_normals']=True
    body['design_note']='Pressed composite shell, restrained rear palm swell, two actual recessed traction-panel seats'
    return handled
