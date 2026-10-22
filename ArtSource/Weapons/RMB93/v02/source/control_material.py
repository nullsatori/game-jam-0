"""Local handling wear for the exterior control cluster, baked into Body."""


def make_control_steel(base, contact_mask):
    material=base.copy();material.name='Source_ControlHandledSteel'
    ns,ls=material.node_tree.nodes,material.node_tree.links
    p=ns.get('Principled BSDF')
    # The receiver's former long lever rub does not belong to the new paddle.
    # Keep its source metal finish, then apply only this cluster's contact map.
    for socket in ('Base Color','Roughness'):
        inherited=p.inputs[socket].links[0].from_node
        assert inherited.label=='Source contact '+socket
        ls.new(inherited.inputs[1].links[0].from_socket,p.inputs[socket])
    bluing=ns.new('ShaderNodeMixRGB');bluing.blend_type='MULTIPLY';bluing.inputs[0].default_value=1
    bluing.inputs[2].default_value=(.72,.75,.78,1)
    ls.new(p.inputs['Base Color'].links[0].from_socket,bluing.inputs[1]);ls.new(bluing.outputs[0],p.inputs['Base Color'])
    coords=next(n for n in ns if n.bl_idname=='ShaderNodeTexCoord').outputs['Object']
    # Wear follows a few contact regions; broad planes retain their bluing.
    contact=contact_mask(material,coords,[
        ((-.009,.0050,-.031),(.021,.003,.0018)),
        ((.019,.0050,-.014),(.003,.003,.007)),
        ((.012,.0220,.0118),(.010,.004,.003)),
        ((-.014,.0030,-.018),(.003,.002,.008)),
    ])
    vector=ns.new('ShaderNodeVectorMath');vector.operation='MULTIPLY'
    vector.inputs[1].default_value=(130,1100,3600);ls.new(coords,vector.inputs[0])
    noise=ns.new('ShaderNodeTexNoise');noise.inputs['Scale'].default_value=1
    noise.inputs['Detail'].default_value=2;ls.new(vector.outputs[0],noise.inputs[0])
    scratches=ns.new('ShaderNodeMapRange');scratches.clamp=True
    scratches.inputs['From Min'].default_value=.64;scratches.inputs['From Max'].default_value=.80
    ls.new(noise.outputs['Fac'],scratches.inputs[0])
    broken=ns.new('ShaderNodeMath');broken.operation='MULTIPLY'
    ls.new(contact,broken.inputs[0]);ls.new(scratches.outputs[0],broken.inputs[1])
    for n in ns:
        if n.label=='WearMask':n.label=''
    wear=ns.new('ShaderNodeMath');wear.operation='MULTIPLY';wear.inputs[1].default_value=.48
    ls.new(broken.outputs[0],wear.inputs[0]);wear.label='WearMask'
    # Strongest rubs remain subdued steel rather than a white painted outline.
    for socket,target in [('Base Color',(.14,.15,.145,1)),('Roughness',(.35,.35,.35,1))]:
        inp=p.inputs[socket];source=inp.links[0].from_socket
        mix=ns.new('ShaderNodeMixRGB');mix.inputs[2].default_value=target
        ls.new(source,mix.inputs[1]);ls.new(wear.outputs[0],mix.inputs[0]);ls.new(mix.outputs[0],inp)
    material['finish_note']='Localized rubbed bluing and short directional contact scuffs; no uniform edge wear or rust'
    return material
