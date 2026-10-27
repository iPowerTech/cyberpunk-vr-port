import unittest
from patch_camera import patch


SOURCE='''define void @VS() {
  %h = call %dx.types.Handle @dx.op.createHandle(i32 57, i8 2, i32 0, i32 1, i1 false)
  %v = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %h, i32 36)
  %x = extractvalue %dx.types.CBufRet.f32 %v, 0
  ret void
}
!dx.shaderModel = !{!1}
!1 = !{!"vs", i32 6, i32 0}
!dx.version = !{!2}
!2 = !{i32 1, i32 0}
!3 = !{i32 0, %CameraShaderConsts* undef, !"camera", i32 0, i32 1, i32 1, i32 848, null}
!4 = !{void ()* @VS, !"VS", !5, !6, null}
'''


class CameraPatchTests(unittest.TestCase):
    def test_pixel_stage_reads_its_own_view_id_without_signature_remapping(self):
        source=SOURCE.replace('@VS','@PS').replace('!"VS"','!"PS"').replace('!"vs"','!"ps"')
        changed,layout=patch(source,main_first=True)
        self.assertIn('!"ps", i32 6, i32 1',changed)
        self.assertIn('xor i32 %cvr_view, 1',changed)
        self.assertIn('!"PS", !5, !6,',changed)
        self.assertEqual(layout['camera_register'],1)
        self.assertEqual(layout['binding_size'],2048)
    def test_main_prefix_preserves_native_handle_and_reverses_only_eye_index(self):
        ordinary,layout=patch(SOURCE)
        paired,reverse=patch(SOURCE,main_first=True)
        self.assertFalse(layout['main_first']);self.assertTrue(reverse['main_first'])
        self.assertIn('mul i32 %cvr_view, 64',ordinary)
        self.assertIn('xor i32 %cvr_view, 1',paired)
        self.assertIn('mul i32 %cvr_camera_eye, 64',paired)
        self.assertIn('i32 0, i32 1, i1 false)',paired)
        self.assertEqual(reverse['binding_size'],2048)
        self.assertEqual(reverse['camera_words'],[144])
        self.assertEqual(reverse['patched_loads'],1)
        with self.assertRaises(ValueError):patch(paired,main_first=True)


if __name__=='__main__':unittest.main()
