"""Import the approved RMB93 exports into a permanent game asset folder.

The loading lid is its own mesh (SM_Jam_RMB93_Lid, its origin on the hinge): run
Scripts/blender/jam_rmb93_split.py on ArtSource/Weapons/RMB93/v02 first. The first run after that
re-imports ViewBody over the old one, which still had the lid joined into it.
"""
from pathlib import Path
import unreal

DEST = '/Game/JamArt/Weapons/RMB93'

def build():
    eal = unreal.EditorAssetLibrary
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    mel = unreal.MaterialEditingLibrary
    meshes = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
    root = Path(unreal.Paths.project_dir())/'ArtSource/Weapons/RMB93/v02'
    assert not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor()

    def imported(path, folder, options=None, replace=False):
        target = DEST+'/'+folder+'/'+path.stem
        if eal.does_asset_exist(target) and not replace:
            return unreal.load_asset(target)
        task = unreal.AssetImportTask()
        for k,v in dict(filename=str(path), destination_path=DEST+'/'+folder,
                        automated=True, save=False, replace_existing=replace).items():
            task.set_editor_property(k,v)
        if options:
            task.set_editor_property('options',options)
        tools.import_asset_tasks([task])
        paths = task.get_editor_property('imported_object_paths')
        assert len(paths)==1, paths
        return unreal.load_asset(paths[0])

    texture = {}
    for suffix in ('BC','N','ORM'):
        t = imported(root/f'textures/T_Jam_RMB93_{suffix}.png','Textures')
        t.set_editor_property('srgb',suffix=='BC')
        t.set_editor_property('compression_settings',getattr(unreal.TextureCompressionSettings,
            {'BC':'TC_DEFAULT','N':'TC_NORMALMAP','ORM':'TC_MASKS'}[suffix]))
        if suffix=='N': t.set_editor_property('flip_green_channel',False)
        assert eal.save_loaded_asset(t,False)
        texture[suffix]=t
    body = unreal.load_asset(DEST+'/Materials/M_Jam_RMB93_Body')
    if not body:
        body = tools.create_asset('M_Jam_RMB93_Body',DEST+'/Materials',unreal.Material,unreal.MaterialFactoryNew())
        for suffix, links in {'BC':[('RGB','MP_BASE_COLOR')], 'N':[('RGB','MP_NORMAL')],
                              'ORM':[('R','MP_AMBIENT_OCCLUSION'),('G','MP_ROUGHNESS'),('B','MP_METALLIC')]}.items():
            n=mel.create_material_expression(body,unreal.MaterialExpressionTextureSampleParameter2D)
            n.set_editor_property('parameter_name',suffix)
            n.set_editor_property('texture',texture[suffix])
            n.set_editor_property('sampler_type',getattr(unreal.MaterialSamplerType,
                {'BC':'SAMPLERTYPE_COLOR','N':'SAMPLERTYPE_NORMAL','ORM':'SAMPLERTYPE_MASKS'}[suffix]))
            for channel, prop in links:
                assert mel.connect_material_property(n,channel,getattr(unreal.MaterialProperty,prop))
        mel.recompile_material(body)
        assert eal.save_loaded_asset(body,False)
    counter=unreal.load_asset('/Game/JamArt/Materials/Instruments/Nixie/MI_Jam_Nixie_RMB93')
    assert counter
    result={}
    flag='Interchange.FeatureFlags.Import.FBX'
    old=unreal.SystemLibrary.get_console_variable_bool_value(flag)
    unreal.SystemLibrary.execute_console_command(None,flag+' 0')
    # No lid asset yet: the ViewBody there is the old one with the lid joined in (the split takes it out).
    split = not eal.does_asset_exist(DEST+'/Meshes/SM_Jam_RMB93_Lid')
    try:
        for group in ('ViewBody','Action','Lid','World'):
            ui=unreal.FbxImportUI()
            for k,v in dict(import_mesh=True,import_as_skeletal=False,import_materials=False,
                            import_textures=False,import_animations=False,
                            mesh_type_to_import=unreal.FBXImportType.FBXIT_STATIC_MESH).items():
                ui.set_editor_property(k,v)
            data=ui.get_editor_property('static_mesh_import_data')
            for k,v in dict(combine_meshes=True,remove_degenerates=False,generate_lightmap_u_vs=False,
                            auto_generate_collision=False,build_nanite=False,convert_scene=True,
                            convert_scene_unit=True,force_front_x_axis=True,import_uniform_scale=1.0,
                            import_rotation=unreal.Rotator(yaw=90),
                            normal_import_method=unreal.FBXNormalImportMethod.FBXNIM_IMPORT_NORMALS).items():
                data.set_editor_property(k,v)
            mesh=imported(root/f'exports/SM_Jam_RMB93_{group}.fbx','Meshes',ui,replace=split and group=='ViewBody')
            slots=mesh.get_editor_property('static_materials')
            assert len(slots)==(1 if group in ('Action','Lid') else 2), (group,len(slots))
            for i,slot in enumerate(slots):
                mesh.set_material(i,counter if str(slot.material_slot_name)=='Counter' else body)
            if group=='ViewBody': assert str(slots[1].material_slot_name)=='Counter'
            settings=meshes.get_lod_build_settings(mesh,0)
            for k,v in dict(remove_degenerates=False,recompute_normals=False,recompute_tangents=True,use_mikk_t_space=True).items():
                settings.set_editor_property(k,v)
            meshes.set_lod_build_settings(mesh,0,settings)
            if group=='World' and meshes.get_simple_collision_count(mesh)==0:
                meshes.add_simple_collisions(mesh,unreal.ScriptingCollisionShapeType.NDOP26)
            assert eal.save_loaded_asset(mesh,False)
            result[group]={'path':mesh.get_path_name(),'triangles':mesh.get_num_triangles(0),'bounds':str(mesh.get_bounding_box())}
        # The lid cut out of the body: the three view parts together are the whole gun.
        assert result['ViewBody']['triangles']+result['Action']['triangles']+result['Lid']['triangles']==11846,result
        assert result['World']['triangles']==11846,result
    finally:
        unreal.SystemLibrary.execute_console_command(None,flag+' '+str(int(old)))
    return result
