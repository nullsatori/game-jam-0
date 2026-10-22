"""Premium industrial fore-end: pressed planes, diagonal grip cuts and inlay.

Called by build_model.py. All dimensions are game-art metres. No scene reset,
file output, material creation, or changes outside the created Action parts.
"""
import math
from joinery_geometry import seated_panel


def build_foreend(mesh, prism, panel, cyl, rr, parts, poly, steel, edge,
                  burg, rubber, bpy):
    # Each cross-section keeps the side planar. Rib positions do not alter the
    # entire section; independent oblique pockets give crisp pressed features.
    # The front changes smoothly into a circular ferrule seat before the metal
    # collar starts, so square shell corners never poke through the collar.
    cross = rr(0, .006, .041, .041, .0035, n=3)
    rings = [(.043, .93, 0), (.046, 1, 0), (.104, 1, 0),
             (.283, 1, 0), (.296, 1, 0), (.302, 1, .45),
             (.307, 1, 1), (.312, .99, 1)]
    verts = []
    for x, scale, roundness in rings:
        for y, z in cross:
            dy, dz = y, z - .006
            length = math.hypot(dy, dz)
            cy, cz = dy / length * .0205, dz / length * .0205
            verts.append((x, (dy*(1-roundness)+cy*roundness)*scale,
                          .006+(dz*(1-roundness)+cz*roundness)*scale))
    n = len(cross)
    faces = [tuple(range(n-1, -1, -1)),
             tuple(range((len(rings)-1)*n, len(rings)*n))]
    for j in range(len(rings)-1):
        for i in range(n):
            faces.append((j*n+i, j*n+(i+1)%n,
                          (j+1)*n+(i+1)%n, (j+1)*n+i))
    fore = mesh('Foreend_sculpted_ribs', verts, faces, poly, 'Action')

    # One mesh containing disconnected cutters avoids modifier-order dependent
    # shading and leaves a single cohesive shell with genuine recess rims.
    cv, cf = [], []

    def side_pocket(outline, y0, y1):
        start = len(cv)
        count = len(outline)
        cv.extend((x, y, z) for y in (y0, y1) for x, z in outline)
        cf.extend([tuple(start+i for i in range(count-1, -1, -1)),
                   tuple(start+count+i for i in range(count))])
        for i in range(count):
            k = (i+1) % count
            cf.append((start+i, start+k, start+count+k, start+count+i))

    def bottom_pocket(outline, z0, z1):
        start = len(cv)
        count = len(outline)
        cv.extend((x, y, z) for z in (z0, z1) for x, y in outline)
        cf.extend([tuple(start+i for i in range(count-1, -1, -1)),
                   tuple(start+count+i for i in range(count))])
        for i in range(count):
            k = (i+1) % count
            cf.append((start+i, start+k, start+count+k, start+count+i))

    # User-selected regular field: exactly eight identical cuts, one pitch,
    # one width and one inclination. The underside uses the same eight centres.
    # The steel plate above is centred on this field rather than on an unrelated
    # visual subdivision, so the assembly reads as one designed component.
    grip_cuts = [(.122 + .021*i, .004) for i in range(8)]
    for x, width in grip_cuts:
        h = width/2
        shape = [(x-.004-h,-.0094), (x-.0035-h,-.0105),
                 (x-.004+h,-.0105), (x+.004+h,.0106),
                 (x+.0033+h,.0120), (x+.004-h,.0120)]
        side_pocket(shape, .0188, .025)
        side_pocket(shape, -.025, -.0188)
        # Matching understated diagonal tread below, without radial shrinkage
        # or scalloping the whole outer silhouette.
        tread = [(x-.0024-h,-.0134), (x-.0016-h,-.0144),
                 (x-.0024+h,-.0144), (x+.0024+h,.0134),
                 (x+.0016+h,.0144), (x+.0024-h,.0144)]
        bottom_pocket(tread, -.019, -.013)

    rear_opening = [(.051,-.009),(.104,-.009),(.108,-.005),
                    (.108,.001),(.099,.010),(.055,.010),(.051,.006)]

    def spine_outline(inset=0):
        # Symmetric chamfered rectangle, aligned over the eight-cut field.
        # Adjusting the chamfer by (2-sqrt(2))*inset keeps the perpendicular
        # clearance constant on diagonal edges as well as on straight edges.
        x0, x1 = .110+inset, .281-inset
        z0, z1 = .0141+inset, .0227-inset
        chamfer = .002-(2-math.sqrt(2))*inset
        return [(x0+chamfer,z0),(x1-chamfer,z0),
                (x1,z0+chamfer),(x1,z1-chamfer),
                (x1-chamfer,z1),(x0+chamfer,z1),
                (x0,z1-chamfer),(x0,z0+chamfer)]

    spine_opening = spine_outline()

    for side in (-1, 1):
        side_pocket(rear_opening,
                    min(side*.0187, side*.025), max(side*.0187, side*.025))
        side_pocket(spine_opening,
                    min(side*.0191, side*.025), max(side*.0191, side*.025))

    # Clearance for the stationary receiver in the authored closed pose and
    # visual +X action travel. This open rear-top channel retains both side
    # shoulders and insert walls; the upper tube already clears the shell.
    side_pocket([(.040, .014), (.135, .014),
                 (.135, .034), (.040, .034)], -.0183, .0183)

    cutter = mesh('Foreend_pocket_cutters', cv, cf, poly, 'Action')
    bpy.context.view_layer.objects.active = fore
    mod = fore.modifiers.new('Pressed diagonal grip and spine pockets', 'BOOLEAN')
    mod.operation = 'DIFFERENCE'
    mod.solver = 'EXACT'
    mod.object = cutter
    bpy.ops.object.modifier_apply(modifier=mod.name)
    parts.remove(cutter)
    bpy.data.objects.remove(cutter, do_unlink=True)

    # Small edge break keeps the silhouette clean and the moulded grooves from
    # reading as soft radial corrugations. One segment limits the triangle cost.
    bevel = fore.modifiers.new('Restrained pressed edge break', 'BEVEL')
    bevel.width = .00022
    bevel.segments = 1
    # Round the mouth of each pocket, keeping the invisible pocket floor sharp.
    # Weighting only actual non-coplanar surface edges avoids doubling both
    # rings of every recess and keeps the complete fore-end below 4500 tris.
    import bmesh
    bm = bmesh.new()
    bm.from_mesh(fore.data)
    bm.edges.ensure_lookup_table()
    bm.edges.index_update()
    weights = fore.data.attributes.new('bevel_weight_edge', 'FLOAT', 'EDGE')
    for e in bm.edges:
        a, b = (v.co for v in e.verts)
        side_mouth = (abs(abs(a.y)-.0205)<1e-6 and
                      abs(abs(b.y)-.0205)<1e-6 and a.y*b.y>0)
        bottom_mouth = abs(a.z+.0145)<1e-6 and abs(b.z+.0145)<1e-6
        if (side_mouth or bottom_mouth) and e.is_manifold and e.calc_face_angle()>.3:
            weights.data[e.index].value = 1
    bm.free()
    bevel.limit_method = 'WEIGHT'
    bevel.use_clamp_overlap = True
    for p in fore.data.polygons:
        p.use_smooth = True
    fore.data.set_sharp_from_angle(angle=math.radians(38))
    wn = fore.modifiers.new('Stable pressed face normals', 'WEIGHTED_NORMAL')
    wn.keep_sharp = True
    wn.weight = 70

    collar = cyl('Foreend_front_collar', (.307, 0, .006), (.321, 0, .006),
                 .0216, steel, 'Action', n=28, bevel=.00025)

    for side in (-1, 1):
        # All inserts now use the same controlled seat logic: a thin backing
        # below a rolled perimeter, with no intersecting plates or sharp foil.
        rear=seated_panel(mesh,'Foreend_rear_recess_'+str(side),rear_opening,side,
                          [(.01876,.00050),(.01918,.00050),(.01943,.00072)],rubber)
        rear['design_note']='Molded rear grip pad with uniform seat clearance and a narrow rolled edge'
        border=seated_panel(mesh,'Foreend_insert_border_'+str(side),spine_opening,side,
                            [(.01916,.00020),(.01940,.00020),(.01950,.00028)],rubber)
        plate=seated_panel(mesh,'Foreend_phenolic_insert_'+str(side),spine_opening,side,
                           [(.01955,.00070),(.01972,.00070),(.01991,.00088)],steel)
        plate['design_note']='Thin recessed steel wear strip, symmetric chamfered terminations, rolled edge and uniform dark seating reveal'

    fore['design_note'] = 'Eight equal 4mm diagonal grip cuts on 21mm pitch; coordinated underside; one symmetric recessed steel wear plate'
    fore['editable_part'] = True
    return fore
