"""Authored handling damage and residue, baked into the shared game atlas.

No curvature threshold: low-poly triangulation must not become painted patches.
Object-space coordinates survive the combined bake because transforms are applied.
"""


def apply_weathering(material, contact_mask, category='steel'):
    ns, ls = material.node_tree.nodes, material.node_tree.links
    p = ns.get('Principled BSDF')
    coord = next(n for n in ns if n.bl_idname == 'ShaderNodeTexCoord').outputs['Object']

    def math(op, a, b):
        n = ns.new('ShaderNodeMath'); n.operation = op
        for i, v in enumerate((a, b)):
            if isinstance(v, (float, int)): n.inputs[i].default_value = v
            else: ls.new(v, n.inputs[i])
        return n.outputs[0]

    def noise(scale, detail=2):
        v = ns.new('ShaderNodeVectorMath'); v.operation = 'MULTIPLY'
        v.inputs[1].default_value = scale; ls.new(coord, v.inputs[0])
        n = ns.new('ShaderNodeTexNoise'); n.inputs['Scale'].default_value = 1
        n.inputs['Detail'].default_value = detail; ls.new(v.outputs[0], n.inputs[0])
        return n.outputs['Fac']

    def remap(value, lo, hi, outlo=0, outhi=1):
        n = ns.new('ShaderNodeMapRange'); n.clamp = True
        for key, v in [('From Min',lo),('From Max',hi),('To Min',outlo),('To Max',outhi)]:
            n.inputs[key].default_value = v
        ls.new(value,n.inputs[0]); return n.outputs[0]

    def mix(socket, mask, color):
        inp = p.inputs[socket]; old = inp.links[0].from_socket if inp.is_linked else None
        n = ns.new('ShaderNodeMixRGB'); n.inputs[2].default_value = color
        if old: ls.new(old,n.inputs[1])
        else: n.inputs[1].default_value = inp.default_value if socket=='Base Color' else (inp.default_value,)*3+(1,)
        ls.new(mask,n.inputs[0]); ls.new(n.outputs[0],inp)

    steel = category == 'steel'
    # Broad rubbed islands make age readable at the full-weapon viewing distance.
    sites = [
        ((.083,.0175,.052),(.028,.004,.0016)),
        ((-.043,.0175,.018),(.006,.004,.003)),
        ((.018,.0175,.004),(.025,.004,.0015)),
        ((.242,.014,.046),(.065,.004,.004)),
        ((.455,.012,.047),(.032,.005,.004)),
        ((.385,.009,.008),(.045,.004,.002)),
        ((.293,.020,.063),(.045,.004,.0013)),
        ((.125,.020,.064),(.028,.004,.0013)),
        ((.495,.020,.072),(.012,.006,.026)),
        ((-.009,.005,-.031),(.024,.004,.003)),
    ] if steel else [
        ((.249,.0205,.019),(.045,.004,.006)),
        ((.160,.0205,-.010),(.042,.004,.006)),
        ((.301,.018,.007),(.012,.006,.014)),
        ((-.068,.016,-.027),(.012,.008,.020)),
        ((-.106,.017,-.087),(.014,.008,.015)),
        ((-.086,.018,-.058),(.014,.005,.023)),
        ((.495,.020,.072),(.012,.006,.026)),
    ]
    contact = contact_mask(material,coord,sites)
    chips = remap(noise((95,500,2700)),.55 if steel else .43,.69)
    chips = math('MULTIPLY',chips,remap(noise((320,650,450)),.32,.56))
    rubs = math('MULTIPLY',contact, chips)
    # Sparse short streaks, clustered rather than a uniform brushed finish.
    clusters = remap(noise((46,80,62)),.44,.68)
    streaks = remap(noise((210,1700,4900)),.64,.77)
    scratches = math('MULTIPLY',clusters,streaks)
    wear = math('MAXIMUM',math('MULTIPLY',rubs,.78),math('MULTIPLY',scratches,.58 if steel else .32))
    oldwear = next((n.outputs[0] for n in ns if n.label=='WearMask'),None)
    for n in ns:
        if n.label=='WearMask': n.label=''
    if oldwear: wear = math('MAXIMUM',wear,oldwear)
    wear.node.label='WearMask'
    mix('Base Color',wear,(.15,.16,.155,1) if steel else (.048,.049,.043,1))
    mix('Roughness',wear,(.31,.31,.31,1) if steel else (.34,.34,.34,1))

    # Greasy dark residues in real seams plus limited dry deposits on broad planes.
    ao=ns.new('ShaderNodeAmbientOcclusion');ao.inputs['Distance'].default_value=.0035;ao.samples=16
    cavity=math('SUBTRACT',1,ao.outputs['AO'])
    stains=remap(noise((55,85,70),3),.55,.72,0,.30)
    dirt=math('MAXIMUM',math('MULTIPLY',cavity,.65),stains)
    dirt=math('MULTIPLY',dirt,math('SUBTRACT',1,math('MULTIPLY',wear,.80)))
    dirt.node.label='DirtMask'
    mix('Base Color',dirt,(.018,.015,.011,1))
    mix('Roughness',dirt,(.79,.79,.79,1))
    if steel: mix('Metallic',dirt,(.22,.22,.22,1))
    material['weathering']='broken rubbed finish, clustered directional scratches and cavity residue; no rust layer'


def make_grip_material(base, insert=False):
    """Distinct dense resin shell and dry compressed-fiber contact panel."""
    material=base.copy();material.name='Source_GripFiberPanel' if insert else 'Source_GripDenseResin'
    ns,ls=material.node_tree.nodes,material.node_tree.links;p=ns.get('Principled BSDF')
    texture=next(n for n in ns if n.bl_idname=='ShaderNodeTexImage')
    field=ns.new('ShaderNodeMapRange');field.clamp=True
    field.inputs['From Min'].default_value=.008;field.inputs['From Max'].default_value=.09
    ls.new(texture.outputs['Color'],field.inputs[0])
    ramp=ns.new('ShaderNodeValToRGB')
    ramp.color_ramp.elements[0].color=(.013,.014,.013,1) if insert else (.006,.008,.009,1)
    ramp.color_ramp.elements[1].color=(.037,.041,.039,1) if insert else (.021,.025,.027,1)
    ls.new(field.outputs[0],ramp.inputs[0]);ls.new(ramp.outputs[0],p.inputs['Base Color'])
    rough=ns.new('ShaderNodeMapRange')
    rough.inputs['To Min'].default_value=.54 if insert else .30
    rough.inputs['To Max'].default_value=.73 if insert else .52
    ls.new(field.outputs[0],rough.inputs[0]);ls.new(rough.outputs[0],p.inputs['Roughness'])
    bump=next(n for n in ns if n.bl_idname=='ShaderNodeBump')
    # Keep shell resin dense; expose the fiber structure only on the inset panel.
    for link in list(bump.inputs['Strength'].links):ls.remove(link)
    bump.inputs['Strength'].default_value=.70 if insert else .30
    bump.inputs['Distance'].default_value=.00016 if insert else .000035
    p.inputs['Specular IOR Level'].default_value=.36
    return material
