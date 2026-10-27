import copy
import struct
import unittest
from ia_snapshot_report_test import fixture as ia_fixture
from material_resource_report import analyze, analyze_buffers, analyze_buffer_copies


def fixture():
    sources = ia_fixture()[3]
    rows, summaries, commands = [], [], []
    for ordinal, source in enumerate(sources['rows']):
        row = [0]*24
        row[:9] = [ordinal, source[1], 23, 0, 0, 8000+ordinal, 0, 90+ordinal, 5]
        row[10:14] = [9000+ordinal, ordinal+1, 20+ordinal, 10000+ordinal]
        row[14:21] = [source[6], source[7], ordinal+1, 1, 1, 3, 98]
        rows.append(row)
        summaries.append([ordinal, source[1], 1, 1, 0])
        command = [0]*32
        command[4] = 1
        commands.append(command)
    return rows, summaries, sources, dict(verified=True, commands=commands), dict(verified=True, rows=[])


def check(data):
    rows, summaries, *rest = data
    return analyze(b''.join(struct.pack('<24Q', *r) for r in rows),
                   b''.join(struct.pack('<5Q', *r) for r in summaries), *rest)


class MaterialResourceTests(unittest.TestCase):
    def test_raw_and_structured_material_nominations_are_not_copy_grants(self):
        sources=ia_fixture()[3]
        rows=[]
        material=[]
        for ordinal,source in enumerate(sources['rows']):
            for reg in (4,6):
                row=[0]*40
                row[:8]=[ordinal,source[1],23,reg,1,8000+ordinal*2+reg,0x100000,4096]
                row[9]=1024
                row[10]=1
                row[12:18]=[100+ordinal,0,4096,11,2,0xC0]
                row[24]=1
                row[28:33]=[39,0,256,0,1] if reg==4 else [0,0,16,64,0]
                row[33:38]=[source[6],source[7],ordinal+1,20000,30000]
                rows.append(row)
                dependency=[0]*24
                dependency[:6]=[ordinal,source[1],23,reg,0,row[5]]
                dependency[14:17]=[source[6],source[7],ordinal+1]
                dependency[19]=1
                material.append(dependency)
        def run(values):
            return analyze_buffers(b''.join(struct.pack('<40Q',*row) for row in values),sources,b''.join(struct.pack('<24Q',*row) for row in material))
        result=run(rows)
        self.assertEqual(result['captured'],8)
        self.assertFalse(result['copyPermission'])
        self.assertFalse(result['nativeExecutionPermitted'])
        for index,value in ((3,5),(4,11),(5,0),(6,0),(7,512),(8,1),(9,512),(10,2),(12,0),(13,4),(13,16),(14,1),(15,16),(28,28),(31,128),(32,0),(34,0),(36,0),(37,0),(38,1),(39,1)):
            bad=copy.deepcopy(rows)
            bad[0][index]=value
            with self.subTest(field=index),self.assertRaises(ValueError):run(bad)
        with self.assertRaises(ValueError):run(rows[:-1])
        with self.assertRaises(ValueError):run(rows+[rows[0]])
        copies=[]
        for index,row in enumerate(rows):
            copies.append([row[0],row[1],row[3],1,0,100000+index,160000,row[8],index*2048,row[9],1,1,1,*row[33:36]])
        def check_copies(values):
            return analyze_buffer_copies(b''.join(struct.pack('<16Q',*row) for row in values),result)
        self.assertEqual(check_copies(copies)['complete'],8)
        for field,value in ((3,0),(4,1),(5,0),(6,rows[0][5]),(7,1),(8,1),(9,1023),(10,0),(11,0),(13,0),(14,0),(15,0)):
            bad=copy.deepcopy(copies)
            bad[0][field]=value
            with self.subTest(copy_field=field),self.assertRaises(ValueError):check_copies(bad)
        bad=copy.deepcopy(copies);bad[1][8]=bad[0][8]
        with self.assertRaises(ValueError):check_copies(bad)
        bad=copy.deepcopy(copies);bad[1][5]=bad[0][5]
        with self.assertRaises(ValueError):check_copies(bad)

    def test_owners_do_not_grant_destination_execution(self):
        result = check(fixture())
        self.assertEqual(result['readySources'], 4)
        self.assertEqual(result['validAfterSource'], 4)
        self.assertFalse(result['destinationReadEpochProved'])
        self.assertFalse(result['nativeExecutionPermitted'])

    def test_missing_pin_and_forged_scope_rejected(self):
        for field, value in ((10, 0), (11, 0), (12, 0), (13, 0), (15, 0), (17, 0), (21, 1)):
            data = fixture()
            data[0][0][field] = value
            with self.subTest(field=field), self.assertRaises(ValueError):
                check(data)

    def test_null_srv_does_not_need_an_allocation(self):
        data = fixture()
        row = data[0][0]
        row[8] = 1
        for field in (5, 6, 7, 9, 10, 11, 12, 13, 19, 20):
            row[field] = 0
        self.assertEqual(check(data)['readySources'], 4)
        row[10] = 1
        with self.assertRaises(ValueError):
            check(data)

    def test_deferred_dependency_cannot_be_hidden(self):
        data = fixture()
        row = data[0][0]
        row[8:14] = [4, 85, 0, 0, 0, 0]
        row[17:19] = [0, 0]
        with self.assertRaises(ValueError):
            check(data)
        data[1][0][3] = 0
        self.assertEqual(check(data)['readySources'], 3)

    def test_duplicate_and_omitted_source_rejected(self):
        data = fixture()
        data[0].append(copy.deepcopy(data[0][0]))
        with self.assertRaises(ValueError):
            check(data)
        data = fixture()
        data[1].pop()
        with self.assertRaises(ValueError):
            check(data)

    def test_private_t5_has_separate_completed_owner(self):
        data = fixture()
        row = data[0][0]
        row[3] = 5
        row[8:14] = [6, 0, 14000, 0, 0, 0]
        row[17:21] = [0, 1, 1, 0]
        view = [0]*32
        view[9], view[27] = 14000, 1
        data[4]['rows'] = [view]
        self.assertEqual(check(data)['readySources'], 4)
        view[27] = 0
        with self.assertRaises(ValueError):
            check(data)


if __name__ == '__main__':
    unittest.main()
