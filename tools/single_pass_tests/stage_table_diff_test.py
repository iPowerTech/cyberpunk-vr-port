import struct
import unittest

import stage_table_diff as diff


def table(param, group, descriptors, offset):
    return dict(op='RootTable', flags=16, count=0, a=param, b=0, c=0, d=offset), \
        struct.pack('<Q', len(descriptors) | (group << 32)) + struct.pack('<%dQ' % len(descriptors), *descriptors)


def stream(binds):
    """binds: list of (pso, {param: descriptors}) per draw."""
    rows, payload = [], b''
    for pso, tables in binds:
        rows.append(dict(op='Pipeline', flags=0, count=0, a=0, b=pso, c=0, d=diff.NO_PAYLOAD))
        for param, descriptors in tables.items():
            row, blob = table(param, param + 1, descriptors, len(payload))
            rows.append(row)
            payload += blob
        rows.append(dict(op='DrawIndexed', flags=0, count=0, a=36, b=0, c=0, d=diff.NO_PAYLOAD))
    return rows, payload


class TablePayloadTest(unittest.TestCase):
    def test_decodes_group_and_descriptors(self):
        row, payload = table(3, 7, [0x10, 0x20], 0)
        self.assertEqual(diff.table_payload(row, payload), (7, (0x10, 0x20)))

    def test_rows_without_contents(self):
        row, payload = table(3, 7, [0x10], 0)
        row['flags'] = 0
        self.assertIsNone(diff.table_payload(row, payload))
        row['flags'] = 16
        self.assertIsNone(diff.table_payload(row, payload[:12]))


class CompareTest(unittest.TestCase):
    def test_finds_the_per_view_slot(self):
        main = diff.draw_states(*stream([(1, {0: [0xC0, 0xAA], 2: [0x5]}), (2, {0: [0xC0, 0xBB], 2: [0x6]})]))
        vrcam = diff.draw_states(*stream([(1, {0: [0xC1, 0xAA], 2: [0x5]}), (2, {0: [0xC1, 0xBB], 2: [0x6]})]))
        value = diff.compare(main, vrcam, camera=[0xC0, 0xC1])
        self.assertEqual(value['matched'], 2)
        self.assertEqual(len(value['slots']), 1)
        slot = value['slots'][0]
        self.assertEqual((slot['parameter'], slot['index'], slot['differ'], slot['distinctPairs']), (0, 0, 2, 1))
        self.assertTrue(slot['camera'])
        self.assertEqual(slot['top'][0], dict(vrcam='0xc1', main='0xc0', draws=2))

    def test_tables_without_contents_are_unknown(self):
        rows, payload = stream([(1, {0: [0xC0]})])
        rows[1]['flags'] = 0
        main = diff.draw_states(rows, payload)
        vrcam = diff.draw_states(*stream([(1, {0: [0xC1]})]))
        value = diff.compare(main, vrcam)
        self.assertEqual(value['unknownTables'], 1)
        self.assertEqual(value['slots'], [])


class LocateTest(unittest.TestCase):
    def test_same_offset_in_both_dumps(self):
        slots = [dict(parameter=0, index=3, top=[dict(vrcam='0x10', main='0x20', draws=5)])]
        dumps = dict(vrcam=dict(context=[0, 0x10, 0x10], state=[0x10]), main=dict(context=[0, 0x20, 0x99], state=[0x20]))
        self.assertEqual(diff.locate(slots, dumps)[0]['at'], ['context+0x8', 'state+0x0'])


if __name__ == '__main__':
    unittest.main()
