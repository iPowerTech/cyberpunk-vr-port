struct Varying { float4 position:SV_Position; nointerpolation float4 color:TEXCOORD0; };
cbuffer Camera:b0 {uint eyeBase;uint objectIndex;}
Varying VS(uint vertex:SV_VertexID,uint eye:SV_ViewID,uint instance:SV_InstanceID) {
    eye+=eyeBase;
    float2 p[3]={float2(-.65,-.55),float2(.55,-.55),float2(0,.65)};
    float2 shift[4]={float2(0,0),float2(.25,.1),float2(-.25,.05),float2(0,-.25)};
    Varying o;o.position=float4(p[vertex]+float2(eye*.16+instance*.05,instance*.04)+shift[min(objectIndex,3)],.6+objectIndex*.035,1);
    o.color=float4(.2+eye*.35,.3+objectIndex*.12,.7,1);return o;
}
struct Targets { float4 a:SV_Target0;float4 b:SV_Target1;float4 c:SV_Target2; };
Targets PS(Varying i) {
    Targets o;o.a=float4(frac(i.position.xy*float2(.013,.017)),i.color.zw);o.b=float4(i.position.z,i.color.yz,1);o.c=i.color.wzyx;return o;
}
