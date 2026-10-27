"""Export immutable native PSO sources retained by the completed geometry probe."""
import hashlib
import struct


def export(reader,symbols,destination):
    address=lambda name:int(symbols['CyberpunkVR_RetainedGeometryShader'+name],16)
    count,=struct.unpack('<I',reader.read(address('Count'),4))
    row_bytes,=struct.unpack('<I',reader.read(address('RowBytes'),4))
    pointer,=struct.unpack('<Q',reader.read(address('Rows'),8))
    if count>256 or row_bytes!=88 or (count and not pointer):raise ValueError('Invalid native shader snapshot publication')
    rows=struct.iter_unpack('<11Q',reader.read(pointer,count*row_bytes) if count else b'')
    destination.mkdir(parents=True,exist_ok=True)
    result=[];ids=set()
    for row in rows:
        identity,pipeline,vs,vbytes,ps,pbytes,root,rbytes,inputs,icount,ibytes=row
        if not identity or identity in ids or not pipeline or not 0<vbytes<=16*1024*1024 or pbytes>16*1024*1024 or not 0<rbytes<=1024*1024 or icount>32 or ibytes!=88:
            raise ValueError('Invalid immutable shader/input snapshot')
        ids.add(identity);entry=dict(id=identity,pipeline=hex(pipeline),input=[])
        for name,source,size in (('vs',vs,vbytes),('ps',ps,pbytes),('root',root,rbytes)):
            if size and not source:raise ValueError('Missing shader snapshot payload')
            data=reader.read(source,size) if size else b'';digest=hashlib.sha256(data).hexdigest();path=destination/(digest+'.bin')
            if path.exists():
                if path.read_bytes()!=data:raise ValueError('Conflicting shader snapshot artifact')
            else:path.write_bytes(data)
            entry[name]=dict(sha256=digest,bytes=size,file=path.name)
        if icount and not inputs:raise ValueError('Missing shader input layout')
        for name,index,fmt,slot,offset,classification,step in struct.iter_unpack('<64s6I',reader.read(inputs,icount*ibytes) if icount else b''):
            if b'\0' not in name:raise ValueError('Unterminated input semantic')
            semantic=name.split(b'\0',1)[0].decode('ascii')
            if not semantic or slot>=32 or classification not in (0,1):raise ValueError('Invalid input-layout element')
            entry['input'].append(dict(semantic=semantic,index=index,format=fmt,slot=slot,offset=offset,classification=classification,step=step))
        result.append(entry)
    return dict(records=result,directory=destination.name,limitation='Exact creation sources and PSO lifetime identities, not resource contents or paired-render admission.')
