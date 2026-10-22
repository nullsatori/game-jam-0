"""Read-only native/export verification and additional delivery renders.

Run with Blender --background --python verify_delivery.py. --skip-renders
after -- verifies a revised export without regenerating the contact views.
Never writes the authored .blend or modifies production textures/exports.
"""
from pathlib import Path
import bpy
import bmesh
import json
import math
import struct
import sys
from mathutils import Vector, Matrix
from mathutils.bvhtree import BVHTree

ROOT = Path(__file__).resolve().parents[1]
REPORT = ROOT / 'roundtrip_validation.json'
GROUPS = ['Body', 'Action', 'Stock', 'LoadingLid', 'Counter']
report = {'status': 'running', 'unreal_runtime_verified': False,
          'tests': {}, 'renders': [], 'failures': []}


def note(name, passed, **details):
    report['tests'][name] = {'passed': bool(passed), **details}
    if not passed:
        report['failures'].append(name)
    print('VERIFY', name, bool(passed), flush=True)


def persist():
    REPORT.write_text(json.dumps(report, indent=2), encoding='utf-8')


def png_info(path):
    with path.open('rb') as f:
        header = f.read(26)
    assert header[:8] == b'\x89PNG\r\n\x1a\n', path
    w, h = struct.unpack('>II', header[16:24])
    return {'size': [w, h], 'bits': header[24], 'color_type': header[25]}


def game_objects():
    return [o for o in bpy.context.scene.objects
            if o.type == 'MESH' and 'assembly' in o]


def bounds(objects):
    pts = [o.matrix_world @ v.co for o in objects for v in o.data.vertices]
    lo = [min(p[i] for p in pts) for i in range(3)]
    hi = [max(p[i] for p in pts) for i in range(3)]
    return {'min': lo, 'max': hi, 'dimensions': [hi[i]-lo[i] for i in range(3)]}


def tri_count(objects):
    return sum(len(p.vertices)-2 for o in objects for p in o.data.polygons)


def bvh(obj, delta=0, transform=None):
    matrix = (transform if transform is not None else Matrix.Translation((delta, 0, 0))) @ obj.matrix_world
    vertices = [matrix @ v.co for v in obj.data.vertices]
    faces = [tuple(p.vertices) for p in obj.data.polygons]
    return BVHTree.FromPolygons(vertices, faces, all_triangles=True, epsilon=1e-7)


def verify_joinery(hinge_transform):
    """Pinned exterior seat checks on the final baked native meshes."""
    inlay_names=[stem+str(side) for side in (-1,1) for stem in
                 ('Foreend_rear_recess_','Foreend_insert_border_','Foreend_phenolic_insert_')]
    closed_names=['Loading_lid','Counter_recess_rim','Counter_sealing_gasket',
                  'Foreend_front_collar','Upper_front_cap',
                  'Stock_rear_hinge_-1','Stock_rear_hinge_1']+inlay_names
    topology=[]
    for name in closed_names:
        ob=bpy.data.objects.get(name)
        if ob is None:
            topology.append({'part':name,'missing':True});continue
        bm=bmesh.new();bm.from_mesh(ob.data)
        topology.append({'part':name,'nonmanifold_edges':sum(not e.is_manifold for e in bm.edges),
                         'degenerate_faces':sum(f.calc_area()<1e-12 for f in bm.faces),
                         'signed_volume':bm.calc_volume(signed=True)})
        bm.free()
    note('joinery_closed_positive_topology',all(not x.get('missing') and
         x['nonmanifold_edges']==0 and x['degenerate_faces']==0 and x['signed_volume']>0
         for x in topology),parts=topology)

    lid=bpy.data.objects.get('Loading_lid');receiver=bpy.data.objects.get('Receiver_shell')
    samples=[]
    if lid and receiver:
        fixed=bvh(receiver)
        for step in range(21):
            angle=80*step/20
            pairs=bvh(lid,transform=hinge_transform((.105,0,.055),angle)).overlap(fixed)
            samples.append({'angle_degrees':angle,'triangle_intersection_pairs':len(pairs)})
    note('loading_lid_receiver_clearance_21_samples',len(samples)==21 and
         all(x['triangle_intersection_pairs']==0 for x in samples),samples=samples,
         limitation='Exterior seat surfaces only; hinge contact is intentionally excluded.')

    insert_hits=[];missing=[]
    for side in (-1,1):
        names=[stem+str(side) for stem in
               ('Foreend_rear_recess_','Foreend_insert_border_','Foreend_phenolic_insert_')]
        for i,a in enumerate(names):
            for b in names[i+1:]+['Foreend_sculpted_ribs']:
                oa,ob=bpy.data.objects.get(a),bpy.data.objects.get(b)
                if oa is None or ob is None:missing.append([a,b]);continue
                pairs=bvh(oa).overlap(bvh(ob))
                if pairs:insert_hits.append({'a':a,'b':b,'pairs':len(pairs)})
    note('foreend_insert_seating_surface_clearance',not missing and not insert_hits,
         missing_pairs=missing,intersections=insert_hits,
         limitation='Surface intersections for the separate recessed inserts and surrounding shell; not a minimum-gap measurement.')

    counter_names=['Counter_recess_rim','Counter_sealing_gasket','Counter_face']
    counter_hits=[];counter_missing=[]
    for i,a in enumerate(counter_names):
        for b in counter_names[i+1:]:
            oa,ob=bpy.data.objects.get(a),bpy.data.objects.get(b)
            if oa is None or ob is None:counter_missing.append([a,b]);continue
            pairs=bvh(oa).overlap(bvh(ob))
            if pairs:counter_hits.append({'a':a,'b':b,'pairs':len(pairs)})
    note('counter_glass_gasket_bezel_surface_clearance',not counter_missing and not counter_hits,
         missing_pairs=counter_missing,intersections=counter_hits)

    # Independently pinned pre-joinery face contract, not read from the active
    # build helper or inferred from the current mesh. The 20-vertex rounded
    # face and its unatlased UV mapping were frozen before this geometry pass.
    expected=[]
    for u0,v0,start in [(.0095,.0065,0),(-.0095,.0065,90),
                        (-.0095,-.0065,180),(.0095,-.0065,270)]:
        for step in range(5):
            a=math.radians(start+22.5*step)
            u=u0+.0025*math.cos(a);v=v0+.0025*math.sin(a)
            expected.append((Vector((-.030+.6*v-.8*.00025,-u,.056+.8*v+.6*.00025)),
                             Vector((.5+u/.024,.5+v/.018))))
    face=bpy.data.objects.get('Counter_face');geometry_error=math.inf;uv_error=math.inf
    if face and len(face.data.vertices)==20 and face.data.uv_layers.active:
        match={}
        geometry_error=0
        for vertex in face.data.vertices:
            world=face.matrix_world@vertex.co
            index=min(range(20),key=lambda i:(world-expected[i][0]).length_squared)
            match[vertex.index]=index
            geometry_error=max(geometry_error,(world-expected[index][0]).length)
        uv_error=max((loop.uv-expected[match[face.data.loops[i].vertex_index]][1]).length
                     for i,loop in enumerate(face.data.uv_layers.active.data))
        unique_matches=len(set(match.values()))
    else:unique_matches=0
    note('counter_face_frozen_geometry_uv_contract',unique_matches==20 and
         geometry_error<2e-7 and uv_error<2e-6,
         expected_vertex_count=20,unique_matches=unique_matches,
         maximum_position_error_m=geometry_error if math.isfinite(geometry_error) else None,
         maximum_uv_error=uv_error if math.isfinite(uv_error) else None,
         baseline='Pre-joinery face: center(-.030,0,.056), right(0,-1,0), up(.6,0,.8), normal(-.8,0,.6), depth .00025, width .024, height .018, corner radius .0025, 4 corner intervals.')


def render_views(objects):
    s = bpy.context.scene
    s.render.resolution_x = 1672
    s.render.resolution_y = 941
    s.render.resolution_percentage = 100
    s.render.image_settings.file_format = 'PNG'
    s.render.image_settings.color_mode = 'RGB'
    s.render.film_transparent = False
    s.cycles.samples = 40
    s.cycles.use_denoising = True
    try:
        prefs = bpy.context.preferences.addons['cycles'].preferences
        prefs.compute_device_type = 'OPTIX'
        prefs.get_devices()
        for d in prefs.devices:
            d.use = d.type != 'CPU'
        s.cycles.device = 'GPU'
    except Exception:
        s.cycles.device = 'CPU'
    backdrop = bpy.data.objects.get('Studio_Backdrop')
    if backdrop:
        backdrop.hide_render = True  # Do not obscure the opposite-side view.
    camera = s.camera
    fill_data=bpy.data.lights.new('Review_lower_fill','AREA');fill_data.shape='DISK';fill_data.size=.7
    fill=bpy.data.objects.new('Review_lower_fill',fill_data);s.collection.objects.link(fill)
    fill.location=(.3,.2,-.5);fill.rotation_euler=(Vector((.2,0,0))-fill.location).to_track_quat('-Z','Y').to_euler()
    camera.data.type = 'ORTHO'
    native_matrices = {o.name: o.matrix_world.copy() for o in objects}
    aspect = 1672/941

    def reset_pose():
        for o in objects:
            o.matrix_world = native_matrices[o.name].copy()
        bpy.context.view_layer.update()

    def capture(name, direction, margin=1.09):
        fill_data.energy=5 if name in ('08_bottom','09_front') else 0
        extent = bounds(objects)
        center = (Vector(extent['min'])+Vector(extent['max']))/2
        camera.location = center + Vector(direction).normalized()
        camera.rotation_euler = (center-camera.location).to_track_quat('-Z', 'Y').to_euler()
        bpy.context.view_layer.update()
        view = camera.matrix_world.inverted()
        pts = [view @ (o.matrix_world @ v.co) for o in objects for v in o.data.vertices]
        width = max(p.x for p in pts)-min(p.x for p in pts)
        height = max(p.y for p in pts)-min(p.y for p in pts)
        camera.data.ortho_scale = max(width, height*aspect)*margin
        s.render.filepath = str(ROOT/'renders'/(name+'.png'))
        bpy.ops.render.render(write_still=True)
        path = Path(s.render.filepath)
        info = png_info(path)
        report['renders'].append({'file': 'renders/'+path.name,
                                 'size': info['size'], 'bytes': path.stat().st_size,
                                 'source': 'native_baked_blend',
                                 'pose': 'open_access' if 'access' in name else
                                         'pump_forward' if 'pump' in name else 'closed'})
        persist()

    for name, direction in [('05_left',(0,1,0)), ('06_right',(0,-1,0)),
                            ('07_top',(0,0,1)), ('08_bottom',(0,0,-1)),
                            ('09_front',(1,0,0)), ('10_rear',(-1,0,0)),
                            ('11_hero',(.18,1,.27))]:
        capture(name, direction)
    for o in objects:
        if o['assembly'] == 'Action':
            o.matrix_world = Matrix.Translation((.055,0,0)) @ native_matrices[o.name]
    bpy.context.view_layer.update()
    capture('12_pump_forward', (0,1,.05))
    reset_pose()
    for o in objects:
        if o['assembly'] == 'Stock':
            pivot, angle = Vector((-.012,0,.063)), -110
        elif o['assembly'] == 'LoadingLid':
            pivot, angle = Vector((.105,0,.055)), 80
        else:
            continue
        o.matrix_world = Matrix.Translation(pivot) @ Matrix.Rotation(math.radians(angle),4,'Y') @ Matrix.Translation(-pivot) @ native_matrices[o.name]
    bpy.context.view_layer.update()
    capture('13_load_access', (.1,1,.28), 1.12)
    reset_pose()


try:
    bpy.ops.wm.open_mainfile(filepath=str(ROOT/'Jam_RMB93_v02.blend'))
    objects = game_objects()
    counter_faces=[p for o in objects if o.get('assembly')=='Counter' for p in o.data.polygons]
    note('counter_faces_outward', bool(counter_faces) and all(p.normal.dot(Vector((-.8,0,.6)))>.99 for p in counter_faces))
    source_bounds = bounds(objects)
    source_triangles = tri_count(objects)
    source_groups = sorted({o['assembly'] for o in objects})
    per_group = {g: bounds([o for o in objects if o['assembly']==g])
                 for g in source_groups}
    report['native'] = {'parts': len(objects), 'triangles': source_triangles,
                        'bounds': source_bounds, 'assemblies': source_groups,
                        'group_bounds': per_group}
    note('native_assemblies', source_groups == sorted(GROUPS), expected=GROUPS, actual=source_groups)
    note('triangle_budget', 0 < source_triangles <= 12000, actual=source_triangles, maximum=12000)
    mats = sorted({m.name for o in objects for m in o.data.materials if m})
    note('two_materials', mats == ['Body','Counter'], actual=mats)
    uv_issues = []
    for o in objects:
        uv = o.data.uv_layers.active
        if not uv:
            uv_issues.append({'part': o.name, 'reason': 'no UV'})
            continue
        bad = sum(not all(math.isfinite(c) and -1e-5 <= c <= 1.00001 for c in loop.uv)
                  for loop in uv.data)
        if bad:
            uv_issues.append({'part': o.name, 'invalid_loops': bad})
    note('uv0_finite_single_tile', not uv_issues, issues=uv_issues)
    expected_textures = {'BC':4096, 'N':4096, 'ORM':4096, 'Masks':1024,
                         'Counter_BC':512, 'Counter_N':512,
                         'Counter_ORM':512, 'Counter_Masks':512, 'DigitAtlas_M':1024}
    texture_info = []
    valid = True
    for stem, size in expected_textures.items():
        path = ROOT/'textures'/('T_Jam_RMB93_'+stem+'.png')
        if not path.exists():
            texture_info.append({'file': path.name, 'missing': True}); valid=False; continue
        info = png_info(path)
        good = info['size']==[size,size] and info['bits']==8 and info['color_type']==2
        texture_info.append({'file': path.name, **info, 'valid': good})
        valid = valid and good
    note('nine_rgb_texture_contract', valid and len(texture_info)==9, textures=texture_info)

    pair_names = [('Foreend_sculpted_ribs','Receiver_shell'),
                  ('Lower_barrel','Receiver_shell'),
                  ('Foreend_front_collar','Upper_magazine'),
                  ('Barrel_perforated_sleeve','Upper_magazine')]
    movement = []
    for moving_name, fixed_name in pair_names:
        moving, fixed = bpy.data.objects.get(moving_name), bpy.data.objects.get(fixed_name)
        if moving is None or fixed is None:
            movement.append({'moving':moving_name, 'fixed':fixed_name, 'missing':True}); continue
        stationary = bvh(fixed)
        results=[]
        for step in range(21):
            offset = .055*step/20
            overlaps = bvh(moving, offset).overlap(stationary)
            results.append({'offset_m':offset, 'triangle_intersection_pairs':len(overlaps)})
        movement.append({'moving':moving_name, 'fixed':fixed_name, 'samples':results})
    clear = all(not pair.get('missing') and all(s['triangle_intersection_pairs']==0 for s in pair['samples']) for pair in movement)
    note('visual_pump_clearance_21_samples', clear, pairs=movement,
         limitation='Surface triangle intersections for four specified exterior art pairs only; no physics, swept-volume, or functioning mechanism claim.')
    grip_parts=[o for o in objects if o.get('grip_custom_normals')]
    grip_topology=[]
    for ob in grip_parts:
        bm=bmesh.new();bm.from_mesh(ob.data)
        grip_topology.append({'part':ob.name,'nonmanifold_edges':sum(not e.is_manifold for e in bm.edges),
            'degenerate_faces':sum(f.calc_area()<1e-12 for f in bm.faces),'signed_volume':bm.calc_volume(signed=True)})
        bm.free()
    note('grip_closed_positive_topology',len(grip_parts)==5 and all(
        x['nonmanifold_edges']==0 and x['degenerate_faces']==0 and x['signed_volume']>0
        for x in grip_topology),parts=grip_topology)
    grip_hits=[]
    for i,a in enumerate(grip_parts):
        for b in grip_parts[i+1:]:
            hits=bvh(a).overlap(bvh(b))
            if hits:grip_hits.append({'a':a.name,'b':b.name,'pairs':len(hits)})
    note('grip_seating_surface_clearance',len(grip_parts)==5 and not grip_hits,intersections=grip_hits)
    controls=[o for o in objects if o.get('control_custom_normals')]
    control_topology=[]
    for ob in controls:
        bm=bmesh.new();bm.from_mesh(ob.data)
        control_topology.append({'part':ob.name,'nonmanifold_edges':sum(not e.is_manifold for e in bm.edges),
            'degenerate_faces':sum(f.calc_area()<1e-12 for f in bm.faces),'signed_volume':bm.calc_volume(signed=True)})
        bm.free()
    note('controls_closed_positive_topology',len(controls)==6 and all(
        x['nonmanifold_edges']==0 and x['degenerate_faces']==0 and x['signed_volume']>0
        for x in control_topology),parts=control_topology)
    control_hits=[]
    for ob in controls:
        fixed=bvh(ob)
        for action in [o for o in objects if o['assembly']=='Action']:
            for step in range(21):
                if bvh(action,.055*step/20).overlap(fixed):
                    control_hits.append({'control':ob.name,'action':action.name,'sample':step})
    note('controls_action_clearance_21_samples',bool(controls) and not control_hits,intersections=control_hits,
         limitation='Exterior surface intersections at sampled art poses; no internal mechanism or hand fit verification.')
    # The redesigned folded frame must clear the visible fixed shell and the
    # independently opening lid throughout the art-review access pose.
    stock=[o for o in objects if o['assembly']=='Stock']
    stock_topology=[]
    for ob in stock:
        bm=bmesh.new();bm.from_mesh(ob.data)
        result={'part':ob.name,'nonmanifold_edges':sum(not e.is_manifold for e in bm.edges),
                'degenerate_faces':sum(f.calc_area()<1e-12 for f in bm.faces),
                'signed_volume':bm.calc_volume(signed=True)}
        bm.free();stock_topology.append(result)
    note('stock_closed_positive_topology',bool(stock_topology) and all(
        x['nonmanifold_edges']==0 and x['degenerate_faces']==0 and x['signed_volume']>0
        for x in stock_topology),parts=stock_topology)
    fixed_names=['Receiver_shell','Upper_magazine','Upper_rear_ferrule','Lower_barrel','Barrel_perforated_sleeve',
                 'Front_sight','Rear_sight_sculpted_notch','Rear_sight_foot','Counter_face']
    fixed_stock={name:bvh(bpy.data.objects[name]) for name in fixed_names}
    lid=bpy.data.objects['Loading_lid']
    def hinge_transform(pivot, angle):
        p=Vector(pivot)
        return Matrix.Translation(p) @ Matrix.Rotation(math.radians(angle),4,'Y') @ Matrix.Translation(-p)
    verify_joinery(hinge_transform)
    for route,stock_angle,lid_angle in [('access',-110,80),('deployment',-195,0)]:
        stock_samples=[]
        for step in range(21):
            t=step/20
            sm=hinge_transform((-.012,0,.063),stock_angle*t)
            lm=hinge_transform((.105,0,.055),lid_angle*t)
            targets=dict(fixed_stock);targets['Loading_lid']=bvh(lid,transform=lm)
            hits=[]
            for ob in stock:
                tree=bvh(ob,transform=sm)
                for name,target in targets.items():
                    overlaps=tree.overlap(target)
                    if overlaps:hits.append({'stock_part':ob.name,'fixed_part':name,'pairs':len(overlaps)})
            stock_samples.append({'pose_fraction':t,'intersections':hits})
        note('stock_'+route+'_clearance_21_samples',bool(stock) and not any(x['intersections'] for x in stock_samples),
             stock_parts=[o.name for o in stock],fixed_targets=fixed_names+['Loading_lid'],samples=stock_samples,
             limitation='Exterior triangle intersection samples only; hinge contact parts excluded; not continuous collision or a mechanical simulation.')
    persist()

    if '--skip-renders' not in sys.argv:
        render_views(objects)

    # Import into two separate empty scenes. Exporters may change local object
    # transforms, so compare transformed world-space bounds by assembly.
    for kind, path in [('glb', ROOT/'exports/Jam_RMB93_v02.glb'),
                       ('fbx', ROOT/'exports/SM_Jam_RMB93_World.fbx')]:
        bpy.ops.wm.read_factory_settings(use_empty=True)
        if kind == 'glb':
            bpy.ops.import_scene.gltf(filepath=str(path))
        else:
            bpy.ops.import_scene.fbx(filepath=str(path), use_custom_normals=True)
        meshes = [o for o in bpy.context.scene.objects if o.type=='MESH']
        assembly_map = {}
        for o in meshes:
            group = o.get('assembly')
            if group not in GROUPS:
                group = next((g for g in GROUPS if o.name.startswith('SM_Jam_RMB93_'+g)), None)
            assembly_map.setdefault(group, []).append(o)
        imported_bounds = bounds(meshes)
        error = max(abs(imported_bounds[k][i]-source_bounds[k][i])
                    for k in ['min','max'] for i in range(3))
        group_errors = {}
        for group in GROUPS:
            if group not in assembly_map or group not in per_group:
                group_errors[group] = None
            else:
                b = bounds(assembly_map[group]); a=per_group[group]
                group_errors[group] = max(abs(b[k][i]-a[k][i]) for k in ['min','max'] for i in range(3))
        triangles = tri_count(meshes)
        good = (triangles==source_triangles and set(assembly_map)==set(GROUPS)
                and error<1e-5 and all(e is not None and e<1e-5 for e in group_errors.values()))
        note(kind+'_roundtrip', good, triangles=triangles,
             expected_triangles=source_triangles, assemblies=sorted(str(g) for g in assembly_map),
             world_bounds=imported_bounds, max_position_error_m=error,
             group_position_error_m=group_errors, tolerance_m=1e-5)
    report['status'] = 'pass' if not report['failures'] else 'failed'
except Exception as e:
    import traceback
    report['status'] = 'error'
    report['exception'] = str(e)
    report['traceback'] = traceback.format_exc()
    print(report['traceback'], flush=True)
finally:
    persist()
    print('DELIVERY_VERIFY_COMPLETE', report['status'], json.dumps(report['failures']), flush=True)
