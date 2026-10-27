import copy
import struct
import unittest
from ia_snapshot_report import analyze, analyze_reuse
from source_stream_report_test import fixture as source_fixture, raw
from source_stream_report import analyze as source_analyze


def pack(values, words):
    return b''.join(struct.pack(f'<{words}Q', *row) for row in values)


def fixture():
    stats, sources, retained = source_fixture()
    commands, observations, views = [], [], []
    for ordinal, source in enumerate(sources):
        source[19] = 128
        source[25:28] = [0x1000000 + ordinal * 256, 32, 57]
        keys = ([(0, 2**32 - 1)] if source[2] != 2 else []) + [(1, 7)]
        for index, (role, slot) in enumerate(keys):
            o = [0] * 64
            o[:10] = [role, slot, 0, 100 + ordinal, source[25], 256, source[25], 32, 57 if not role else 48, 0]
            o[40:43] = [source[6], source[7], ordinal + 1]
            o[10:17] = [o[3], o[4], o[5], 11, 12, 13, 1]
            o[19], o[22], o[26], o[28] = 1, 1, 1, 1
            o[29:40] = [21, 0, o[3], 0, o[5], 22, 23, 22, 2755, 8, 1]
            o[43] = 2047
            o[56], o[58], o[59], o[60], o[63] = 1, 1, 1, 1, 1
            observations.append([ordinal, source[1], index, *o])
            views.append([ordinal, source[1], role, slot, 1000 + ordinal, 2000 + ordinal, 0, index * 32, 32, 32,
                          0x2000000 + ordinal * 256 + index * 32, 32, o[8], 1, 1, 0])
        commands.append([ordinal, source[1], 1, len(keys), len(keys), 1, 1, 0, len(keys), len(keys) * 32,
                         source[6], source[7], ordinal + 1, 3000 + ordinal, 1, 1])
    report = source_analyze(stats, raw(sources), retained)
    return commands, observations, views, report


def check(data):
    commands, observations, views, source = data
    return analyze(pack(commands, 16), pack(observations, 67), pack(views, 16), source)


class IaSnapshotReportTests(unittest.TestCase):
    def test_same_recording_reuse_has_earlier_copy_and_actual_byte_counts(self):
        data=fixture();commands,observations,views,sources=data
        for row in observations:
            if row[0]==1:row[6]=row[13]=row[34]=observations[0][6]
        for row in views:
            if row[0]==1:
                prior=next(v for v in views if v[0]==0 and v[2:4]==row[2:4])
                row[4:11]=prior[4:11]
        report=check(data)
        reuse=[[v[0],v[1],v[2],v[3],1 if v[0]==1 else v[0]+1,int(v[0]==1),v[7],v[8]] for v in views]
        totals=[0,0];transfers=[]
        for c in commands:
            copied=0 if c[0]==1 else c[9];totals[c[1]]+=copied
            transfers.append([c[0],c[1],copied,c[9] if c[0]==1 else 0,totals[c[1]]])
        def run(a=reuse,b=transfers):return analyze_reuse(pack(a,8),pack(b,5),report,sources)
        self.assertEqual(run()['reusedBytes'],64)
        for row,col,value in ((2,4,2),(2,4,3),(2,5,0),(2,6,1),(2,7,64),(2,1,1)):
            bad=copy.deepcopy(reuse);bad[row][col]=value
            with self.subTest(row=row,col=col),self.assertRaises(ValueError):run(bad)
        for col,value in ((2,64),(3,0),(4,0),(4,64*1024*1024+16)):
            bad=copy.deepcopy(transfers);bad[1][col]=value
            with self.subTest(col=col),self.assertRaises(ValueError):run(b=bad)
        report['observations'][2][6]+=1
        with self.assertRaises(ValueError):run()

    def test_overlapping_views_share_bytes_only_for_identical_source_offsets(self):
        data=fixture();data[2][1][7]=0;data[2][1][10]=data[2][0][10];data[0][0][9]=32
        self.assertEqual(check(data)['copiedCommands'],4)
        wrong=copy.deepcopy(data);wrong[1][1][6]+=1
        with self.assertRaises(ValueError):check(wrong)
        wrong=copy.deepcopy(data);wrong[2][1][6]=1
        with self.assertRaises(ValueError):check(wrong)

    def test_separate_slot7_is_explicit_and_not_a_whole_set(self):
        commands,observations,views,sources=fixture()
        selections=[[i,s[1],0,s[19],s[19]] for i,s in enumerate(sources['rows'])]
        selections[0][2]=128;selections[0][4]=0
        observations=[r for r in observations if not (r[0]==0 and r[3]==1)]
        views=[r for r in views if not (r[0]==0 and r[2]==1)]
        commands[0][3]=commands[0][4]=commands[0][8]=1;commands[0][9]=32
        result=analyze(pack(commands,16),pack(observations,67),pack(views,16),sources,pack(selections,5))
        self.assertEqual(result['copiedCommands'],4);self.assertEqual(result['completeIaCommands'],3)
        selections[0][2]=64
        with self.assertRaises(ValueError):analyze(pack(commands,16),pack(observations,67),pack(views,16),sources,pack(selections,5))

    def test_exact_placed_offset_and_heap_span_are_required(self):
        data=fixture()
        for row in data[1]:
            o=row[3:];o[43]|=8192;o[22:26]=[2,900,65536,65536];o[30]=1;o[32]=900;o[33]=65536;o[46]=65536
            row[3:]=o
        self.assertEqual(check(data)['copiedCommands'],4)
        for field,value in ((43,2047|4096|8192),(23,0),(24,1),(25,0),(30,0),(32,901),(33,256),(46,0),(49,1)):
            with self.subTest(field=field):
                wrong=copy.deepcopy(data);wrong[1][0][3+field]=value
                with self.assertRaises(ValueError):check(wrong)

    def test_actual_pooled_logical_span_and_owner_are_required(self):
        data=fixture()
        for row in data[1]:
            o=row[3:];o[4]-=512;o[5]=1024;o[9]=512;o[18]=5;o[43]=1023|4096
            o[46]=1<<16;o[49:52]=[o[29],1,512];o[54]=1;o[33]=1024
            row[3:]=o
        for row in data[2]:row[6]=512
        self.assertEqual(check(data)['copiedCommands'],4)
        for field,value in ((46,1<<16|5),(49,999),(50,2),(51,0),(54,8),(11,1),(12,16)):
            with self.subTest(field=field):
                wrong=copy.deepcopy(data);wrong[1][0][3+field]=value
                with self.assertRaises(ValueError):check(wrong)

    def test_whole_set_copy_has_no_byte_or_draw_claim(self):
        result = check(fixture())
        self.assertEqual((result['copiedCommands'], result['copiedViews']), (4, 7))
        self.assertFalse(result['gpuBytesMatched'])
        self.assertFalse(result['nativeExecutionPermitted'])

    def test_unsupported_view_defers_complete_command(self):
        data = fixture()
        data[1][0][5] = 14
        data[0][0][4:10] = [1, 0, 0, 14, 0, 0]
        data[0][0][13:] = [0, 0, 0]
        data[2][:2] = []
        self.assertEqual(check(data)['deferredCommands'], 1)

    def test_changed_scopes_and_coordinates_reject(self):
        for group, field, value in ((0, 12, 99), (0, 14, 0), (1, 2, 99), (1, 9, 0), (1, 49, 1),
                                    (2, 3, 0), (2, 8, 16), (2, 11, 16), (2, 13, 0), (2, 15, 1)):
            with self.subTest(group=group, field=field):
                data = copy.deepcopy(fixture())
                data[group][0][field] = value
                with self.assertRaises(ValueError):
                    check(data)


if __name__ == '__main__':
    unittest.main()
