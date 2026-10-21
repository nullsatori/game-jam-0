"""Place the Nixie station and configure the existing test-room shotgun pickup.

Run place() in Lvl_Jam_TestRoom, then save that level. Also called by its builder.
Only these named test actors are changed; repeated runs do not add duplicates.
"""
import unreal

MAP='/Game/Jam/Maps/Lvl_Jam_TestRoom'
WEAPON='/Game/JamArt/Weapons/RMB93/Meshes/SM_Jam_RMB93_'

def place():
    eas=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    world=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    assert world.get_outer().get_path_name()==MAP
    assert not unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).is_in_play_in_editor()
    actors=eas.get_all_level_actors()
    def find(label):
        found=[a for a in actors if a.get_actor_label()==label]
        assert len(found)<=1, label
        return found[0] if found else None
    def spawn(label,cls,loc,scale,rotation=unreal.Rotator()):
        actor=find(label)
        if not actor:
            actor=eas.spawn_actor_from_class(cls,unreal.Vector(*loc),rotation)
            actor.set_actor_label(label)
            actors.append(actor)
        actor.set_actor_location(unreal.Vector(*loc),False,False)
        actor.set_actor_rotation(rotation,False)
        actor.set_actor_scale3d(unreal.Vector(*scale))
        actor.set_folder_path('Tests/Nixie_RMB93')
        return actor
    def stand(label,loc,scale):
        a=spawn(label,unreal.StaticMeshActor,loc,scale)
        a.get_component_by_class(unreal.StaticMeshComponent).set_static_mesh(unreal.load_asset('/Engine/BasicShapes/Cube'))
        return a

    pickup=find('Pickup_SHOTGUN')
    assert pickup and isinstance(pickup,unreal.JamWeaponPickup)
    defaults=unreal.get_default_object(unreal.JamZeroGPawn)
    guns=defaults.get_editor_property('guns')
    definition=guns.get_editor_property('weapons')[1]
    original_mesh=definition.get_editor_property('view_mesh')
    definition.set_editor_property('name',unreal.Text('Рысь / RMB93'))
    definition.set_editor_property('view_mesh',unreal.load_asset(WEAPON+'ViewBody'))
    definition.set_editor_property('action_mesh',unreal.load_asset(WEAPON+'Action'))
    definition.set_editor_property('action_pivot',unreal.Vector())
    definition.set_editor_property('action_travel_cm',5.5)
    definition.set_editor_property('nixie_material_slot',1)
    pickup.set_editor_property('weapon_override',definition)
    pickup.set_editor_property('override_weapon',True)
    aim=unreal.get_default_object(unreal.JamScopeComponent).get_editor_property('aims')[1]
    aim.set_editor_property('sight_rear_cm',unreal.Vector(11.5,0,7.4))
    aim.set_editor_property('sight_front_cm',unreal.Vector(51.5,0,7.4))
    pickup.set_editor_property('aim_override',aim)
    pickup.set_editor_property('override_aim',True)
    pickup.set_editor_property('action_label',unreal.Text('Взять Рысь / RMB93'))
    pickup.get_editor_property('mesh').set_static_mesh(unreal.load_asset(WEAPON+'World'))
    pickup.set_actor_scale3d(unreal.Vector(1,1,1))
    pickup.set_actor_location(unreal.Vector(1120,120,110.3),False,False)
    pickup.set_actor_rotation(unreal.Rotator(yaw=90),False)
    pickup.set_folder_path('Tests/Nixie_RMB93')
    assert guns.get_editor_property('weapons')[1].get_editor_property('view_mesh')==original_mesh, 'CDO changed'
    stand('RMB93_InspectionStand',(1120,140,50),(.9,.6,1))

    bp=unreal.EditorAssetLibrary.load_blueprint_class('/Game/JamArt/Materials/Instruments/Nixie/BP_Jam_NixieDisplay')
    assert bp
    display=spawn('Nixie_TestDisplay',bp,(910,331.5,125),(10,10,10),unreal.Rotator(yaw=90))
    counter=display.get_component_by_class(unreal.JamNixieCounterComponent)
    counter.set_editor_property('digit',4)
    counter.set_editor_property('counter_enabled',True)
    stand('Nixie_TestStand',(910,350,70),(.32,.36,1.4))
    label=spawn('Nixie_TestLabel',unreal.TextRenderActor,(910,330,159),(1,1,1),unreal.Rotator(yaw=-90))
    text=label.get_editor_property('text_render')
    text.set_editor_property('text',unreal.Text('NIXIE'))
    text.set_editor_property('world_size',8.0)
    text.set_editor_property('horizontal_alignment',unreal.HorizTextAligment.EHTA_CENTER)
    return {'map':MAP,'pickup':pickup.get_path_name(),'display':display.get_path_name(),
            'weapon_slot':1,'pump_travel_cm':5.5,'counter_slot':1,'default_weapon_unchanged':True}
