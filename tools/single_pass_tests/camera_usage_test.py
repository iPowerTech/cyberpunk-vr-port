import unittest
from camera_usage import camera_usage

HEADER='''!7 = !{i32 0, %CameraShaderConsts* undef, !"camera", i32 0, i32 1, i32 1, i32 848, null}
  %h = call %dx.types.Handle @dx.op.createHandle(i32 57, i8 2, i32 0, i32 1, i1 false)
'''
class CameraReadTests(unittest.TestCase):
    def test_component_read_does_not_require_unused_history_weight(self):
        src=HEADER+'''  %v = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %h, i32 36)
  %x = extractvalue %dx.types.CBufRet.f32 %v, 0
  %z = extractvalue %dx.types.CBufRet.f32 %v, 2
'''
        self.assertEqual(camera_usage(src)['words'],[144,146])
    def test_weight_consumer_requires_missing_word(self):
        src=HEADER+'''  %v = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %h, i32 36)
  %w = extractvalue %dx.types.CBufRet.f32 %v, 3
'''
        self.assertEqual(camera_usage(src)['words'],[147])
    def test_dynamic_read_requires_full_block(self):
        src=HEADER+'''  %v = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %h, i32 %dynamic)
  %w = extractvalue %dx.types.CBufRet.f32 %v, 0
'''
        self.assertEqual(camera_usage(src)['words'],list(range(212)))
    def test_aggregate_use_requires_all_four_components(self):
        src=HEADER+'''  %v = call %dx.types.CBufRet.f32 @dx.op.cbufferLoadLegacy.f32(i32 59, %dx.types.Handle %h, i32 36)
  call void @helper(%dx.types.CBufRet.f32 %v)
'''
        self.assertEqual(camera_usage(src)['words'],[144,145,146,147])
    def test_other_handle_operation_rejected(self):
        with self.assertRaises(ValueError):camera_usage(HEADER+'  call void @helper(%dx.types.Handle %h)')

if __name__=='__main__':unittest.main()
