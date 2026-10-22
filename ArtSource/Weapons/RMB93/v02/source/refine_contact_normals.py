"""Analytic split normals for the three bevelled contact pieces.

Call after geometry modifiers are applied and before the generic Weighted
Normal branch. Returns True only for handled objects. No geometry is changed.
"""
import math
from mathutils import Vector

CONTACT_AXES = {
    'Pistol_grip': Vector((0, 1, 0)),
    'Trigger_guard': Vector((0, 1, 0)),
    'Counter_recess_rim': Vector((-.8, 0, .6)),
}


def refine_contact_normals(ob):
    if ob.type != 'MESH' or ob.name not in CONTACT_AXES:
        return False
    me = ob.data
    me.update()
    axis = CONTACT_AXES[ob.name]
    adjacent = [[] for _ in me.vertices]
    categories = []
    for face in me.polygons:
        projection = abs(face.normal.dot(axis))
        categories.append('cap' if projection > .9999 else
                          'wall' if projection < .0001 else 'bevel')
        indices = list(face.vertices)
        for i, vi in enumerate(indices):
            v = me.vertices[vi].co
            a = me.vertices[indices[i-1]].co-v
            b = me.vertices[indices[(i+1)%len(indices)]].co-v
            angle = a.angle(b, 0.0)
            adjacent[vi].append((face.index, max(angle, 1e-6)))

    def average(entries, reference):
        normal = Vector((0, 0, 0))
        for fi, weight in entries:
            n = me.polygons[fi].normal
            # Keep unrelated sharp contour changes out of the smooth patch.
            if n.dot(reference) > .35:
                normal += n*weight
        return normal.normalized() if normal.length_squared > 1e-16 else reference.copy()

    normals = [(0, 0, 1)]*len(me.loops)
    for face in me.polygons:
        category = categories[face.index]
        face.use_smooth = True
        for li in face.loop_indices:
            vi = me.loops[li].vertex_index
            neighbors = adjacent[vi]
            if category == 'cap':
                normal = axis if face.normal.dot(axis) > 0 else -axis
            elif category == 'wall':
                walls = [(fi,w) for fi,w in neighbors if categories[fi]=='wall']
                normal = average(walls, face.normal)
            else:
                caps = [(fi,w) for fi,w in neighbors if categories[fi]=='cap']
                walls = [(fi,w) for fi,w in neighbors if categories[fi]=='wall']
                if caps:
                    # A real rounded edge is tangent to the planar cap at its
                    # first ring; weighted mesh normals otherwise bend the cap.
                    n = me.polygons[caps[0][0]].normal
                    normal = axis if n.dot(axis) > 0 else -axis
                elif walls:
                    normal = average(walls, face.normal)
                else:
                    bevels = [(fi,w) for fi,w in neighbors if categories[fi]=='bevel']
                    normal = average(bevels, face.normal)
            normals[li] = tuple(normal)
    assert all(all(math.isfinite(c) for c in n) and abs(sum(c*c for c in n)-1)<1e-4
               for n in normals), ob.name
    me.normals_split_custom_set(normals)
    ob['contact_normals_refined'] = True
    ob['normal_method'] = 'Planar caps; tangent bevel rings; angle-weighted wall contour'
    return True
