#ifndef ROOD_HD_TEXTURES_GLSL
#define ROOD_HD_TEXTURES_GLSL
// Bindings are declared by the packet/native fragment entry points.
#ifdef ROOD_GL
uniform usampler2D hd_words;
uniform usampler2D hd_palettes;
uniform usampler2D hd_table;
uniform sampler2DArray hd_atlas;
#endif
// Upload and page entries have independent namespaces, even if CRC values collide.
bool hd_lookup(uint a,uint pal,uint kind,vec2 source,vec2 original_size,out vec4 color) {
    uint slot=(a*0x9e3779b1u ^ pal*0x85ebca6bu)&4095u;
    for(int probe=0;probe<4096;++probe,slot=(slot+1u)&4095u) {
        uvec4 size=texelFetch(hd_table,ivec2(int(slot),1),0);
        if(size.x==0u)return false;
        uvec4 key=texelFetch(hd_table,ivec2(int(slot),0),0);
        if(key.x!=a || key.y!=pal || size.z!=kind)continue;
        if(size.w==0u)return false;
        vec2 point=clamp(floor(source*vec2(size.xy)/original_size),vec2(0),vec2(size.xy)-1.0);
        // key.w: the atlas layer (high 16 bits) and the row in it
        color=texelFetch(hd_atlas,ivec3(int(key.z)+int(point.x),int(key.w&65535u)+int(point.y),int(key.w>>16)),0);
        if(kind==1u && color.a>0.0 && color.a<1.5/255.0)return false;
        return true;
    }
    return false;
}
bool hd_texel(vec2 uv,uint page,uint clut,uint win,out vec4 color) {
    if(textureSize(hd_words,0).x!=1024)return false;
    vec2 f=fract(uv);
    uvec2 q=uvec2(ivec2(floor(uv)))&255u;
    uvec2 mask=uvec2(win&31u,(win>>5)&31u)*8u;
    q=(q&~mask)|((uvec2((win>>10)&31u,(win>>15)&31u)*8u)&mask);
    uint depth=(page>>7)&3u;
    uint ppw=depth==0u?4u:depth==1u?2u:1u;
    uint pal=depth<2u?texelFetch(hd_palettes,ivec2(int(clut&63u),int((clut>>6)&511u)+(depth==1u?512:0)),0).r:0u;
    ivec2 at=ivec2((page&15u)*64u+q.x/ppw,((page>>4)&1u)*256u+q.y);
    uvec4 word=texelFetch(hd_words,at&ivec2(1023,511),0);
    vec2 source=vec2(float((word.y&65535u)*ppw+q.x%ppw),float(word.y>>16))+f;
    if(word.z!=0u && hd_lookup(word.x,pal,0u,source,vec2(word.z*ppw,word.w),color))return true;
    return hd_lookup(page&0x19fu,pal,1u,vec2(q)+f,vec2(256),color);
}
// PNG alpha follows Beetle: 0 clear, 255 opaque, 1..254 PS1 semi-transparent.
void hd_apply(vec4 hd, inout vec3 texel, inout uint t) {
    texel = hd.rgb * 31.0;
    t = hd.a == 0.0 ? 0u : hd.a < 1.0 ? 0x8000u : 1u;
}
#endif
