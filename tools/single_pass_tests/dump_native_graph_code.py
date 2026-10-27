"""Read PE exception-directory function boundaries and dump native graph compiler code."""
import argparse
import bisect
import hashlib
import json
from pathlib import Path
import struct


class Image:
    def __init__(self,data):
        self.data=data
        if data[:2]!=b'MZ':raise ValueError('Missing DOS header')
        nt,=struct.unpack_from('<I',data,0x3c)
        if data[nt:nt+4]!=b'PE\0\0':raise ValueError('Missing PE signature')
        machine,count,_,_,_,optional,_=struct.unpack_from('<HHIIIHH',data,nt+4)
        start=nt+24
        if machine!=0x8664 or struct.unpack_from('<H',data,start)[0]!=0x20b or optional<144:
            raise ValueError('Expected AMD64 PE32+')
        self.base,=struct.unpack_from('<Q',data,start+24)
        self.sections=[]
        for i in range(count):
            at=start+optional+i*40
            name,virtual,rva,size,raw=struct.unpack_from('<8s4I',data,at)
            if raw>len(data) or size>len(data)-raw:raise ValueError('Section outside image')
            self.sections.append((rva,size,raw,name.rstrip(b'\0').decode('ascii')))
        directory,size=struct.unpack_from('<II',data,start+112+3*8)
        if size%12 or size>16*1024*1024:raise ValueError('Invalid exception directory')
        self.functions=list(struct.iter_unpack('<III',self.read(directory,size)))
        if self.functions!=sorted(self.functions) or any(not begin<end for begin,end,_ in self.functions):
            raise ValueError('Invalid runtime functions')
        self.starts=[row[0] for row in self.functions]

    def read(self,rva,size):
        for start,length,raw,_ in self.sections:
            if start<=rva and rva-start<=length and size<=length-(rva-start):
                return self.data[raw+rva-start:raw+rva-start+size]
        raise ValueError(f'RVA span is not file-backed: {rva:x}/{size:x}')

    def function(self,rva):
        index=bisect.bisect_right(self.starts,rva)-1
        if index<0 or not self.functions[index][0]<=rva<self.functions[index][1]:
            raise ValueError(f'No runtime function for {rva:x}')
        return self.functions[index]

    def callers(self,target,decoder):
        result=[]
        for start,size,raw,name in self.sections:
            if name!='.text':continue
            body=self.data[raw:raw+size];at=0
            while True:
                at=body.find(b'\xe8',at)
                if at<0:break
                if at+5<=len(body) and start+at+5+struct.unpack_from('<i',body,at+1)[0]==target:
                    try:begin,end,_=self.function(start+at)
                    except ValueError:pass
                    else:
                        if any(i.address-self.base==start+at and i.mnemonic=='call'
                               for i in decoder.disasm(self.read(begin,end-begin),self.base+begin)):
                            result.append(dict(site=hex(start+at),function=hex(begin)))
                at+=1
        return result


def fingerprint(data):
    value=14695981039346656037
    for byte in data:value=((value^byte)*1099511628211)&(2**64-1)
    return f'{value:016x}'


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe',type=Path,required=True)
    parser.add_argument('--out',type=Path,required=True)
    parser.add_argument('--rva',type=lambda value:int(value,0),action='append',default=[])
    parser.add_argument('--span',action='append',default=[],help='Explicit RVA:bytes for a leaf without an exception entry')
    parser.add_argument('--callers-rva',type=lambda value:int(value,0),action='append',default=[])
    args=parser.parse_args()
    if not args.rva and not args.span and not args.callers_rva:parser.error('Provide an RVA, caller query or explicit span')
    import capstone
    image=Image(args.exe.read_bytes())
    decoder=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_64);decoder.detail=True
    args.out.mkdir(parents=True,exist_ok=True)
    rows=[]
    requests=[(target,None) for target in args.rva]
    requests.extend((int(value.split(':')[0],0),int(value.split(':')[1],0)) for value in args.span)
    callers={hex(target):image.callers(target,decoder) for target in args.callers_rva}
    for values in callers.values():
        for row in values:
            request=(int(row['function'],0),None)
            if request not in requests:requests.append(request)
    for target,length in requests:
        if length is None:begin,end,unwind=image.function(target)
        else:
            if not 0<length<=1024*1024:raise ValueError('Invalid explicit span')
            begin,end,unwind=target,target+length,None
        body=image.read(begin,end-begin)
        lines=[];calls=[]
        for instruction in decoder.disasm(body,image.base+begin):
            lines.append(f'{instruction.address-image.base:08x} {instruction.bytes.hex():24s} {instruction.mnemonic} {instruction.op_str}')
            if instruction.mnemonic=='call':
                immediate=instruction.operands[0].type==capstone.x86.X86_OP_IMM
                destination=instruction.operands[0].imm-image.base if immediate else None
                calls.append(dict(site=hex(instruction.address-image.base),target=hex(destination) if destination is not None else None,operand=instruction.op_str))
        name=f'native-{begin:x}.asm'
        (args.out/name).write_text('\n'.join(lines)+'\n',encoding='ascii')
        rows.append(dict(requested=hex(target),begin=hex(begin),end=hex(end),unwind=hex(unwind) if unwind is not None else None,
                         boundary='exception-directory' if length is None else 'explicit-span',
                         bytes=len(body),fnv64=fingerprint(body),sha256=hashlib.sha256(body).hexdigest(),file=name,calls=calls))
    result=dict(imageSha256=hashlib.sha256(image.data).hexdigest(),imageBase=hex(image.base),functions=rows,callers=callers)
    (args.out/'index.json').write_text(json.dumps(result,indent=2)+'\n',encoding='ascii')
    print(json.dumps(result,indent=2))


if __name__=='__main__':main()
