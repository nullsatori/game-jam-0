"""Connect imported D8/D9 leaves to MKDoor and the existing F interaction."""
import unreal


def setup():
    subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    actors = subsystem.get_all_level_actors()
    door_class = unreal.load_class(None, '/Script/MindKore.MKDoor')
    components = [c for a in actors for c in a.get_components_by_class(unreal.StaticMeshComponent)]
    for prefix in ('D8', 'D9'):
        label = 'Airlock_' + prefix
        if any(a.get_actor_label() == label for a in actors):
            raise RuntimeError(label + ' already exists; refusing to duplicate it')
        source = next(c for c in components if c.get_name() == prefix + '_CLOSED_Leaf_0')
        jamb = next(c for c in components if c.get_name() == prefix + '_JambA_0')
        threshold = next(c for c in components if c.get_name() == prefix + '_Threshold_0')
        mesh = source.get_editor_property('static_mesh')
        pose = source.get_world_transform()
        jamb_box = jamb.get_editor_property('static_mesh').get_bounding_box()
        threshold_box = threshold.get_editor_property('static_mesh').get_bounding_box()
        # Mesh vertices are baked in ship space; the hinge belongs at the opening's left jamb.
        hinge_local = unreal.Vector(jamb_box.max.x, (jamb_box.min.y + jamb_box.max.y) / 2, threshold_box.min.z)
        hinge = unreal.MathLibrary.transform_location(pose, hinge_local)
        rotation = unreal.MathLibrary.compose_rotators(unreal.Rotator(yaw=-90), source.get_world_rotation())
        door = subsystem.spawn_actor_from_class(door_class, hinge, rotation)
        door.set_actor_label(label)
        door.set_editor_property('DisplayName', unreal.Text('Дверь шлюза'))
        door.set_editor_property('OpenAngleDeg', 95.0)
        leaf = door.get_editor_property('Leaf')
        leaf.set_static_mesh(mesh)
        leaf.set_world_transform(pose, False, True)
        for slot in range(source.get_num_materials()):
            leaf.set_material(slot, source.get_material(slot))
        # Verify that moving the pivot did not move the closed leaf's geometry.
        bounds = mesh.get_bounding_box()
        for point in [bounds.min, bounds.max]:
            before = unreal.MathLibrary.transform_location(pose, point)
            after = unreal.MathLibrary.transform_location(leaf.get_world_transform(), point)
            assert (before - after).length() < 0.01, label + ': closed pose changed'
        # Thin seam strips are part of the moving leaf, not the stationary frame.
        seams = [c for c in components if c.get_name().startswith(prefix + '_LeafSeam')]
        for index, seam in enumerate(seams):
            detail = subsystem.spawn_actor_from_class(unreal.StaticMeshActor, seam.get_world_location())
            detail.set_actor_label(label + '_Seam_' + str(index))
            detail_component = detail.get_component_by_class(unreal.StaticMeshComponent)
            detail_component.set_mobility(unreal.ComponentMobility.MOVABLE)
            detail_component.set_static_mesh(seam.get_editor_property('static_mesh'))
            detail_component.set_world_transform(seam.get_world_transform(), False, True)
            for slot in range(seam.get_num_materials()):
                detail_component.set_material(slot, seam.get_material(slot))
            detail_component.set_collision_profile_name('NoCollision')
            assert detail_component.attach_to_component(leaf, 'None', unreal.AttachmentRule.KEEP_WORLD,
                unreal.AttachmentRule.KEEP_WORLD, unreal.AttachmentRule.KEEP_WORLD, False)
        for original in [source] + seams:
            original.modify()
            original.set_visibility(False)
            original.set_hidden_in_game(True)
            original.set_collision_profile_name('NoCollision')
        print(label, 'hinge', hinge, 'leaf', mesh.get_path_name())


with unreal.ScopedEditorTransaction('Connect ship airlock doors to interaction'):
    setup()
