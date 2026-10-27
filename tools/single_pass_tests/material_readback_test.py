import struct
import unittest
from material_readback import decode,validate,hints,attach_descriptors,validate_textures,FIELDS,ROW_BYTES,ROW_FORMAT


class MaterialReadbackTests(unittest.TestCase):
    def fixture(self):
        raw=b'';blob=b''
        for side in (0,1):
            payload=bytearray(292);struct.pack_into('<I',payload,0,123+side)
            struct.pack_into('<f',payload,272,.5);struct.pack_into('<f',payload,288,2.)
            row=dict.fromkeys(FIELDS,0);row.update(sequence=side+1,frame=7,side=side,family=43,handle=side+1,
                nativeStatus=2,stage=3,payloadOffset=len(blob),payloadBytes=292)
            raw+=struct.pack(ROW_FORMAT,*(row[f] for f in FIELDS));blob+=payload
        return raw,blob

    def test_payloads(self):
        raw,blob=self.fixture();report=decode(raw,blob,True);validate(report)
        self.assertEqual([r['textureIndex'] for r in report['rows']],[123,124])
        self.assertEqual(report['rows'][0]['cutoffBits'],0x3f000000)
        self.assertEqual(report['rows'][0]['uvScaleBits'],0x40000000)

    def test_rejected_and_missing(self):
        raw,blob=self.fixture();row=list(struct.unpack(ROW_FORMAT,raw[:ROW_BYTES]));row[13]=4;row[14]=100;row[16]=0
        report=decode(struct.pack(ROW_FORMAT,*row),b'',True)
        with self.assertRaises(RuntimeError):validate(report)
        with self.assertRaises(RuntimeError):validate(decode(b'',b'',True))
        validate(decode(b'',b'',False))
        with self.assertRaises(RuntimeError):validate(decode(raw,blob,False))

    def test_invalid_payloads(self):
        raw,blob=self.fixture()
        for field,value in ((13,2),(14,40),(15,len(blob)+1),(16,291)):
            row=list(struct.unpack(ROW_FORMAT,raw[:ROW_BYTES]));row[field]=value
            with self.assertRaises(ValueError):decode(struct.pack(ROW_FORMAT,*row),blob,True)
        with self.assertRaises(ValueError):decode(raw[:-1],blob,True)
        report=decode(raw,blob,True);report['rows'][1]['payloadOffset']=0
        with self.assertRaises(RuntimeError):validate(report)

    def test_hints_and_fresh_confirmation(self):
        raw,blob=self.fixture();report=decode(raw,blob,True)
        self.assertEqual(hints(report),[(1,123),(2,124)])
        descriptors=[0]*60;descriptors[0]=2;descriptors[5]=4;descriptors[20]=2;descriptors[25]=5;descriptors[40]=4
        for start in (0,20):
            descriptors[start+1]=5;descriptors[start+2]=start+10;descriptors[start+4]=28
            descriptors[start+13]=3;descriptors[start+14]=128;descriptors[start+15]=64
        packed=struct.pack('<60Q',*descriptors)*2
        for row in report['rows']:
            row.update(hintedIndex=row['textureIndex'],descriptorMask=7,textureConfirmed=1)
        attach_descriptors(report,packed);validate_textures(report)
        report['rows'][0]['hintedIndex']+=1
        with self.assertRaises(ValueError):attach_descriptors(report,packed)
        report['rows'][0]['textureConfirmed']=0
        attach_descriptors(report,packed)
        with self.assertRaises(RuntimeError):validate_textures(report)
        with self.assertRaises(ValueError):attach_descriptors(report,packed[:-1])
        report['rows'][1]['handle']=1
        with self.assertRaises(ValueError):hints(report)
        report=decode(raw,blob,True)
        for row in report['rows']:row.update(hintedIndex=row['textureIndex'],descriptorMask=7,textureConfirmed=1)
        descriptors[2]=0;attach_descriptors(report,struct.pack('<60Q',*descriptors)*2)
        with self.assertRaises(RuntimeError):validate_textures(report)


if __name__=='__main__':unittest.main()
