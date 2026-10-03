"""Canonical Humanoid body sculpture; shared rig and materials live in V2.

All detail is authored geometry, with five palette-compatible materials and at
most two weights per vertex. Optional finger chains support runtime grasp;
no face or cloth simulation is required.
"""

import math

import bpy


def create_body(api, armature, scene, materials):
    shirt, pants, skin, hair, boots = (materials[key] for key in
                                     ('shirt', 'pants', 'skin', 'hair', 'boots'))

    def mesh(name, vertices, faces, material, bone='RF_HEAD', smooth=False):
        data = bpy.data.meshes.new(name)
        data.from_pydata(vertices, [], faces)
        data.update()
        data.materials.append(material)
        for polygon in data.polygons:
            polygon.use_smooth = smooth
        obj = bpy.data.objects.new(name, data)
        scene.collection.objects.link(obj)
        obj.parent = armature
        modifier = obj.modifiers.new('RFCHAR Skin', 'ARMATURE')
        modifier.object = armature
        group = obj.vertex_groups.new(name=bone)
        group.add(list(range(len(vertices))), 1.0, 'REPLACE')
        return obj

    def loft(name, profiles, material, weights, sides=16, offset=0,
             smooth=True):
        obj = api.vertical_loft(name, profiles, sides, material, armature,
                                scene, weights, longitudinal_smooth=smooth)
        if offset:
            for vertex in obj.data.vertices:
                vertex.co.x += offset
        return obj

    def rigid(name, profiles, material, bone, sides=16, offset=0):
        return loft(name, profiles, material, [[(bone, 1.0)]] * len(profiles),
                    sides, offset)

    def segment(name, start, end, profiles, material, bone, sides=10):
        return api.segment_loft(name, start, end, profiles, sides, material,
                                armature, scene, bone)

    # Creation order preserves the canonical palette indices: pants=0,
    # shirt=1, skin=2, hair=3, boots=4, used by CPU and Scene roster palettes.
    loft('Pelvis', [(.72, .003, .201, .142), (.79, .012, .255, .183),
         (.88, .020, .275, .193), (.975, .000, .255, .161),
         (1.035, .000, .217, .120)], pants,
         [[('RF_HIPS', 1)], [('RF_HIPS', 1)],
          [('RF_HIPS', .85), ('RF_SPINE', .15)],
          [('RF_HIPS', .35), ('RF_SPINE', .65)], [('RF_SPINE', 1)]])

    # The top rings form the clavicle/trapezius slope instead of a horizontal
    # chest lid. Shoulder roots remain under the shirt at the original joints.
    loft('Torso', [
        (.96, -.003, .263, .182), (1.015, -.006, .249, .166),
        (1.075, -.008, .233, .151), (1.15, -.008, .238, .162),
        (1.25, -.010, .260, .193), (1.355, -.004, .280, .209),
        (1.435, .006, .286, .202), (1.505, .012, .264, .170),
        (1.565, .017, .183, .128), (1.592, .018, .108, .094)], shirt,
        [[('RF_HIPS', .55), ('RF_SPINE', .45)],
         [('RF_HIPS', .30), ('RF_SPINE', .70)], [('RF_SPINE', 1)],
         [('RF_SPINE', 1)], [('RF_SPINE', .65), ('RF_CHEST', .35)],
         [('RF_CHEST', .8), ('RF_UPPER_CHEST', .2)],
         [('RF_CHEST', .45), ('RF_UPPER_CHEST', .55)],
         [('RF_UPPER_CHEST', 1)], [('RF_UPPER_CHEST', 1)],
         [('RF_UPPER_CHEST', 1)]])
    rigid('ShirtCollar', [(1.557, .016, .112, .099),
                          (1.591, .016, .116, .101),
                          (1.607, .018, .107, .093)], shirt, 'RF_UPPER_CHEST')
    rigid('Neck', [(1.551, .010, .096, .087), (1.60, .015, .091, .082),
                   (1.68, .022, .079, .075), (1.735, .024, .082, .080)],
          skin, 'RF_NECK')

    # Closed head topology has separate chin, mandibular angle, cheek, eye,
    # brow and forehead rings. The frontal surface is wider than an ellipse;
    # small depressions under the brow provide actual orbital depth.
    head_profiles = [
        (1.650, .013, .098, .087), (1.687, -.011, .124, .116),
        (1.720, -.018, .148, .138), (1.755, -.004, .179, .167),
        (1.792, .006, .202, .179), (1.824, .010, .211, .180),
        (1.850, .012, .213, .184), (1.889, .019, .213, .180),
        (1.930, .026, .202, .175), (1.980, .028, .182, .159),
        (2.025, .026, .139, .127), (2.049, .024, .074, .074)]
    head = loft('HeadSculpt', head_profiles, skin,
                [[('RF_HEAD', 1.0)]] * len(head_profiles), sides=24, smooth=False)
    # Keep the face's broad planes gently curved, avoiding a faceted mask.
    for vertex in head.data.vertices:
        ring, angular = divmod(vertex.index, 24)
        angle = angular * math.tau / 24
        z, cy, width, depth = head_profiles[ring]
        if math.sin(angle) < 0:
            t = abs(math.cos(angle))
            frontal = max(0, 1 - t * t) ** .30
            vertex.co.y = cy - depth * frontal
            if ring == 5:
                vertex.co.y += .008 * math.exp(-((t - .43) / .24) ** 2)
            # Nose bridge and tip are part of the head surface. A separate
            # wedge leaves a conspicuous air gap in the orthographic profile.
            if ring in (4, 5, 6):
                vertex.co.y -= {4: .045, 5: .022, 6: .006}[ring] * max(
                    0, 1 - t / .25)
    head.data.update()
    # Sculpted normals use the real surface; caps stay planar.
    for polygon in head.data.polygons:
        polygon.use_smooth = polygon.index < (len(head_profiles) - 1) * 24 * 2

    for sign in (-1, 1):
        # The iris/socket cue is deliberately small: the body remains a
        # restrained tactical stylization, and shares the dark hair material.
        cx = .084 * sign
        eye = [(cx-.031, -.170, 1.824), (cx-.013, -.177, 1.832),
               (cx+.017, -.176, 1.830), (cx+.031, -.168, 1.823),
               (cx+.015, -.176, 1.818), (cx-.015, -.177, 1.818)]
        mesh('EyeInset' + str(sign), eye, [(0, 5, 4, 3, 2, 1)], hair)
        mesh('BrowPlane' + str(sign),
             [(cx-.040, -.169, 1.840), (cx+.041, -.168, 1.841),
              (cx+.035, -.176, 1.851), (cx-.027, -.180, 1.854)],
             [(0, 1, 2, 3)], skin)
        mesh('Brow' + str(sign),
             [(cx-.030, -.181, 1.848), (cx+.032, -.176, 1.846),
              (cx+.027, -.178, 1.852), (cx-.025, -.183, 1.855)],
             [(0, 1, 2, 3)], hair)
        # Ear helix remains visible below helmet rails and has its own inset.
        rigid('Ear' + str(sign), [(1.771, .010, .017, .023),
                    (1.800, .008, .027, .034), (1.844, .014, .026, .037),
                    (1.865, .017, .017, .028)], skin, 'RF_HEAD',
                    sides=10, offset=.206 * sign)
    mesh('LowerLip', [(-.046, -.173, 1.744), (0, -.180, 1.749),
         (.046, -.173, 1.744), (0, -.177, 1.739)], [(0, 3, 2, 1)], skin)
    mesh('MouthCrease', [(-.039, -.177, 1.748), (0, -.182, 1.746),
         (.039, -.177, 1.748), (0, -.182, 1.743)], [(0, 3, 2, 1)], hair)

    # Close-cropped hair wraps the occipital area. A varying hairline preserves
    # the forehead while covering the former bare back of the skull.
    hair_rings = []
    for row in range(6):
        ring = []
        for angular in range(24):
            angle = angular * math.tau / 24
            front = max(0, -math.sin(angle))
            rear = max(0, math.sin(angle))
            if row == 0:
                z = 1.858 + .082 * front - .075 * rear
                rx, ry, cy = .212, .191, .024
            else:
                z, cy, rx, ry = [(1.958, .030, .220, .194),
                    (1.998, .035, .218, .191), (2.039, .040, .195, .171),
                    (2.067, .042, .152, .133), (2.080, .035, .083, .077)][row-1]
            ring.append((rx*math.cos(angle), cy+ry*math.sin(angle), z))
        hair_rings.extend(ring)
    faces = [(row*24+i, row*24+(i+1)%24, (row+1)*24+(i+1)%24,
              (row+1)*24+i) for row in range(5) for i in range(24)]
    faces.append(tuple(range(120, 144)))
    mesh('HairCrop', hair_rings, faces, hair)
    # Broad swept ridges are geometry instead of noisy individual strands.
    for index, x in enumerate((-.12, -.055, .01, .075)):
        mesh('HairSweep' + str(index),
             [(x-.032, -.150, 1.958), (x+.038, -.146, 1.965),
              (x+.061, -.032, 2.055), (x+.018, .105, 2.052),
              (x-.032, .058, 2.064), (x-.018, -.050, 2.076)],
             [(0, 1, 2, 5), (5, 2, 3, 4)], hair)

    for side, sign in (('L', 1.0), ('R', -1.0)):
        prefix = 'RF_' + side + '_'

        def blend(parent, child, amount):
            return [(prefix+parent, 1-amount), (prefix+child, amount)]

        sleeve = [(.220, .059, .101), (.273, .103, .122),
            (.320, .116, .120), (.365, .110, .111), (.415, .098, .100),
            (.47, .094, .093), (.535, .083, .078), (.57, .080, .075),
            (.59, .081, .075), (.62, .084, .077), (.67, .086, .079),
            (.735, .079, .074), (.790, .066, .062), (.826, .060, .057),
            (.842, .061, .058)]
        api.weighted_rings(side+'Sleeve',
            [((x*sign, .003, 1.50), (0, 0, sign), (0, -1, 0), h, d)
             for x, h, d in sleeve], 12, shirt, armature, scene,
            [blend('SHOULDER', 'UPPER_ARM', v) for v in (0, .18, .52, .85)] +
            [blend('UPPER_ARM', 'FOREARM', v) for v in (0, 0, .18, .42, .65, .90, 1)] +
            [blend('FOREARM', 'HAND', v) for v in (0, .22, .7, 1)],
            longitudinal_smooth=True)
        # Narrow wrist, metacarpal fan and finger tips have separate volumes.
        # Palm stays on HAND; two-joint fingers can close around a weapon.
        segment(side+'Palm', (.822*sign, -.003, 1.5),
                (.973*sign, -.010, 1.5),
                [(0, .047, .050, 0), (.35, .044, .064, 0),
                 (.78, .040, .066, 0), (1, .033, .054, 0)], skin, prefix+'HAND')
        for finger, (y, length) in enumerate(((-.051, .073), (-.019, .084),
                                             (.014, .078), (.043, .062))):
            finger_obj = segment(side+'Finger'+str(finger), (.956*sign, y, 1.495),
                    ((.956+length)*sign, y-.006, 1.488),
                    [(0, .018, .016, 0), (.45, .018, .016, 0),
                     (.60, .017, .015, 0),
                     (1, .012, .012, 0)], skin, prefix+'HAND', 8)
            finger_obj.vertex_groups.clear()
            groups = [finger_obj.vertex_groups.new(name=prefix+'FINGER_'+str(finger)+'_'+str(j)) for j in (1,2)]
            for vertex in finger_obj.data.vertices:
                amount = (0, .15, .80, 1)[vertex.index//8]
                for group, weight in zip(groups,(1-amount,amount)):
                    if weight: group.add([vertex.index],weight,'REPLACE')
        thumb = segment(side+'Thumb', (.866*sign, -.044, 1.487),
                (.933*sign, -.095, 1.478),
                [(0, .030, .030, 0), (.45, .028, .028, 0),
                 (.65, .025, .025, 0),
                 (1, .019, .019, 0)], skin, prefix+'HAND', 8)
        thumb.vertex_groups.clear()
        groups = [thumb.vertex_groups.new(name=prefix+'FINGER_thumb_'+str(j)) for j in (1,2)]
        for vertex in thumb.data.vertices:
            amount = (0, .15, .80, 1)[vertex.index//8]
            for group, weight in zip(groups,(1-amount,amount)):
                if weight: group.add([vertex.index],weight,'REPLACE')

        # More longitudinal sections give cloth folds controlled amplitude;
        # the knee bridge retains its original monotonic two-bone blend.
        loft(side+'Trouser', [(.12, 0, .066, .064), (.16, 0, .073, .071),
             (.235, .005, .084, .086), (.30, .008, .102, .098),
             (.375, .009, .112, .109), (.43, .002, .108, .107),
             (.475, -.006, .107, .103), (.505, -.010, .105, .106),
             (.54, -.005, .111, .104), (.59, 0, .117, .111),
             (.685, .007, .128, .127), (.79, .010, .137, .149),
             (.875, .014, .136, .156), (.905, .012, .131, .152)], pants,
             [blend('LOWER_LEG', 'FOOT', v) for v in (1, .70, .15, 0)] +
             [blend('UPPER_LEG', 'LOWER_LEG', v) for v in
              (1, 1, .85, .60, .30, 0, 0, 0, 0, 0)], sides=12,
             offset=.15*sign)
        # Boot shaft, shaped toe box and flat separate welt/sole replace the
        # low wedge. Everything moves on FOOT, with true z=0 ground contact.
        rigid(side+'BootShaft', [(.035, -.004, .085, .092),
              (.095, -.002, .087, .091), (.17, .002, .077, .077),
              (.202, .005, .078, .079)], boots, prefix+'FOOT', 12, .15*sign)
        foot = segment(side+'BootUpper', (.15*sign, .010, .095),
               (.15*sign, -.30, .065),
               [(0, .083, .079, 0), (.23, .108, .085, 0),
                (.65, .108, .069, 0), (.90, .099, .059, 0),
                (1, .077, .045, 0)], boots, prefix+'FOOT', 12)
        for vertex in foot.data.vertices:
            vertex.co.z = max(.030, vertex.co.z)
        # Sole outline is a rounded rectangle, not an elongated ellipsoid.
        outline = [(-.074, .079), (-.098, .038), (-.110, -.175),
                   (-.095, -.305), (-.068, -.324), (.068, -.324),
                   (.095, -.305), (.110, -.175), (.098, .038), (.074, .079)]
        vertices = [(x+.15*sign, y, z) for z in (0, .034)
                    for x, y in outline]
        faces = [(i, (i+1)%10, 10+(i+1)%10, 10+i) for i in range(10)]
        faces += [tuple(range(9, -1, -1)), tuple(range(10, 20))]
        mesh(side+'BootSole', vertices, faces, boots, prefix+'FOOT')
        for lace, y in enumerate((-.04, -.075, -.108)):
            segment(side+'Lace'+str(lace), (.15*sign-.045, y, .173-lace*.008),
                    (.15*sign+.045, y-.006, .173-lace*.008),
                    [(0, .005, .005, 0), (1, .005, .005, 0)],
                    hair, prefix+'FOOT', 6)
