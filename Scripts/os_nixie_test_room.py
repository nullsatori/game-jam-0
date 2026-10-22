"""Place the Nixie station and the RMB-93 pickup in the test room.

Run place() in Lvl_OS_TestRoom, then save that level. Also called by its builder.
Only these named test actors are changed; repeated runs do not add duplicates.
"""
import unreal

MAP='/Game/OS/Maps/Lvl_OS_TestRoom'
WEAPON='/Game/OSArt/Weapons/RMB93/Meshes/SM_OS_RMB93_'

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

    # Its own pickup beside the three guns; the plain shotgun's stays as it is. The RMB-93 itself is
    # defined in code (OSMakeRmb93): the pickup only says which gun, so tuning it never needs this map rebuilt.
    pickup=spawn('Pickup_RMB93',unreal.OSWeaponPickup,(1360,120,110.3),(1,1,1),unreal.Rotator(yaw=90))
    pickup.set_editor_property('weapon_index',1)
    pickup.set_editor_property('gun',unreal.OSPickupGun.RMB93)
    pickup.set_editor_property('action_label',unreal.Text('Взять Рысь'))
    pickup.get_editor_property('mesh').set_static_mesh(unreal.load_asset(WEAPON+'World'))
    stand('RMB93_InspectionStand',(1360,140,50),(.9,.6,1))
    # Labelled like the three guns beside it (build_test_room.label_text).
    tag=spawn('Label_RMB-93',unreal.TextRenderActor,(1360,120,160),(1,1,1),unreal.Rotator(yaw=90))
    tag_text=tag.get_editor_property('text_render')
    tag_text.set_editor_property('text',unreal.Text('RMB-93'))
    tag_text.set_editor_property('world_size',18.0)
    tag_text.set_editor_property('horizontal_alignment',unreal.HorizTextAligment.EHTA_CENTER)
    tag_text.set_editor_property('text_render_color',unreal.Color(r=200,g=200,b=170,a=255))

    bp=unreal.EditorAssetLibrary.load_blueprint_class('/Game/OSArt/Materials/Instruments/Nixie/BP_OS_NixieDisplay')
    assert bp
    display=spawn('Nixie_TestDisplay',bp,(910,331.5,125),(10,10,10),unreal.Rotator(yaw=90))
    counter=display.get_component_by_class(unreal.OSNixieCounterComponent)
    counter.set_editor_property('digit',4)
    counter.set_editor_property('counter_enabled',True)
    stand('Nixie_TestStand',(910,350,70),(.32,.36,1.4))
    label=spawn('Nixie_TestLabel',unreal.TextRenderActor,(910,330,159),(1,1,1),unreal.Rotator(yaw=-90))
    text=label.get_editor_property('text_render')
    text.set_editor_property('text',unreal.Text('NIXIE'))
    text.set_editor_property('world_size',8.0)
    text.set_editor_property('horizontal_alignment',unreal.HorizTextAligment.EHTA_CENTER)
    return {'map':MAP,'pickup':pickup.get_path_name(),'display':display.get_path_name()}
