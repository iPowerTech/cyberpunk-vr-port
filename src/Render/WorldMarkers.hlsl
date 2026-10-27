cbuffer Camera : register(b1) { float4 camera[53]; };
cbuffer Object : register(b5) { float4 object[7]; };
cbuffer Material : register(b7) { float4 material[28]; };

struct Input { float3 position:POSITION; float2 uv:TEXCOORD; float4 color:COLOR; };

float MarkerDepth(float value) {
    return value < -65536 && value >= -147456 ? (-value-65536)/4096 : 0;
}

float4 Project(float3 position) {
    float vertexDepth=MarkerDepth(position.z);
    if(vertexDepth>0) position.z=0;
    // Glyphs carry their anchor in the affine matrix instead of each vertex.
    // Native composition batches these matrices without calling the ordinary
    // constant uploaders, so decoding only on the CPU leaves text far outside
    // the clip volume. Decode here as well, before applying the ink camera.
    float textDepth=object[2].x==0 && object[2].y==0 && object[2].z==1
        ? MarkerDepth(object[2].w) : 0;
    float invDepth=textDepth>0 ? textDepth : vertexDepth;
    float4 local=float4(position,1);
    float3 world=float3(dot(object[0],local),dot(object[1],local),dot(object[2],local));
    if(textDepth>0) world.z=dot(object[2].xyz,position);
    float4 p=float4(world,1);
    float4 clip=float4(dot(camera[0],p),dot(camera[1],p),dot(camera[2],p),dot(camera[3],p));
    // Zero in MAIN; signed IPD/tan(horizontal half-FOV) in VRCAM's flat camera.
    if(invDepth>0) clip.x += camera[0].z*invDepth*clip.w;
    return clip;
}

struct Sprite { float4 position:SV_Position; float4 uv:TEXCOORD0; float3 color:TEXCOORD1; };
Sprite SpriteMain(Input i) {
    Sprite o;
    o.position=Project(i.position);
    float z=i.position.z < -65536 && i.position.z >= -147456 ? 0 : i.position.z;
    o.uv=float4(i.uv,z,i.color.x);o.color=i.color.yzw;
    return o;
}

struct Procedural { float4 position:SV_Position; float4 uv:TEXCOORD0; float4 color:TEXCOORD1; };
Procedural ProceduralMain(Input i,uint id:SV_VertexID) {
    Procedural o;
    o.position=Project(i.position);
    float2 corner=float2(id==1 || id==2 || id==4 ? material[1].x : 0,
                         id==2 || id==4 || id==5 ? material[1].y : 0);
    o.uv=float4(i.uv,corner);o.color=i.color;
    return o;
}
