"""Build the reusable project Nixie material and RMB93 artist preset in UE Python.

Run build() inside Unreal Python after build_sdf.py with NumPy/Pillow.
Only /Game/JamArt/Materials/Instruments/Nixie is written. Inputs are canonical
ArtSource files; no preview asset dependency. No maps or project settings change.
"""
from pathlib import Path
import hashlib
import unreal

DEFAULTS = {
    'SP_DigitIndex': 4., 'SP_CounterEnabled': 1.,
    # v5 event clocks: written by UJamNixieCounterComponent once per event, never per frame.
    'SP_PrevDigit': 10., 'SP_ChangeTime': -1000., 'SP_PowerTime': -1000.,
    'SP_DepthCm': .11, 'SP_StackSpacingCm': .032, 'SP_FaceWidthCm': 2.4, 'SP_FaceHeightCm': 1.8,
    'SP_GhostStrength': .06, 'SP_CoreIntensity': 4.5,
    'SP_DischargeIntensity': 2.4, 'SP_HaloIntensity': 1.0,
    'SP_FlickerStrength': .006, 'SP_GlassCurvature': .10,
    'SP_ClearCoat': .2, 'SP_ClearCoatRoughness': .025,
    'SP_InternalRadiance': 4.0, 'SP_GlassEchoStrength': .07,
    'SP_EdgeRadiance': .6, 'SP_CavityRoughness': .65,
    'SP_SegmentFlickerStrength': .14, 'SP_SegmentFlickerRate': 2.5,
    'SP_SegmentFlickerScale': 14., 'SP_FlickerPhase': 0.,
    'SP_MinCorePixels': .85,
    'SP_CathodeShadow': .85, 'SP_CathodeBounce': 1.4,
    'SP_MeshCellCm': .085, 'SP_MeshWireCm': .0055, 'SP_MeshOpacity': 1., 'SP_MeshGlow': .5,
    'SP_DustScatter': 1.2,
    # v6 (owner 22.10): slow enough to read at a 75 px face in HIP, bright enough for bloom; the change's
    # blink and the orange-to-red cooling are in the shader. v5 was .09 / .045 / .6 / .30.
    'SP_IgniteSeconds': .28, 'SP_AfterglowSeconds': .35, 'SP_StrikeOvershoot': 1.2, 'SP_WarmupSeconds': .6,
    # A lit 0 blinks at this rate once ignited (the empty magazine); 0: steady. The RMB93 instance sets 1.4.
    'SP_EmptyBlinkHz': 0.,
}

def build(destination='/Game/JamArt/Materials/Instruments/Nixie'):
    assert destination=='/Game/JamArt/Materials/Instruments/Nixie', 'Owned feature package only'
    assert not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor()
    assert not unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()
    dirty = unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()
    assert all(p.get_name().startswith(destination+'/') for p in dirty), 'Unrelated dirty package'
    mel = unreal.MaterialEditingLibrary
    eal=unreal.EditorAssetLibrary
    tools=unreal.AssetToolsHelpers.get_asset_tools()
    mat=unreal.load_asset(destination+'/M_Jam_Nixie') if eal.does_asset_exist(destination+'/M_Jam_Nixie') else tools.create_asset('M_Jam_Nixie',destination,unreal.Material,unreal.MaterialFactoryNew())
    assert mat
    textures={}
    for key,name in {
        'Masks':'T_Jam_RMB93_Counter_Masks',
        'BackColor':'T_Jam_RMB93_Counter_BC', 'Normal':'T_Jam_RMB93_Counter_N',
        'ORM':'T_Jam_RMB93_Counter_ORM'}.items():
        target=destination+'/Textures/T_Jam_Nixie_'+key
        if not eal.does_asset_exist(target):
            source=Path(__file__).resolve().parents[2]/'Weapons/RMB93/v02/textures'/(name+'.png')
            task=unreal.AssetImportTask()
            for prop,value in {'filename':str(source),'destination_path':destination+'/Textures','destination_name':'T_Jam_Nixie_'+key,'automated':True,'save':False}.items():task.set_editor_property(prop,value)
            tools.import_asset_tasks([task])
        textures[key]=unreal.load_asset(target)
        textures[key].set_editor_property('srgb',key=='BackColor')
        textures[key].set_editor_property('compression_settings',unreal.TextureCompressionSettings.TC_NORMALMAP if key=='Normal' else unreal.TextureCompressionSettings.TC_DEFAULT if key=='BackColor' else unreal.TextureCompressionSettings.TC_MASKS)
    target=destination+'/Textures/T_Jam_Nixie_Distance'
    task=unreal.AssetImportTask()
    for key,value in {'filename':str(Path(__file__).with_name('T_Jam_Nixie_Distance.png')),'destination_path':destination+'/Textures','automated':True,'save':False,'replace_existing':True,'replace_existing_settings':True}.items():task.set_editor_property(key,value)
    tools.import_asset_tasks([task])
    textures['Atlas']=unreal.load_asset(target)
    sdf=textures['Atlas']
    sdf.set_editor_property('compression_settings',unreal.TextureCompressionSettings.TC_VECTOR_DISPLACEMENTMAP)
    sdf.set_editor_property('srgb',False)
    sdf.set_editor_property('mip_gen_settings',unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
    sdf.set_editor_property('filter',unreal.TextureFilter.TF_BILINEAR)
    sdf.set_editor_property('never_stream',True)
    sdf.set_editor_property('address_x',unreal.TextureAddress.TA_CLAMP)
    sdf.set_editor_property('address_y',unreal.TextureAddress.TA_CLAMP)
    assert all(textures.values())
    mel.delete_all_material_expressions(mat)
    mat.set_editor_property('use_material_attributes',True)
    mat.set_editor_property('shading_model',unreal.MaterialShadingModel.MSM_CLEAR_COAT)
    # The glow changes in place (ignition, afterglow, warm-up) and the cathodes sit behind the face (parallax):
    # neither is in the velocity buffer, so without this TSR smears the digit on a draw or a turn (owner 22.10).
    mat.set_editor_property('has_pixel_animation',True)
    index = 0
    def node(kind, **properties):
        nonlocal index
        result = mel.create_material_expression(mat,getattr(unreal,kind),-1800+(index%6)*260,(index//6)*250)
        index += 1
        for key,value in properties.items(): result.set_editor_property(key,value)
        return result
    def link(a,out,b,pin):
        assert mel.connect_material_expressions(a,out,b,pin), (out,pin)
    def custom(code, inputs, outputs=None):
        fields=[]
        for name in inputs:
            item=unreal.CustomInput(); item.set_editor_property('input_name',name); fields.append(item)
        extra=[]
        for name in outputs or []:
            item=unreal.CustomOutput(); item.set_editor_property('output_name',name)
            item.set_editor_property('output_type',unreal.CustomMaterialOutputType.CMOT_FLOAT3); extra.append(item)
        expr=node('MaterialExpressionCustom',code=code,inputs=fields,additional_outputs=extra,output_type=unreal.CustomMaterialOutputType.CMOT_FLOAT3)
        for name,(src,out) in inputs.items(): link(src,out,expr,name)
        return expr
    attrs=node('MaterialExpressionMakeMaterialAttributes')
    assert mel.connect_material_property(attrs,'',unreal.MaterialProperty.MP_MATERIAL_ATTRIBUTES)
    params={name:node('MaterialExpressionScalarParameter',parameter_name=name,default_value=value,group='Nixie') for name,value in DEFAULTS.items()}
    colors={name:node('MaterialExpressionVectorParameter',parameter_name='VP_'+name,default_value=unreal.LinearColor(*value,1.),group='Nixie') for name,value in {'CoreColor':(1.,.27,.045),'DischargeColor':(1.,.065,.002),'HaloColor':(1.,.025,.001)}.items()}
    uv=node('MaterialExpressionTextureCoordinate')
    objects={}
    for key in ['Atlas','Masks','BackColor']:
        sampler=unreal.MaterialSamplerType.SAMPLERTYPE_COLOR if key=='BackColor' else unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR if key=='Atlas' else unreal.MaterialSamplerType.SAMPLERTYPE_MASKS
        objects[key]=node('MaterialExpressionTextureObjectParameter',parameter_name=key,texture=textures[key],sampler_type=sampler)
    normal=node('MaterialExpressionTextureSampleParameter2D',parameter_name='Normal',texture=textures['Normal'],sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL)
    orm=node('MaterialExpressionTextureSampleParameter2D',parameter_name='ORM',texture=textures['ORM'],sampler_type=unreal.MaterialSamplerType.SAMPLERTYPE_MASKS)
    shader=Path(__file__).with_name('UE_Nixie.usf').read_text(encoding='utf-8')
    inputs={name:(obj,'') for name,obj in objects.items()}
    inputs.update(UV=(uv,''),WorldPos=(node('MaterialExpressionWorldPosition'),''),CameraVector=(node('MaterialExpressionCameraVectorWS'),''),TimeSeconds=(node('MaterialExpressionTime'),''))
    inputs.update({name:(expr,'RGB') for name,expr in colors.items()})
    for local,param in {'Digit':'DigitIndex','Enabled':'CounterEnabled','DepthCm':'DepthCm','WidthCm':'FaceWidthCm','HeightCm':'FaceHeightCm','GhostStrength':'GhostStrength','CoreIntensity':'CoreIntensity','DischargeIntensity':'DischargeIntensity','HaloIntensity':'HaloIntensity','FlickerStrength':'FlickerStrength'}.items():
        inputs[local]=(params['SP_'+param],'')
    for local in ['InternalRadiance','GlassEchoStrength','EdgeRadiance','MinCorePixels']:inputs[local]=(params['SP_'+local],'')
    for local in ['PrevDigit','ChangeTime','PowerTime','StackSpacingCm','CathodeShadow','CathodeBounce','MeshCellCm','MeshWireCm','MeshOpacity','MeshGlow','DustScatter','IgniteSeconds','AfterglowSeconds','StrikeOvershoot','WarmupSeconds','EmptyBlinkHz']:inputs[local]=(params['SP_'+local],'')
    for local,param in {'SegmentStrength':'SegmentFlickerStrength','SegmentRate':'SegmentFlickerRate','SegmentScale':'SegmentFlickerScale','FlickerPhase':'FlickerPhase'}.items():inputs[local]=(params['SP_'+param],'')
    optical=custom(shader,inputs,['LayeredBaseColor'])
    link(optical,'',attrs,'EmissiveColor'); link(optical,'LayeredBaseColor',attrs,'BaseColor')
    lens=custom('return normalize(float3(N.xy*.45+(UV-.5)*Curvature,max(N.z,.1)));',{'N':(normal,'RGB'),'UV':(uv,''),'Curvature':(params['SP_GlassCurvature'],'')})
    link(lens,'',attrs,'Normal')
    # Current project r.ClearCoatNormal=0: retain that setting. This curved optical
    # normal drives both physical lobes; parallax cathode coordinates remain flat.
    link(params['SP_CavityRoughness'],'',attrs,'Roughness')
    link(orm,'R',attrs,'AmbientOcclusion'); link(orm,'B',attrs,'Metallic')
    link(params['SP_ClearCoat'],'',attrs,'ClearCoat');link(params['SP_ClearCoatRoughness'],'',attrs,'ClearCoatRoughness')
    # Suppress the broad substrate lobe; the outer physical clearcoat keeps the
    # polished reflection. No global separate-bottom-normal setting required.
    link(node('MaterialExpressionConstant',r=0.),'',attrs,'Specular')
    errors=list(mel.recompile_material(mat))
    assert not errors, errors
    mi_path=destination+'/MI_Jam_Nixie_RMB93'
    mi=unreal.load_asset(mi_path) if eal.does_asset_exist(mi_path) else tools.create_asset('MI_Jam_Nixie_RMB93',destination,unreal.MaterialInstanceConstant,unreal.MaterialInstanceConstantFactoryNew())
    mel.set_material_instance_parent(mi,mat)
    mel.set_material_instance_scalar_parameter_value(mi,'SP_EmptyBlinkHz',1.4) # the weapon's 0 is an empty magazine
    for asset in [*textures.values(),mat,mi]:assert eal.save_loaded_asset(asset,False)
    return {'material':mat.get_path_name(),'instance':mi.get_path_name(),'shader':'UE nixie v6: ten-cathode stack, hex anode mesh, gas volume, slow event-clock ignition and cooling','compile_errors':errors,'parameters':DEFAULTS,
        'textures':{name:tex.get_path_name() for name,tex in textures.items()},
        'shader_sha256':hashlib.sha256(shader.encode()).hexdigest(),'clearcoat_bottom_normal_cvar':unreal.SystemLibrary.get_console_variable_int_value('r.ClearCoatNormal'),
        'limitations':['Opaque virtual optical depth: layers are found along the view ray, no transparent shell','One curved optical normal; substrate specular suppressed, outer clearcoat supplies reflection','Runtime binding belongs to UJamNixieCounterComponent; material generation replaces no game weapon']}
