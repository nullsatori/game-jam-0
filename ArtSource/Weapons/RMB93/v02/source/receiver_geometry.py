"""Single stamped receiver housing with a restrained integral left relief."""
from joinery_geometry import inset_outline


def build_receiver(prism,mesh,parts,steel,bpy):
    # The approved silhouette and all subsequent counter/loading/barrel cuts
    # remain unchanged. This is only an exterior pressed surface on +Y.
    receiver=prism('Receiver_shell',[
        (.120,.029),(.124,.033),(.124,.048),(.120,.0531),
        (-.0137,.0535),(-.0137,.066),(-.0225,.066),(-.0375,.046),
        (-.056,.022),(-.045,.0025),(.0344,.0025),(.0456,.0203),
        (.1037,.0215)],.0175,steel,bevel=.00045)
    # The long formed plane is deliberately independent of the outer contour.
    # Its unequal clipped ends distinguish the housing without a raised frame.
    mouth=[(-.022,.0370),(-.012,.0455),(.104,.0455),
           (.112,.0378),(.098,.0305),(-.008,.0305)]
    floor=inset_outline(mouth,.00105)
    n=len(mouth)
    vertices=[(x,y,z) for y,outline in [(.01685,floor),(.01750,mouth),(.0200,mouth)]
              for x,z in outline]
    faces=[tuple(range(n-1,-1,-1)),tuple(range(2*n,3*n))]
    for layer in range(2):
        a=layer*n;b=(layer+1)*n
        faces.extend((a+i,a+(i+1)%n,b+(i+1)%n,b+i) for i in range(n))
    cutter=mesh('_Receiver_pressed_relief_cutter',vertices,faces,steel,'Body')
    bpy.context.view_layer.objects.active=receiver
    mod=receiver.modifiers.new('Integral shallow stamped left plane','BOOLEAN')
    mod.operation='DIFFERENCE';mod.solver='EXACT';mod.object=cutter
    receiver.modifiers.move(len(receiver.modifiers)-1,0)
    bpy.ops.object.modifier_apply(modifier=mod.name)
    parts.remove(cutter);data=cutter.data
    bpy.data.objects.remove(cutter,do_unlink=True)
    bpy.data.meshes.remove(data)
    receiver['receiver_pressed_relief']=True
    receiver['design_note']='Single integral 0.65 mm left pressed relief, 1.05 mm sloped shoulder, asymmetric closed ends; original silhouette and control arrangement preserved'
    return receiver
