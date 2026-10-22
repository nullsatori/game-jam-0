"""Authored resin/fiber surface for the approved composite shells and insert.

Image-led art material, not a measured scan. Shared structure drives color,
roughness and shallow relief; the production baker emits the final PBR maps.
"""
import bpy


def apply_composite(material, principled, coordinates, bump, root, insert=False):
    nodes, links = material.node_tree.nodes, material.node_tree.links

    def node(kind):
        return nodes.new(kind)

    mapping = node('ShaderNodeVectorMath')
    mapping.operation = 'SCALE'
    mapping.inputs[3].default_value = 1 / (.045 if insert else .065)
    links.new(coordinates, mapping.inputs[0])
    texture = node('ShaderNodeTexImage')
    texture.image = bpy.data.images.load(str(root / 'material_sources/CharcoalPhenolic_Source.png'), check_existing=True)
    texture.projection = 'BOX'
    texture.projection_blend = .22
    links.new(mapping.outputs[0], texture.inputs[0])
    structure = node('ShaderNodeMapRange')
    structure.clamp = True
    structure.inputs['From Min'].default_value = .009
    structure.inputs['From Max'].default_value = .10
    links.new(texture.outputs['Color'], structure.inputs[0])

    color = node('ShaderNodeValToRGB')
    color.color_ramp.elements[0].color = (.012, .015, .013, 1) if insert else (.018, .021, .019, 1)
    color.color_ramp.elements[1].color = (.021, .025, .021, 1) if insert else (.024, .028, .024, 1)
    links.new(structure.outputs[0], color.inputs[0])
    links.new(color.outputs[0], principled.inputs['Base Color'])
    roughness = node('ShaderNodeMapRange')
    roughness.inputs['To Min'].default_value = .53 if insert else .49
    roughness.inputs['To Max'].default_value = .75 if insert else .65
    links.new(structure.outputs[0], roughness.inputs[0])
    links.new(roughness.outputs[0], principled.inputs['Roughness'])
    links.new(structure.outputs[0], bump.inputs['Height'])
    bump.inputs['Distance'].default_value = .00012 if insert else .000055
    bump.inputs['Strength'].default_value = .7
    principled.inputs['Specular IOR Level'].default_value = .34

    if not insert:
        # Compact cut fibers appear only on sloping transitions, not as a pale
        # outline around every face. Broad planes retain the resin skin.
        geometry = node('ShaderNodeNewGeometry')
        separate = node('ShaderNodeSeparateXYZ')
        links.new(geometry.outputs['Normal'], separate.inputs[0])
        absolute = node('ShaderNodeMath'); absolute.operation = 'ABSOLUTE'
        links.new(separate.outputs['Y'], absolute.inputs[0])
        band = node('ShaderNodeValToRGB')
        ramp = band.color_ramp
        ramp.elements[0].position = .15; ramp.elements[0].color = (0, 0, 0, 1)
        ramp.elements[1].position = .94; ramp.elements[1].color = (0, 0, 0, 1)
        ramp.elements.new(.48).color = (.35, .35, .35, 1)
        ramp.elements.new(.78).color = (.35, .35, .35, 1)
        links.new(absolute.outputs[0], band.inputs[0])
        mask = node('ShaderNodeMath'); mask.operation = 'MULTIPLY'
        links.new(band.outputs[0], mask.inputs[0]); links.new(structure.outputs[0], mask.inputs[1])
        reveal = node('ShaderNodeMixRGB')
        links.new(mask.outputs[0], reveal.inputs[0]); links.new(color.outputs[0], reveal.inputs[1])
        reveal.inputs[2].default_value = (.031, .034, .029, 1)
        links.new(reveal.outputs[0], principled.inputs['Base Color'])
