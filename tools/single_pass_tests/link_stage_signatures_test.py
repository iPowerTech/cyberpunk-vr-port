import unittest
from link_stage_signatures import link_pixel_inputs, signature


def stage(kind, face_row=1, user_name='SV_Position', clip=True):
    vertex = kind == 'vs'
    elements = '!3, !4' if vertex and clip else '!3' if vertex else '!3, !5'
    return f'''define void @main() {{
  %a = call float @dx.op.loadInput.f32(i32 4, i32 0, i32 0, i8 0, i32 undef)
  %b = call i32 @dx.op.loadInput.i32(i32 4, i32 1, i32 0, i8 0, i32 undef)
  ret void
}}
!dx.shaderModel = !{{!0}}
!dx.entryPoints = !{{!1}}
!0 = !{{!"{kind}", i32 6, i32 1}}
!1 = !{{void ()* @main, !"main", !2, null, null}}
!2 = !{{null, !6, null}}
!3 = !{{i32 0, !"{user_name}", i8 9, i8 3, !7, i8 4, i32 1, i8 4, i32 0, i8 0, null}}
!4 = !{{i32 1, !"SV_ClipDistance", i8 9, i8 6, !7, i8 2, i32 1, i8 1, i32 1, i8 0, null}}
!5 = !{{i32 1, !"SV_IsFrontFace", i8 5, i8 13, !7, i8 1, i32 1, i8 1, i32 {face_row}, i8 0, null}}
!6 = !{{{elements}}}
!7 = !{{i32 0}}
'''.replace('!2 = !{null, !6, null}', '!2 = !{null, !6, null}' if vertex else '!2 = !{!6, null, null}')


class SignatureLinkTests(unittest.TestCase):
    def test_clip_prefix_and_generated_input_relocation(self):
        vertex, pixel = stage('vs'), stage('ps')
        linked, report = link_pixel_inputs(vertex, pixel)
        self.assertEqual(report['relocated'], [dict(semantic='!"sv_isfrontface"', old=[1, 0], new=[2, 0])])
        self.assertIn('@dx.op.loadInput.i32(i32 4, i32 2,', linked)
        elements = signature(linked, 'ps')[2]
        self.assertEqual([e['fields'][1] for e in elements], ['!"SV_Position"', '!"SV_ClipDistance"', '!"SV_IsFrontFace"'])
        self.assertEqual(elements[1]['fields'][10], 'null')
        self.assertEqual(link_pixel_inputs(vertex, linked)[0], linked)
        self.assertIn('@dx.op.loadInput.i32(i32 4, i32 1,', pixel)

    def test_no_relocation_without_collision(self):
        _, report = link_pixel_inputs(stage('vs'), stage('ps', face_row=2))
        self.assertEqual(report['relocated'], [])

    def test_already_matching_prefix_is_unchanged(self):
        pixel = stage('ps')
        linked, report = link_pixel_inputs(stage('vs', clip=False), pixel)
        self.assertEqual(linked, pixel)
        self.assertFalse(report['changed'])

    def test_shape_changes_rejected(self):
        pixel = stage('ps').replace('i32 1, i8 4, i32 0', 'i32 1, i8 3, i32 0')
        with self.assertRaisesRegex(ValueError, 'shape/location'):
            link_pixel_inputs(stage('vs'), pixel)

    def test_unmatched_user_input_rejected(self):
        with self.assertRaisesRegex(ValueError, 'absent'):
            link_pixel_inputs(stage('vs'), stage('ps', user_name='TEXCOORD'))

    def test_unknown_input_instruction_id_rejected(self):
        pixel = stage('ps').replace('loadInput.i32(i32 4, i32 1', 'loadInput.i32(i32 4, i32 9')
        with self.assertRaisesRegex(ValueError, 'original semantic'):
            link_pixel_inputs(stage('vs'), pixel)

    def test_malformed_or_wrong_model_rejected(self):
        for pixel in ('', stage('ps').replace('i32 6, i32 1', 'i32 6, i32 0')):
            with self.assertRaises(ValueError):
                link_pixel_inputs(stage('vs'), pixel)


if __name__ == '__main__':
    unittest.main()
