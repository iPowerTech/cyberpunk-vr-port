// The 2.5 flat ink family (engine\shader_final.cache; WorldMarkerShader.cpp lists them). CameraShaderConsts ends
// with the eye the pass draws: 0 a one-view frame, 1 left / 2 right in the engine's own stereo frame.
cbuffer Camera : register(b1) {
    float4 camera[53]; int cameraEye; float3 markerPadding;
    float4 markerEyePosition, markerRight, markerUp, markerForward, markerProjection;
};
cbuffer Object : register(b5) { float4 object[7]; };
cbuffer Material : register(b7) { float4 material[32]; };

struct Input { float3 position:POSITION; float2 uv:TEXCOORD; float4 color:COLOR; };

float MarkerDepth(float value) {
    return value < -65536 && value >= -147456 ? (-value-65536)/4096 : 0;
}

// Glyphs carry their anchor in the affine matrix instead of each vertex.
// Native composition batches these matrices without calling the ordinary
// constant uploaders, so decoding only on the CPU leaves text far outside
// the clip volume. Decode here as well, before applying the ink camera.
float TextDepth() {
    return object[2].x==0 && object[2].y==0 && object[2].z==1 ? MarkerDepth(object[2].w) : 0;
}

bool WorldVertex(float alpha) {
    return (asuint(alpha)&0x80000000u)!=0;
}
bool WorldText() { return object[2].z==-16384; }

float4 Project(float3 position,bool worldVertex) {
    if(worldVertex) {
        // Never interpret cached world coordinates as canvas pixels if this
        // pass has no matching eye camera (for example a menu transition).
        if(markerEyePosition.w!=538012) return float4(0,0,-1,1);
        // Sprites carry world coordinates directly. Text shares local glyph
        // vertices and carries the lifted world plane in its affine matrix.
        float3 world=position;
        if(WorldText()) {
            float4 glyph=float4(position.xy,0,1);
            world=float3(dot(object[0],glyph),dot(object[1],glyph),dot(object[2],glyph));
        }
        float3 delta=world-markerEyePosition.xyz;
        float3 view=float3(dot(delta,markerRight.xyz),dot(delta,markerUp.xyz),dot(delta,markerForward.xyz));
        if(view.z<=0.01) return float4(0,0,-1,1);
        return float4(view.xy*markerProjection.xy+view.z*markerProjection.zw,0,view.z);
    }
    float vertexDepth=MarkerDepth(position.z);
    if(vertexDepth>0) position.z=0;
    float textDepth=TextDepth();
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
    bool world=WorldVertex(i.color.w) || WorldText();
    if(WorldVertex(i.color.w))i.color.w=abs(i.color.w);
    o.position=Project(i.position,world);
    float z=world || (i.position.z < -65536 && i.position.z >= -147456) ? 0 : i.position.z;
    o.uv=float4(i.uv,z,i.color.x);o.color=i.color.yzw;
    return o;
}

// THE ENGINE'S STEREO TERM. The stereo variants move each draw by its own stereo offset: right in the left eye,
// left in the right one, nothing in a one-view frame. They differ only in where the offset comes from -- a
// material slot, and a second one when the first is zero -- and one of them leaves an eye past the second alone.
// The offset is the 3D monitor's disparity, not the headset's. A marker carrying the port's depth tag is already
// at its point's depth (the parallax above) and takes none of it: with it, a quest marker 46 m away stood 110 px
// outside its car in each eye (2.5 engine stereo, Quest 3 profile), the same with the port's parallax or its
// canvas move switched off. The rest of the HUD keeps the engine's term, its widget depth (~2.8 m).
Sprite StereoSprite(Input i,float offset,bool twoEyesOnly) {
    Sprite o=SpriteMain(i);
    if(WorldVertex(i.color.w) || WorldText() || TextDepth()>0 || MarkerDepth(i.position.z)>0) return o;
    o.position.x+=cameraEye==1 ? offset : cameraEye==0 || (twoEyesOnly && cameraEye!=2) ? 0 : -offset;
    return o;
}
Sprite SpriteStereoMain(Input i) { return StereoSprite(i,material[1].x!=0 ? material[1].x : material[2].x,false); }
Sprite TextStereoMain(Input i) { return StereoSprite(i,material[2].x!=0 ? material[2].x : material[1].x,false); }
Sprite SpriteStereoZMain(Input i) { return StereoSprite(i,material[1].z!=0 ? material[1].z : material[2].x,false); }
Sprite SpriteStereoOneMain(Input i) { return StereoSprite(i,material[1].x,true); }

struct Procedural { float4 position:SV_Position; float4 uv:TEXCOORD0; float4 color:TEXCOORD1; };
Procedural ProceduralMain(Input i,uint id:SV_VertexID) {
    Procedural o;
    bool world=WorldVertex(i.color.w) || WorldText();
    if(WorldVertex(i.color.w))i.color.w=abs(i.color.w);
    o.position=Project(i.position,world);
    float2 corner=float2(id==1 || id==2 || id==4 ? material[1].x : 0,
                         id==2 || id==4 || id==5 ? material[1].y : 0);
    o.uv=float4(i.uv,corner);o.color=i.color;
    return o;
}
