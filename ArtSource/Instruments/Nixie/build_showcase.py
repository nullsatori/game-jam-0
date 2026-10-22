"""Save a small reusable display Blueprint; uses only engine Plane + shared MI."""
import unreal

def build():
    folder='/Game/JamArt/Materials/Instruments/Nixie'
    path=folder+'/BP_Jam_NixieDisplay'
    eal=unreal.EditorAssetLibrary
    if eal.does_asset_exist(path):
        return {'blueprint':path,'existing':True}
    assert not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor()
    factory=unreal.BlueprintFactory()
    factory.set_editor_property('parent_class',unreal.Actor)
    bp=unreal.AssetToolsHelpers.get_asset_tools().create_asset('BP_Jam_NixieDisplay',folder,unreal.Blueprint,factory)
    assert bp
    subsystem=unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
    root=subsystem.k2_gather_subobject_data_for_blueprint(bp)[0]
    def add(cls,name):
        args=unreal.AddNewSubobjectParams()
        args.set_editor_property('parent_handle',root)
        args.set_editor_property('new_class',cls)
        args.set_editor_property('blueprint_context',bp)
        handle,reason=subsystem.add_new_subobject(args)
        assert not str(reason),str(reason)
        subsystem.rename_subobject_member_variable(bp,handle,name)
        data=subsystem.k2_find_subobject_data_from_handle(handle)
        return unreal.SubobjectDataBlueprintFunctionLibrary.get_object_for_blueprint(data,bp)
    mesh=add(unreal.StaticMeshComponent,'DisplaySurface')
    assert mesh
    mesh.set_static_mesh(unreal.load_asset('/Engine/BasicShapes/Plane'))
    mesh.set_material(0,unreal.load_asset(folder+'/MI_Jam_Nixie_RMB93'))
    mesh.set_mobility(unreal.ComponentMobility.MOVABLE)
    mesh.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
    mesh.set_cast_shadow(False)
    mesh.set_editor_property('relative_scale3d',unreal.Vector(.024,.018,1))
    mesh.set_editor_property('relative_rotation',unreal.MathLibrary.make_rot_from_xz(unreal.Vector(0,1,0),unreal.Vector(-1,0,0)))
    counter=add(unreal.JamNixieCounterComponent,'NixieCounter')
    reference=unreal.ComponentReference()
    reference.set_editor_property('component_property','DisplaySurface')
    counter.set_editor_property('target_mesh',reference)
    counter.set_editor_property('material_slots',[0])
    counter.set_editor_property('digit',4)
    counter.set_editor_property('counter_enabled',True)
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    status=str(bp.get_editor_property('status'))
    assert 'ERROR' not in status,status
    assert eal.save_loaded_asset(bp,False)
    return {'blueprint':bp.get_path_name(),'status':status,'surface_cm':[2.4,1.8],
        'front_normal':'Actor -X; U +Y, V -Z','mesh':'/Engine/BasicShapes/Plane',
        'material':folder+'/MI_Jam_Nixie_RMB93','counter_class':'/Script/MindKore.JamNixieCounterComponent',
        'target_component':'DisplaySurface','digit':4,'enabled':True,'preview_dependency':False}
