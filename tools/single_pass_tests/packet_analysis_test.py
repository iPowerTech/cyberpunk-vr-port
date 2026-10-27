"""Check that native packet comparison removes indices but preserves semantics."""
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

class PacketComparison(unittest.TestCase):
    def compare(self,*,first_xor=0,skin_change=False,program=0,full=False):
        with tempfile.TemporaryDirectory() as directory:
            root=Path(directory);capture=root/'sample.json'
            a=((3<<17)|(5<<32)|(7<<45)|0x9123,11|(1<<18)|(13<<33)|(1<<51))
            b=(((8<<17)|(12<<32)|(17<<45)|0x9123)^first_xor,21|(1<<18)|(25<<33)|(1<<51))
            (root/'packets.bin').write_bytes(struct.pack('<6Q',*a,*b,*b))
            schema={}
            if full:
                command=struct.pack('<4I',1,0x80,7,1)+bytes([2])
                payload=command+bytes(48)+command+(bytes(47)+bytes([int(skin_change)]))
                first=bytearray(256);second=bytearray(256)
                for data,offset in ((first,0),(second,65)):
                    struct.pack_into('<4I',data,0,127,1,17,1)
                    struct.pack_into('<Q',data,104,0x1000+offset) # equivalent programs have different pointers
                    struct.pack_into('<I',data,112,17)
                    struct.pack_into('<6I',data,216,offset+17,48,48,offset,17,0)
                (root/'payload.bin').write_bytes(payload)
                schema={'inputBytes':256,'inputBlobFile':'payload.bin'}
            else:
                first=struct.pack('<4I',63,1,0,0)+bytes(200)
                second=bytearray(struct.pack('<4I',63,1,program,0)+bytes(200))
                if skin_change:second[160]=1 # a changed instance transform must not merge
            (root/'inputs.bin').write_bytes(first+second+second)
            common={'frame':1,'node':'0x23a938','groupPlane':3,'groupVariant':0,'groupFlags':'0x11',
                'modes':[0,0,1],'status':0,'metadataFlags':7,'storage':'0x100'}
            rows=[{**common,'sequence':1,'side':1,'offset':0,'count':1},
                  {**common,'sequence':2,'side':0,'offset':1,'count':2}]
            capture.write_text(json.dumps({'pid':0,'full':False,'packetDataFile':'packets.bin','inputDataFile':'inputs.bin','records':rows,**schema}))
            subprocess.run([sys.executable,'-B',str(Path(__file__).with_name('analyze_geometry_packets.py')),str(capture)],check=True,capture_output=True)
            return json.loads((root/'sample-comparison.json').read_text())['groups'][0]['exactResolvedInputs']
    def test_different_indices_and_duplicate_multiplicity(self):self.assertEqual(self.compare(),1)
    def test_sort_and_render_flags_are_preserved(self):
        for bit in (0,13,14,56,62):
            with self.subTest(bit=bit):self.assertEqual(self.compare(first_xor=1<<bit),0)
    def test_skin_changes_cannot_merge(self):self.assertEqual(self.compare(skin_change=True),0)
    def test_unresolved_command_programs_cannot_merge(self):self.assertEqual(self.compare(program=1),0)
    def test_full_program_and_instance_blobs(self):
        self.assertEqual(self.compare(full=True),1)
        self.assertEqual(self.compare(full=True,skin_change=True),0)

if __name__=='__main__':unittest.main()
