"""Role-specific manufacturing finishes and authored wear for the game exterior.

Source shaders bake into the existing Body atlas, never extra runtime slots.
"""
import bpy


def make_finish(role, root, contact_mask):
    metal=role in ('receiver','tube','stamping','hardware','bezel')
    # Color is restrained; finish differences primarily live in roughness/normal.
    settings={
        'receiver':((.035,.042,.048),.43,.50,.000022),
        'tube':((.026,.031,.035),.29,.36,.000013),
        'stamping':((.031,.035,.037),.45,.53,.000022),
        'hardware':((.065,.070,.069),.32,.39,.000014),
        'bezel':((.040,.046,.049),.32,.38,.000012),
        'resin':((.010,.013,.015),.39,.53,.000095),
        'traction':((.024,.027,.025),.57,.69,.000090),
        'foreend':((.018,.020,.019),.48,.62,.000120),
        'rubber':((.008,.009,.010),.76,.84,.000050),
    }
    color,roughlo,roughhi,depth=settings[role]
    m=bpy.data.materials.new('Source_Finish_'+role);m.use_nodes=True;m['finish_role']=role
    m.diffuse_color=(*color,1)
    ns,ls=m.node_tree.nodes,m.node_tree.links;p=ns.get('Principled BSDF')
    p.inputs['Metallic'].default_value=.92 if metal else 0
    p.inputs['Specular IOR Level'].default_value=.35
    tex=ns.new('ShaderNodeTexCoord');coord=tex.outputs['Object']

    def math(op,a,b=0):
        n=ns.new('ShaderNodeMath');n.operation=op
        for i,v in enumerate((a,b)):
            if isinstance(v,(int,float)):n.inputs[i].default_value=v
            else:ls.new(v,n.inputs[i])
        return n.outputs[0]

    def remap(value,lo,hi,a=0,b=1):
        n=ns.new('ShaderNodeMapRange');n.clamp=True
        for key,v in [('From Min',lo),('From Max',hi),('To Min',a),('To Max',b)]:n.inputs[key].default_value=v
        ls.new(value,n.inputs[0]);return n.outputs[0]

    def noise(scale,detail=2):
        mapping=ns.new('ShaderNodeVectorMath');mapping.operation='MULTIPLY';mapping.inputs[1].default_value=scale;ls.new(coord,mapping.inputs[0])
        n=ns.new('ShaderNodeTexNoise');n.inputs['Scale'].default_value=1;n.inputs['Detail'].default_value=detail;ls.new(mapping.outputs[0],n.inputs[0]);return n.outputs['Fac']

    def mix(socket,factor,target):
        inp=p.inputs[socket];n=ns.new('ShaderNodeMixRGB')
        if inp.is_linked:ls.new(inp.links[0].from_socket,n.inputs[1])
        else:n.inputs[1].default_value=inp.default_value if socket=='Base Color' else (inp.default_value,)*3+(1,)
        n.inputs[2].default_value=target;ls.new(factor,n.inputs[0]);ls.new(n.outputs[0],inp)

    im=ns.new('ShaderNodeTexImage');im.image=bpy.data.images.load(str(root/'material_sources'/('BluedSteel_Source.png' if metal else 'CharcoalPhenolic_Source.png')),check_existing=True)
    im.projection='BOX';im.projection_blend=.25
    mapping=ns.new('ShaderNodeVectorMath');mapping.operation='SCALE';mapping.inputs[3].default_value=9 if metal else 18
    ls.new(coord,mapping.inputs[0]);ls.new(mapping.outputs[0],im.inputs[0])
    field=remap(im.outputs['Color'],.008,.09)
    tint=ns.new('ShaderNodeValToRGB')
    amplitude=.06 if metal or role=='resin' else .10
    tint.color_ramp.elements[0].color=(*(v*(1-amplitude) for v in color),1)
    tint.color_ramp.elements[1].color=(*(v*(1+amplitude) for v in color),1)
    ls.new(field,tint.inputs[0]);ls.new(tint.outputs[0],p.inputs['Base Color'])
    rough=remap(field,0,1,roughlo,roughhi);ls.new(rough,p.inputs['Roughness'])
    grain=noise((100,2800,2800) if role=='tube' else (750,1400,1900) if metal else (650,1050,850) if role in ('foreend','resin','traction') else (1600,1600,1600))
    # Subtle cast-fiber breakup survives the atlas at ordinary review distance.
    if role in ('foreend','resin','traction'):
        rough=math('ADD',rough,remap(grain,.2,.8,-.055,.055));ls.new(rough,p.inputs['Roughness'])
        rough=math('ADD',rough,remap(noise((55,75,65)),.25,.75,-.065,.065));ls.new(rough,p.inputs['Roughness'])
    grain_strength=.35 if metal else .65
    bump=ns.new('ShaderNodeBump');bump.inputs['Distance'].default_value=depth;bump.inputs['Strength'].default_value=grain_strength
    ls.new(grain,bump.inputs['Height']);ls.new(bump.outputs[0],p.inputs['Normal'])
    if role=='traction':
        # Fine molded diagonals, deliberately interrupted by the fiber surface.
        dot=ns.new('ShaderNodeVectorMath');dot.operation='DOT_PRODUCT';dot.inputs[1].default_value=(4300,0,-2300);ls.new(coord,dot.inputs[0])
        rib=remap(math('SINE',dot.outputs['Value']),.50,.92)
        rib=math('MULTIPLY',rib,remap(noise((320,480,560)),.33,.68,.08,1))
        molded=ns.new('ShaderNodeBump');molded.inputs['Distance'].default_value=.000040;molded.inputs['Strength'].default_value=.42
        ls.new(rib,molded.inputs['Height']);ls.new(bump.outputs[0],molded.inputs['Normal']);ls.new(molded.outputs[0],p.inputs['Normal'])

    sites={
        'receiver':[((-.052,.0175,.017),(.002,.003,.0015)),((.077,.0175,.053),(.019,.003,.0008)),((-.018,.0175,.003),(.014,.003,.0008))],
        'tube':[((.471,.012,.047),(.020,.003,.00065))],
        'stamping':[((.365,.023,.066),(.018,.003,.0012)),((.409,.030,.057),(.006,.004,.006))],
        'hardware':[((.524,.010,.015),(.004,.004,.003)),((.008,.022,.0148),(.010,.003,.0008)),((-.009,.005,-.031),(.016,.003,.0007))],
        'bezel':[((-.037,.011,.060),(.002,.003,.002))],
        'resin':[((-.067,.016,-.028),(.009,.004,.016)),((-.107,.016,-.090),(.009,.004,.010))],
        'traction':[((-.074,.0167,-.043),(.008,.003,.014)),((-.092,.0167,-.072),(.010,.003,.013))],
        'foreend':[((.215,.0205,.015),(.034,.003,.009)),((.291,.019,.003),(.009,.004,.012))],
        'rubber':[((.424,.028,-.020),(.004,.005,.017))],
    }[role]
    contact=contact_mask(m,coord,sites)
    # Every visible chip is tied to a physical contact zone. No global scratch field.
    broken=remap(noise((190,850,2900)),.38,.65)
    wear=math('MULTIPLY',contact,broken)
    if metal:
        wear=remap(wear,.24,.63,0,.66)
        mix('Base Color',wear,(.12,.13,.125,1))
        mix('Roughness',wear,(.29,.29,.29,1))
    else:
        wear=math('MULTIPLY',contact,remap(field,0,1,.65,1))
        # Dense resin polishes; fiber and rubber do not turn into exposed steel.
        mix('Base Color',math('MULTIPLY',wear,.25),(.021,.024,.023,1))
        target=.68 if role=='rubber' else .40 if role=='traction' else .30
        mix('Roughness',wear,(target,target,target,1))
        strength=remap(wear,0,1,grain_strength,.18);ls.new(strength,bump.inputs['Strength'])
    wear.node.label='WearMask'
    ao=ns.new('ShaderNodeAmbientOcclusion');ao.inputs['Distance'].default_value=.002;ao.samples=16
    dirt=math('MULTIPLY',math('SUBTRACT',1,ao.outputs['AO']),.52)
    dirt=math('MULTIPLY',dirt,math('SUBTRACT',1,wear));dirt.node.label='DirtMask'
    mix('Base Color',dirt,(.008,.007,.005,1));mix('Roughness',dirt,(.77,.77,.77,1))
    if metal:mix('Metallic',dirt,(.28,.28,.28,1))
    if role=='receiver':
        # One small factory stamp below the formed shoulder, only on the left.
        xyz=ns.new('ShaderNodeSeparateXYZ');ls.new(coord,xyz.inputs[0])
        uv=ns.new('ShaderNodeCombineXYZ')
        ls.new(math('DIVIDE',math('SUBTRACT',.013,xyz.outputs['X']),.016),uv.inputs[0])
        ls.new(math('DIVIDE',math('SUBTRACT',xyz.outputs['Z'],.0258),.002),uv.inputs[1])
        stamp=ns.new('ShaderNodeTexImage');stamp.image=bpy.data.images.load(str(root/'material_sources/Receiver_Stamp_M.png'),check_existing=True)
        stamp.image.colorspace_settings.name='Non-Color';stamp.extension='CLIP';ls.new(uv.outputs[0],stamp.inputs[0])
        ink=math('MULTIPLY',stamp.outputs['Color'],math('GREATER_THAN',xyz.outputs['Y'],.017))
        mix('Base Color',math('MULTIPLY',ink,.52),(.085,.091,.090,1))
        mix('Roughness',ink,(.47,.47,.47,1))
        engraving=ns.new('ShaderNodeBump');engraving.invert=True;engraving.inputs['Distance'].default_value=.000025;engraving.inputs['Strength'].default_value=.35
        ls.new(ink,engraving.inputs['Height']);ls.new(p.inputs['Normal'].links[0].from_socket,engraving.inputs['Normal']);ls.new(engraving.outputs[0],p.inputs['Normal'])
    m['manufacturing_finish']=role
    return m


def assign_finishes(parts,counter,root,contact_mask):
    mats={role:make_finish(role,root,contact_mask) for role in
          ('receiver','tube','stamping','hardware','bezel','resin','traction','foreend','rubber')}
    for ob in parts:
        if ob==counter:continue
        name=ob.name;old=ob.data.materials[0]
        if name=='Pistol_grip':role='resin'
        elif name.startswith('Grip_checkered_phenolic'):role='traction'
        elif name=='Foreend_sculpted_ribs' or name.startswith('Foreend_insert_border'):role='foreend'
        elif name=='Folded_stock_heel_pad' or name.startswith('Foreend_rear_recess'):role='rubber'
        elif name in ('Receiver_shell','Loading_lid'):role='receiver'
        elif name in ('Upper_magazine','Lower_barrel','Barrel_perforated_sleeve'):role='tube'
        elif name.startswith('Folded_stock') or name.startswith('Foreend_phenolic'):role='stamping'
        elif name.startswith('Counter_') and 'gasket' not in name.lower() and 'seal' not in name.lower():role='bezel'
        elif old and old.get('kind')=='steel':role='hardware'
        else:continue
        ob.data.materials.clear();ob.data.materials.append(mats[role]);ob['finish_role']=role
    return mats
