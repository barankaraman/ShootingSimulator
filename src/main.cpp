/*
 * main.cpp — Iron Man Hand Shooter  (NYC Street Edition v2)
 *
 * Yeni özellikler:
 *  - Başlangıç menüsü (START / SETTINGS / EXIT)
 *  - Settings: oyun süresi seçimi (30 / 60 / 120 saniye)
 *  - Laser ateş sesi (WAV üretilip afplay ile çalınır)
 *  - Ekranda "HIT!" yazısı (vuruşta belirir, solar)
 *  - Sol üstte canlı skor
 *  - Sağ üstte geri sayım zamanlayıcı
 *  - Süre dolunca GAME OVER ekranı + skor
 */

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "Shader.h"
#include "Model.h"
#include "stb_truetype.h"

#include <iostream>
#include <thread>
#include <mutex>
#include <atomic>
#include <vector>
#include <random>
#include <chrono>
#include <cstring>
#include <cmath>
#include <cstdio>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

// ═══════════════════════════════════════════════════════════════
//  Sabitler
// ═══════════════════════════════════════════════════════════════
static constexpr int   WIN_W     = 1280;
static constexpr int   WIN_H     = 720;
static constexpr int   UDP_PORT  = 5005;
static constexpr int   BUF_SIZE  = 512;
static constexpr int   TARGET_N  = 8;
static constexpr float RESPAWN_T = 3.5f;
static const char* FONT_PATH = "/System/Library/Fonts/Supplemental/Arial.ttf";
static const char* LASER_WAV = "assets/sounds/laser.wav";

// ── Model yolları ─────────────────────────────────────────────
static const std::vector<std::string> HUMAN_MODELS = {
    "assets/models/humans/character-a.obj",
    "assets/models/humans/character-b.obj",
    "assets/models/humans/character-c.obj",
    "assets/models/humans/character-d.obj",
    "assets/models/humans/character-e.obj",
    "assets/models/humans/character-f.obj",
    "assets/models/humans/character-g.obj",
    "assets/models/humans/character-h.obj",
    "assets/models/humans/character-i.obj",
    "assets/models/humans/character-j.obj",
};
static const std::string WEAPON_MODEL = "assets/models/extras/blaster-a.obj";

// ═══════════════════════════════════════════════════════════════
//  Oyun Durumu
// ═══════════════════════════════════════════════════════════════
enum class GameState { MENU, SETTINGS, PLAYING, GAMEOVER };

// ═══════════════════════════════════════════════════════════════
//  Thread-safe el verisi + UDP
// ═══════════════════════════════════════════════════════════════
struct HandData { float x=0.5f, y=0.5f; int state=0; bool detected=false; };
static std::mutex        g_mu;
static HandData          g_hand;
static std::atomic<bool> g_running{true};

static float findF(const char* s,const char* k){const char* p=strstr(s,k);if(!p)return 0.f;p+=strlen(k);while(*p==':'||*p==' ')++p;return(float)atof(p);}
static int   findI(const char* s,const char* k){const char* p=strstr(s,k);if(!p)return 0;p+=strlen(k);while(*p==':'||*p==' ')++p;return atoi(p);}
static bool  findB(const char* s,const char* k){const char* p=strstr(s,k);if(!p)return false;p+=strlen(k);while(*p==':'||*p==' ')++p;return strncmp(p,"true",4)==0;}

static void udpThread() {
    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock < 0) return;
    sockaddr_in a{}; a.sin_family=AF_INET; a.sin_addr.s_addr=INADDR_ANY; a.sin_port=htons(UDP_PORT);
    if (bind(sock,(sockaddr*)&a,sizeof(a))<0){ close(sock); return; }
    timeval tv{0,100000}; setsockopt(sock,SOL_SOCKET,SO_RCVTIMEO,&tv,sizeof(tv));
    std::cout<<"[UDP] Port "<<UDP_PORT<<" dinleniyor\n";
    char buf[BUF_SIZE]; sockaddr_in fr{}; socklen_t fl=sizeof(fr);
    while(g_running){
        int n=(int)recvfrom(sock,buf,BUF_SIZE-1,0,(sockaddr*)&fr,&fl);
        if(n>0){ buf[n]='\0'; HandData d;
            d.x=findF(buf,"\"x\""); d.y=findF(buf,"\"y\"");
            d.state=findI(buf,"\"state\""); d.detected=findB(buf,"\"detected\"");
            std::lock_guard<std::mutex> lk(g_mu); g_hand=d; }
    }
    close(sock);
}

// ═══════════════════════════════════════════════════════════════
//  Ses sistemi — laser.wav oluştur + çal
// ═══════════════════════════════════════════════════════════════
static void generateLaserWav() {
    system("mkdir -p assets/sounds");
    const int SR = 22050, SAMPLES = SR * 28 / 100; // 280ms
    std::vector<int16_t> pcm(SAMPLES);
    float phase = 0;
    for (int i = 0; i < SAMPLES; ++i) {
        float t = (float)i / SAMPLES;
        float freq = 1100.f * std::exp(-4.5f * t) + 90.f;
        float amp  = std::exp(-3.8f * t) * 0.85f;
        float s = std::sin(phase) + 0.28f * std::sin(phase * 2.f);
        pcm[i] = (int16_t)(s / 1.28f * amp * 29000);
        phase += 2.f * (float)M_PI * freq / SR;
    }
    FILE* f = fopen(LASER_WAV, "wb");
    if (!f) return;
    uint32_t dataSz = SAMPLES * 2, fileSz = 36 + dataSz, sr = SR, br = SR * 2;
    uint16_t ch = 1, fmt = 1, ba = 2, bps = 16;
    fwrite("RIFF",1,4,f); fwrite(&fileSz,4,1,f); fwrite("WAVE",1,4,f);
    fwrite("fmt ",1,4,f); uint32_t fs16=16; fwrite(&fs16,4,1,f);
    fwrite(&fmt,2,1,f); fwrite(&ch,2,1,f); fwrite(&sr,4,1,f);
    fwrite(&br,4,1,f);  fwrite(&ba,2,1,f); fwrite(&bps,2,1,f);
    fwrite("data",1,4,f); fwrite(&dataSz,4,1,f);
    fwrite(pcm.data(),2,SAMPLES,f);
    fclose(f);
}

static void playLaser() {
    std::thread([]{ system("afplay -v 0.85 assets/sounds/laser.wav"); }).detach();
}

// ═══════════════════════════════════════════════════════════════
//  GLSL Shader'ları
// ═══════════════════════════════════════════════════════════════

static const char* VS_SKY = R"glsl(
#version 330 core
layout(location=0) in vec2 aPos;
out vec2 vUV;
void main(){ vUV = aPos * 0.5 + 0.5; gl_Position = vec4(aPos, 0.9999, 1.0); }
)glsl";

static const char* FS_SKY = R"glsl(
#version 330 core
in vec2 vUV; out vec4 FragColor; uniform float uTime;
float hash(vec2 p){ return fract(sin(dot(p,vec2(127.1,311.7)))*43758.5); }
float noise(vec2 p){
    vec2 i=floor(p),f=fract(p); f=f*f*(3.0-2.0*f);
    return mix(mix(hash(i),hash(i+vec2(1,0)),f.x),mix(hash(i+vec2(0,1)),hash(i+vec2(1,1)),f.x),f.y);
}
float fbm(vec2 p){ return noise(p)*0.5+noise(p*2.1)*0.25+noise(p*4.3)*0.125; }
void main(){
    vec3 sky=mix(vec3(0.60,0.78,0.95),vec3(0.18,0.42,0.82),pow(vUV.y,0.7));
    vec2 cuv=vec2(vUV.x*3.0+uTime*0.015,vUV.y*1.5+0.3);
    float cl=smoothstep(0.48,0.72,fbm(cuv))*smoothstep(0.0,0.3,vUV.y);
    FragColor=vec4(mix(sky,vec3(1.0),cl*0.85),1.0);
}
)glsl";

static const char* VS_GROUND = R"glsl(
#version 330 core
layout(location=0) in vec3 aPos; layout(location=1) in vec3 aNormal; layout(location=2) in vec2 aUV;
out vec3 vWorld; out vec3 vNorm; out vec2 vUV; out float vFog;
uniform mat4 model,view,projection; uniform vec3 uCamPos;
void main(){
    vec4 w=model*vec4(aPos,1.0); vWorld=w.xyz;
    vNorm=normalize(mat3(transpose(inverse(model)))*aNormal); vUV=aUV;
    vFog=exp(-length(w.xyz-uCamPos)*0.012); gl_Position=projection*view*w;
}
)glsl";

static const char* FS_GROUND = R"glsl(
#version 330 core
in vec3 vWorld; in vec3 vNorm; in vec2 vUV; in float vFog;
out vec4 FragColor; uniform vec3 uFogColor,uSunDir;
void main(){
    vec2 wp=vWorld.xz; float ax=abs(wp.x); bool isRoad=ax<6.5;
    vec3 base;
    if(isRoad){
        float n=fract(sin(dot(floor(wp*0.5),vec2(12.9,78.2)))*43758.5)*0.06;
        base=vec3(0.18+n,0.19+n,0.20+n);
        float dash=step(0.5,fract(wp.y*0.1));
        base=mix(base,vec3(0.95,0.82,0.05),step(abs(wp.x),0.12)*dash*0.9);
        base=mix(base,vec3(0.95),step(abs(abs(wp.x)-3.2),0.08)*dash*0.85);
        float cw=step(abs(wp.y-3.0),2.5)*step(ax,6.3);
        base=mix(base,vec3(0.90),cw*step(0.55,fract(wp.x*0.55))*0.7);
    } else {
        vec2 tile=floor(wp*0.5);
        base=mix(vec3(0.72,0.70,0.67),vec3(0.65,0.63,0.60),mod(tile.x+tile.y,2.0));
        vec2 frc=fract(wp*0.5);
        base*=(0.80+step(min(frc.x,frc.y),0.04)*0.20);
    }
    float diff=max(dot(normalize(vNorm),uSunDir),0.0)*0.55+0.45;
    FragColor=vec4(mix(uFogColor,base*diff,clamp(vFog,0.0,1.0)),1.0);
}
)glsl";

static const char* VS_BLDG = R"glsl(
#version 330 core
layout(location=0) in vec3 aPos; layout(location=1) in vec3 aNormal;
out vec3 vWorld; out vec3 vNorm; out float vFog;
uniform mat4 model,view,projection; uniform vec3 uCamPos;
void main(){
    vec4 w=model*vec4(aPos,1.0); vWorld=w.xyz;
    vNorm=normalize(mat3(transpose(inverse(model)))*aNormal);
    vFog=exp(-length(w.xyz-uCamPos)*0.012); gl_Position=projection*view*w;
}
)glsl";

static const char* FS_BLDG = R"glsl(
#version 330 core
in vec3 vWorld; in vec3 vNorm; in float vFog; out vec4 FragColor;
uniform vec3 uFogColor,uSunDir,uBldgColor; uniform float uBldgHeight,uTime;
float hash(vec2 p){ return fract(sin(dot(p,vec2(127.1,311.7)))*43758.5); }
void main(){
    vec2 fuv; float anx=abs(vNorm.x),anz=abs(vNorm.z);
    fuv = (anx>anz) ? vec2(vWorld.z,vWorld.y) : vec2(vWorld.x,vWorld.y);
    float wRows=uBldgHeight/3.0;
    vec2 wuv=vec2(fuv.x/2.5*4.0,fuv.y/uBldgHeight*wRows);
    vec2 wfrc=fract(wuv); vec2 wid=floor(wuv);
    bool isWin=wfrc.x>0.15&&wfrc.x<0.85&&wfrc.y>0.12&&wfrc.y<0.88;
    bool winOn=hash(wid+floor(vec2(uBldgColor.r*7.0,uBldgColor.g*13.0)))>0.35;
    float brickY=floor(fuv.y*3.5),brickX=floor(fuv.x*4.0+brickY*0.5);
    float mortar=step(min(fract(fuv.y*3.5),fract(fuv.x*4.0+brickY*0.5)),0.06);
    vec3 facade=mix(uBldgColor,uBldgColor*0.72,mortar);
    vec3 base;
    if(isWin){ base=winOn?mix(vec3(0.95,0.90,0.65),vec3(0.75,0.82,1.0),hash(wid*3.7)):vec3(0.08,0.10,0.14); }
    else { base=facade; if(fuv.y<3.5) base*=0.65; }
    float diff=max(dot(vNorm,uSunDir),0.0)*0.50+0.35;
    if(isWin&&winOn) diff=0.92;
    FragColor=vec4(mix(uFogColor,base*diff,clamp(vFog,0.0,1.0)),1.0);
}
)glsl";

static const char* VS_PROP = R"glsl(
#version 330 core
layout(location=0) in vec3 aPos; layout(location=1) in vec3 aNormal;
out vec3 vNorm; out vec3 vWorld; out float vFog;
uniform mat4 model,view,projection; uniform vec3 uCamPos;
void main(){
    vec4 w=model*vec4(aPos,1.0); vWorld=w.xyz;
    vNorm=normalize(mat3(transpose(inverse(model)))*aNormal);
    vFog=exp(-length(w.xyz-uCamPos)*0.012); gl_Position=projection*view*w;
}
)glsl";

static const char* FS_PROP = R"glsl(
#version 330 core
in vec3 vNorm; in vec3 vWorld; in float vFog; out vec4 FragColor;
uniform vec3 uColor,uFogColor,uSunDir;
void main(){
    float diff=max(dot(normalize(vNorm),uSunDir),0.0)*0.55+0.40;
    FragColor=vec4(mix(uFogColor,uColor*diff,clamp(vFog,0.0,1.0)),1.0);
}
)glsl";

static const char* VS_CHAR = R"glsl(
#version 330 core
layout(location=0) in vec3 aPos; layout(location=1) in vec3 aNormal; layout(location=2) in vec2 aUV;
out vec3 vWorld; out vec3 vNorm; out vec2 vUV; out float vFog;
uniform mat4 model,view,projection; uniform vec3 uCamPos;
void main(){
    vec4 w=model*vec4(aPos,1.0); vWorld=w.xyz;
    vNorm=normalize(mat3(transpose(inverse(model)))*aNormal); vUV=aUV;
    vFog=exp(-length(w.xyz-uCamPos)*0.012); gl_Position=projection*view*w;
}
)glsl";

static const char* FS_CHAR = R"glsl(
#version 330 core
in vec3 vWorld; in vec3 vNorm; in vec2 vUV; in float vFog; out vec4 FragColor;
uniform vec3 uColor,uFogColor,uSunDir,uCamPos;
uniform bool uUseTexture; uniform sampler2D uTexture; uniform float uHitFlash;
void main(){
    vec3 base=uUseTexture?texture(uTexture,vUV).rgb:uColor;
    float diff=max(dot(normalize(vNorm),uSunDir),0.0)*0.65+0.35;
    vec3 V=normalize(uCamPos-vWorld); vec3 H=normalize(uSunDir+V);
    float sp=pow(max(dot(normalize(vNorm),H),0.0),16.0)*0.2;
    vec3 lit=mix(base*diff+vec3(1.0)*sp,vec3(1.0,0.1,0.05),uHitFlash);
    FragColor=vec4(mix(uFogColor,lit,clamp(vFog,0.0,1.0)),1.0);
}
)glsl";

static const char* VS_WEAP = R"glsl(
#version 330 core
layout(location=0) in vec3 aPos; layout(location=1) in vec3 aNormal; layout(location=2) in vec2 aUV;
out vec3 vNorm; out vec2 vUV; out vec3 vVP;
uniform mat4 model,projection;
void main(){
    vec4 vp=model*vec4(aPos,1.0); vVP=vp.xyz;
    vNorm=normalize(mat3(transpose(inverse(model)))*aNormal);
    vUV=aUV; gl_Position=projection*vp;
}
)glsl";

static const char* FS_WEAP = R"glsl(
#version 330 core
in vec3 vNorm; in vec2 vUV; in vec3 vVP; out vec4 FragColor;
uniform bool uUseTexture; uniform sampler2D uTexture;
const vec3 GOLD=vec3(0.83,0.64,0.10);
void main(){
    vec3 base=uUseTexture?texture(uTexture,vUV).rgb:GOLD;
    base=mix(GOLD,base,0.30);
    vec3 L=normalize(vec3(0.3,0.8,0.5));
    float d=max(dot(normalize(vNorm),L),0.0);
    vec3 H=normalize(L+normalize(-vVP));
    float sp=pow(max(dot(normalize(vNorm),H),0.0),64.0);
    float redness=max(base.r-base.g-base.b,0.0);
    vec3 col=base*(0.35+d*0.60)+vec3(1.0,0.85,0.3)*sp*0.55+vec3(1.0,0.1,0.0)*redness*0.5;
    FragColor=vec4(col,1.0);
}
)glsl";

// ── Crosshair / UI çizgi ─────────────────────────────────────
static const char* VS_2D = R"glsl(
#version 330 core
layout(location=0) in vec2 aPos;
uniform vec2 uOffset; uniform float uScale;
void main(){ gl_Position=vec4(aPos*uScale+uOffset,0.0,1.0); }
)glsl";
static const char* FS_2D = R"glsl(
#version 330 core
out vec4 FragColor; uniform vec4 uColor;
void main(){ FragColor=uColor; }
)glsl";

// ── UI düz renk quad ─────────────────────────────────────────
static const char* VS_UI = R"glsl(
#version 330 core
layout(location=0) in vec2 aPos;
void main(){ gl_Position=vec4(aPos,0.0,1.0); }
)glsl";
// FS_UI: FS_2D ile aynı (uColor)

// ── Metin ────────────────────────────────────────────────────
static const char* VS_TEXT = R"glsl(
#version 330 core
layout(location=0) in vec2 aPos;
layout(location=1) in vec2 aUV;
out vec2 vUV;
void main(){ vUV=aUV; gl_Position=vec4(aPos,0.0,1.0); }
)glsl";
static const char* FS_TEXT = R"glsl(
#version 330 core
in vec2 vUV; out vec4 FragColor;
uniform sampler2D uAtlas; uniform vec4 uColor;
void main(){ float a=texture(uAtlas,vUV).r; FragColor=vec4(uColor.rgb,uColor.a*a); }
)glsl";

// ═══════════════════════════════════════════════════════════════
//  TextRenderer — stb_truetype
// ═══════════════════════════════════════════════════════════════
struct TextRenderer {
    GLuint atlasID=0, vao=0, vbo=0;
    stbtt_bakedchar glyphs[96];
    int AW=512, AH=512;
    bool ok=false;

    bool init(const char* fontPath) {
        FILE* f=fopen(fontPath,"rb");
        if(!f){ std::cerr<<"[Font] Bulunamadı: "<<fontPath<<"\n"; return false; }
        fseek(f,0,SEEK_END); int sz=(int)ftell(f); rewind(f);
        std::vector<uint8_t> buf(sz);
        fread(buf.data(),1,sz,f); fclose(f);
        int off=stbtt_GetFontOffsetForIndex(buf.data(),0);
        if(off<0){ std::cerr<<"[Font] Offset hatası\n"; return false; }
        std::vector<uint8_t> pix(AW*AH,0);
        int r=stbtt_BakeFontBitmap(buf.data(),off,52.f,pix.data(),AW,AH,32,96,glyphs);
        if(r<=0){ std::cerr<<"[Font] Atlas küçük\n"; return false; }
        glGenTextures(1,&atlasID);
        glBindTexture(GL_TEXTURE_2D,atlasID);
        glPixelStorei(GL_UNPACK_ALIGNMENT,1);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RED,AW,AH,0,GL_RED,GL_UNSIGNED_BYTE,pix.data());
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        glGenVertexArrays(1,&vao); glGenBuffers(1,&vbo);
        glBindVertexArray(vao); glBindBuffer(GL_ARRAY_BUFFER,vbo);
        glBufferData(GL_ARRAY_BUFFER,6*4*sizeof(float),nullptr,GL_DYNAMIC_DRAW);
        glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,4*sizeof(float),(void*)0); glEnableVertexAttribArray(0);
        glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,4*sizeof(float),(void*)(2*sizeof(float))); glEnableVertexAttribArray(1);
        glBindVertexArray(0);
        ok=true; std::cout<<"[Font] Yüklendi: "<<fontPath<<"\n"; return true;
    }

    // px, py: ekran piksel koordinatı (sol-üst köşe baz alınır)
    void draw(Shader& sh, const std::string& text, float px, float py,
              float scale, glm::vec4 color) {
        if(!ok) return;
        sh.use(); sh.setVec4("uColor",color); sh.setInt("uAtlas",0);
        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D,atlasID);
        glBindVertexArray(vao); glBindBuffer(GL_ARRAY_BUFFER,vbo);
        float x=px, y=py;
        auto nx=[](float v){ return v/WIN_W*2.f-1.f; };
        auto ny=[](float v){ return 1.f-v/WIN_H*2.f; };
        for(char c:text){
            if(c<32||c>127) continue;
            stbtt_aligned_quad q;
            stbtt_GetBakedQuad(glyphs,AW,AH,c-32,&x,&y,&q,1);
            float sx0=px+(q.x0-px)*scale, sx1=px+(q.x1-px)*scale;
            float sy0=py+(q.y0-py)*scale, sy1=py+(q.y1-py)*scale;
            float v[]={
                nx(sx0),ny(sy0), q.s0,1.f-q.t0,
                nx(sx1),ny(sy0), q.s1,1.f-q.t0,
                nx(sx1),ny(sy1), q.s1,1.f-q.t1,
                nx(sx0),ny(sy0), q.s0,1.f-q.t0,
                nx(sx1),ny(sy1), q.s1,1.f-q.t1,
                nx(sx0),ny(sy1), q.s0,1.f-q.t1,
            };
            glBufferSubData(GL_ARRAY_BUFFER,0,sizeof(v),v);
            glDrawArrays(GL_TRIANGLES,0,6);
        }
    }

    // Metnin piksel genişliğini ölç (scale=1 için)
    float measure(const std::string& text) {
        if(!ok) return 0;
        float x=0,y=0;
        for(char c:text){
            if(c<32||c>127) continue;
            stbtt_aligned_quad q;
            stbtt_GetBakedQuad(glyphs,AW,AH,c-32,&x,&y,&q,1);
        }
        return x;
    }
};

// ═══════════════════════════════════════════════════════════════
//  Geometri Yardımcıları
// ═══════════════════════════════════════════════════════════════
static GLuint makeGroundVAO(float W,float D){
    float hw=W*.5f,hd=D*.5f;
    float V[]={-hw,0,-hd,0,1,0,0,hd*2, hw,0,-hd,0,1,0,W,hd*2, hw,0,hd,0,1,0,W,0, -hw,0,hd,0,1,0,0,0};
    unsigned I[]={0,1,2,0,2,3};
    GLuint vao,vbo,ebo;
    glGenVertexArrays(1,&vao); glGenBuffers(1,&vbo); glGenBuffers(1,&ebo);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER,vbo); glBufferData(GL_ARRAY_BUFFER,sizeof(V),V,GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,ebo); glBufferData(GL_ELEMENT_ARRAY_BUFFER,sizeof(I),I,GL_STATIC_DRAW);
    glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,32,(void*)0);  glEnableVertexAttribArray(0);
    glVertexAttribPointer(1,3,GL_FLOAT,GL_FALSE,32,(void*)12); glEnableVertexAttribArray(1);
    glVertexAttribPointer(2,2,GL_FLOAT,GL_FALSE,32,(void*)24); glEnableVertexAttribArray(2);
    glBindVertexArray(0); return vao;
}

static GLuint makeBoxVAO(){
    float V[]={
        -0.5f,-0.5f,-0.5f,0,0,-1, 0.5f,-0.5f,-0.5f,0,0,-1, 0.5f,0.5f,-0.5f,0,0,-1,
         0.5f,0.5f,-0.5f,0,0,-1,-0.5f,0.5f,-0.5f,0,0,-1,-0.5f,-0.5f,-0.5f,0,0,-1,
        -0.5f,-0.5f,0.5f,0,0,1, 0.5f,-0.5f,0.5f,0,0,1, 0.5f,0.5f,0.5f,0,0,1,
         0.5f,0.5f,0.5f,0,0,1,-0.5f,0.5f,0.5f,0,0,1,-0.5f,-0.5f,0.5f,0,0,1,
        -0.5f,0.5f,0.5f,-1,0,0,-0.5f,0.5f,-0.5f,-1,0,0,-0.5f,-0.5f,-0.5f,-1,0,0,
        -0.5f,-0.5f,-0.5f,-1,0,0,-0.5f,-0.5f,0.5f,-1,0,0,-0.5f,0.5f,0.5f,-1,0,0,
         0.5f,0.5f,0.5f,1,0,0,0.5f,0.5f,-0.5f,1,0,0,0.5f,-0.5f,-0.5f,1,0,0,
         0.5f,-0.5f,-0.5f,1,0,0,0.5f,-0.5f,0.5f,1,0,0,0.5f,0.5f,0.5f,1,0,0,
        -0.5f,-0.5f,-0.5f,0,-1,0,0.5f,-0.5f,-0.5f,0,-1,0,0.5f,-0.5f,0.5f,0,-1,0,
         0.5f,-0.5f,0.5f,0,-1,0,-0.5f,-0.5f,0.5f,0,-1,0,-0.5f,-0.5f,-0.5f,0,-1,0,
        -0.5f,0.5f,-0.5f,0,1,0,0.5f,0.5f,-0.5f,0,1,0,0.5f,0.5f,0.5f,0,1,0,
         0.5f,0.5f,0.5f,0,1,0,-0.5f,0.5f,0.5f,0,1,0,-0.5f,0.5f,-0.5f,0,1,0,
    };
    GLuint vao,vbo;
    glGenVertexArrays(1,&vao); glGenBuffers(1,&vbo);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER,vbo); glBufferData(GL_ARRAY_BUFFER,sizeof(V),V,GL_STATIC_DRAW);
    glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,24,(void*)0);  glEnableVertexAttribArray(0);
    glVertexAttribPointer(1,3,GL_FLOAT,GL_FALSE,24,(void*)12); glEnableVertexAttribArray(1);
    glBindVertexArray(0); return vao;
}

static GLuint makeSkyVAO(){
    float V[]={-1,-1,1,-1,1,1,-1,1}; unsigned I[]={0,1,2,0,2,3};
    GLuint vao,vbo,ebo;
    glGenVertexArrays(1,&vao); glGenBuffers(1,&vbo); glGenBuffers(1,&ebo);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER,vbo); glBufferData(GL_ARRAY_BUFFER,sizeof(V),V,GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,ebo); glBufferData(GL_ELEMENT_ARRAY_BUFFER,sizeof(I),I,GL_STATIC_DRAW);
    glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,8,(void*)0); glEnableVertexAttribArray(0);
    glBindVertexArray(0); return vao;
}

static GLuint makeCrossVAO(int& n){
    const float S=0.035f,G=0.007f;
    float L[]={-S,0,-G,0, G,0,S,0, 0,G,0,S, 0,-S,0,-G};
    n=8;
    GLuint vao,vbo; glGenVertexArrays(1,&vao); glGenBuffers(1,&vbo);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER,vbo); glBufferData(GL_ARRAY_BUFFER,sizeof(L),L,GL_STATIC_DRAW);
    glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,8,(void*)0); glEnableVertexAttribArray(0);
    glBindVertexArray(0); return vao;
}

// Dinamik UI quad VAO (menü butonları, overlay vs.)
static GLuint uiVAO=0, uiVBO=0;
static void makeUiVAO(){
    glGenVertexArrays(1,&uiVAO); glGenBuffers(1,&uiVBO);
    glBindVertexArray(uiVAO); glBindBuffer(GL_ARRAY_BUFFER,uiVBO);
    glBufferData(GL_ARRAY_BUFFER,12*sizeof(float),nullptr,GL_DYNAMIC_DRAW);
    glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,8,(void*)0); glEnableVertexAttribArray(0);
    glBindVertexArray(0);
}

// pixel koordinatından NDC
static inline float px2ndc(float v){ return v/WIN_W*2.f-1.f; }
static inline float py2ndc(float v){ return 1.f-v/WIN_H*2.f; }

static void drawRect(Shader& sh, float x,float y,float w,float h, glm::vec4 col){
    float x0=px2ndc(x),y0=py2ndc(y),x1=px2ndc(x+w),y1=py2ndc(y+h);
    float v[]={x0,y0, x1,y0, x1,y1, x0,y0, x1,y1, x0,y1};
    glBindVertexArray(uiVAO); glBindBuffer(GL_ARRAY_BUFFER,uiVBO);
    glBufferSubData(GL_ARRAY_BUFFER,0,sizeof(v),v);
    sh.use(); sh.setVec4("uColor",col);
    glDrawArrays(GL_TRIANGLES,0,6);
}

// ═══════════════════════════════════════════════════════════════
//  Şehir Sahne Yapıları
// ═══════════════════════════════════════════════════════════════
struct Building { glm::vec3 pos,scale,color; };
struct Prop     { glm::vec3 pos,scale,color; };

static std::mt19937 rng(std::random_device{}());

static std::vector<Building> buildCity(){
    std::vector<Building> b;
    auto rf=[](float lo,float hi){return std::uniform_real_distribution<float>(lo,hi)(rng);};
    for(int side:{1,-1}){
        float z=-4.f;
        while(z>-80.f){
            float w=rf(8.f,16.f),h=rf(10.f,35.f),d=rf(8.f,14.f);
            glm::vec3 col={rf(0.55f,0.80f),rf(0.52f,0.75f),rf(0.48f,0.70f)};
            b.push_back({{side*(12.f+w*0.5f),h*0.5f,z-d*0.5f},{w,h,d},col});
            z-=d+rf(0.5f,2.f);
        }
    }
    return b;
}

static std::vector<Prop> buildProps(){
    std::vector<Prop> p;
    for(float z=-3.f;z>-75.f;z-=10.f){
        p.push_back({{ 8.5f,2.5f,z},{0.2f,5.0f,0.2f},{0.30f,0.30f,0.32f}});
        p.push_back({{-8.5f,2.5f,z},{0.2f,5.0f,0.2f},{0.30f,0.30f,0.32f}});
        p.push_back({{ 7.8f,5.15f,z},{1.5f,0.18f,0.18f},{0.30f,0.30f,0.32f}});
        p.push_back({{-7.8f,5.15f,z},{1.5f,0.18f,0.18f},{0.30f,0.30f,0.32f}});
        p.push_back({{ 7.1f,5.15f,z},{0.35f,0.35f,0.35f},{1.0f,0.92f,0.55f}});
        p.push_back({{-7.1f,5.15f,z},{0.35f,0.35f,0.35f},{1.0f,0.92f,0.55f}});
    }
    for(float z=-8.f;z>-70.f;z-=12.f){
        p.push_back({{ 9.5f,0.75f,z},{4.5f,1.5f,2.2f},{0.65f,0.12f,0.10f}});
        p.push_back({{-9.5f,0.75f,z-5.f},{4.5f,1.5f,2.2f},{0.20f,0.35f,0.55f}});
    }
    p.push_back({{ 8.0f,0.4f,-6.f},{0.5f,0.8f,0.5f},{0.15f,0.50f,0.15f}});
    p.push_back({{-8.0f,0.3f,-10.f},{0.3f,0.6f,0.3f},{0.72f,0.12f,0.10f}});
    return p;
}

// ═══════════════════════════════════════════════════════════════
//  Hedef (İnsan) Sistemi
// ═══════════════════════════════════════════════════════════════
struct Target {
    glm::vec3 pos; float scale,rotY,hitFlash; int modelIdx; bool alive; float bobPhase;
};

static const std::vector<glm::vec3> SPAWN_POINTS={
    { 5.5f,0.f,-8.f},{3.0f,0.f,-12.f},{-4.0f,0.f,-10.f},{-5.5f,0.f,-16.f},
    { 4.5f,0.f,-20.f},{-3.0f,0.f,-24.f},{ 2.0f,0.f,-18.f},{-2.0f,0.f,-28.f},
};

static Target spawnHuman(int idx,int mc){
    auto rf=[](float lo,float hi){return std::uniform_real_distribution<float>(lo,hi)(rng);};
    glm::vec3 base=SPAWN_POINTS[idx%SPAWN_POINTS.size()];
    return {base+glm::vec3(rf(-0.8f,0.8f),0.f,rf(-1.f,1.f)),
            rf(0.90f,1.10f),rf(-20.f,20.f),0.f,
            std::uniform_int_distribution<int>(0,mc-1)(rng),true,rf(0.f,6.28f)};
}

// ═══════════════════════════════════════════════════════════════
//  GLFW Callbacks
// ═══════════════════════════════════════════════════════════════
static void onKey(GLFWwindow* w,int k,int,int a,int){
    if(k==GLFW_KEY_ESCAPE&&a==GLFW_PRESS) glfwSetWindowShouldClose(w,GLFW_TRUE);
}
static void onResize(GLFWwindow*,int W,int H){ if(H>0) glViewport(0,0,W,H); }

// ═══════════════════════════════════════════════════════════════
//  main
// ═══════════════════════════════════════════════════════════════
int main(){
    // Laser WAV oluştur
    generateLaserWav();

    if(!glfwInit()){ std::cerr<<"GLFW hatası\n"; return -1; }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR,3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR,3);
    glfwWindowHint(GLFW_OPENGL_PROFILE,GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT,GL_TRUE);
    glfwWindowHint(GLFW_SAMPLES,4);

    GLFWwindow* win=glfwCreateWindow(WIN_W,WIN_H,"Iron Man Hand Shooter — NYC",nullptr,nullptr);
    if(!win){ glfwTerminate(); return -1; }
    glfwMakeContextCurrent(win); glfwSwapInterval(1);
    glfwSetKeyCallback(win,onKey); glfwSetFramebufferSizeCallback(win,onResize);

    glewExperimental=GL_TRUE;
    if(glewInit()!=GLEW_OK){ std::cerr<<"GLEW hatası\n"; return -1; }
    glEnable(GL_DEPTH_TEST); glEnable(GL_MULTISAMPLE);
    std::cout<<"[GL] "<<glGetString(GL_VERSION)<<"\n";

    std::thread udp(udpThread);

    // ── Shaderlar ─────────────────────────────────────────────
    Shader skySh  (VS_SKY,   FS_SKY);
    Shader gndSh  (VS_GROUND,FS_GROUND);
    Shader bldgSh (VS_BLDG,  FS_BLDG);
    Shader propSh (VS_PROP,  FS_PROP);
    Shader charSh (VS_CHAR,  FS_CHAR);
    Shader weapSh (VS_WEAP,  FS_WEAP);
    Shader crossSh(VS_2D,    FS_2D);
    Shader uiSh   (VS_UI,    FS_2D);    // aynı fragment
    Shader textSh (VS_TEXT,  FS_TEXT);

    // ── TextRenderer ─────────────────────────────────────────
    TextRenderer txr;
    txr.init(FONT_PATH);

    // ── İnsan modelleri ───────────────────────────────────────
    std::vector<Model> humans(HUMAN_MODELS.size());
    int loadedHumans=0;
    for(int i=0;i<(int)HUMAN_MODELS.size();++i)
        if(humans[i].load(HUMAN_MODELS[i])) ++loadedHumans;
    std::cout<<"[Model] "<<loadedHumans<<"/"<<HUMAN_MODELS.size()<<" karakter\n";

    Model weaponMdl; weaponMdl.load(WEAPON_MODEL);

    // ── Geometri ─────────────────────────────────────────────
    GLuint groundVAO=makeGroundVAO(40.f,100.f);
    GLuint boxVAO   =makeBoxVAO();
    GLuint skyVAO   =makeSkyVAO();
    int crossN=0;
    GLuint crossVAO =makeCrossVAO(crossN);
    makeUiVAO();

    auto buildings=buildCity();
    auto props    =buildProps();

    // ── Kamera ───────────────────────────────────────────────
    glm::vec3 camPos   ={0.f,1.7f,2.0f};
    glm::vec3 camTarget={0.f,1.5f,-15.f};
    glm::mat4 proj=glm::perspective(glm::radians(68.f),(float)WIN_W/WIN_H,0.05f,150.f);
    glm::mat4 view=glm::lookAt(camPos,camTarget,{0,1,0});
    glm::vec3 sunDir=glm::normalize(glm::vec3(0.4f,0.9f,0.3f));
    const glm::vec3 FOG={0.65f,0.75f,0.88f};

    // ── FPS Silah ─────────────────────────────────────────────
    glm::mat4 weapBase=glm::translate(glm::mat4(1.f),{0.52f,-0.46f,-0.90f});
    weapBase=glm::rotate(weapBase,glm::radians(-15.f),{0,1,0});
    weapBase=glm::rotate(weapBase,glm::radians(  8.f),{1,0,0});
    weapBase=glm::scale (weapBase,glm::vec3(0.55f));
    float weapRecoil=0.f, weapSwayX=0.f, weapSwayY=0.f;

    // ── Oyun durumu ───────────────────────────────────────────
    GameState state=GameState::MENU;
    int  score=0;
    int  gameDuration=60;          // settings'den değiştirilir
    float timeLeft=gameDuration;
    bool prevFist=false;
    float gameTime=0.f;
    float hitTextAlpha=0.f;        // "HIT!" efekti
    auto lastSpawn=std::chrono::steady_clock::now();
    auto lastTime =std::chrono::steady_clock::now();
    bool lbPrevPress=false;

    int mc=std::max(1,loadedHumans);
    std::vector<Target> targets(TARGET_N);
    for(int i=0;i<TARGET_N;++i) targets[i]=spawnHuman(i,mc);

    // Blend her zaman açık (metin için)
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA);

    // ── Render yardımcıları ───────────────────────────────────
    auto drawScene=[&](float gt){
        glClearColor(FOG.r,FOG.g,FOG.b,1.f);
        glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);

        glDisable(GL_DEPTH_TEST);
        skySh.use(); skySh.setFloat("uTime",gt);
        glBindVertexArray(skyVAO); glDrawElements(GL_TRIANGLES,6,GL_UNSIGNED_INT,nullptr);
        glEnable(GL_DEPTH_TEST);

        gndSh.use(); gndSh.setMat4("view",view); gndSh.setMat4("projection",proj);
        gndSh.setVec3("uCamPos",camPos); gndSh.setVec3("uFogColor",FOG); gndSh.setVec3("uSunDir",sunDir);
        glm::mat4 gm=glm::translate(glm::mat4(1.f),{0.f,0.f,-48.f});
        gndSh.setMat4("model",gm);
        glBindVertexArray(groundVAO); glDrawElements(GL_TRIANGLES,6,GL_UNSIGNED_INT,nullptr);

        bldgSh.use(); bldgSh.setMat4("view",view); bldgSh.setMat4("projection",proj);
        bldgSh.setVec3("uCamPos",camPos); bldgSh.setVec3("uFogColor",FOG);
        bldgSh.setVec3("uSunDir",sunDir); bldgSh.setFloat("uTime",gt);
        glBindVertexArray(boxVAO);
        for(auto& b:buildings){
            glm::mat4 m=glm::scale(glm::translate(glm::mat4(1.f),b.pos),b.scale);
            bldgSh.setMat4("model",m); bldgSh.setVec3("uBldgColor",b.color);
            bldgSh.setFloat("uBldgHeight",b.scale.y); glDrawArrays(GL_TRIANGLES,0,36);
        }

        propSh.use(); propSh.setMat4("view",view); propSh.setMat4("projection",proj);
        propSh.setVec3("uCamPos",camPos); propSh.setVec3("uFogColor",FOG); propSh.setVec3("uSunDir",sunDir);
        glBindVertexArray(boxVAO);
        for(auto& p:props){
            glm::mat4 m=glm::scale(glm::translate(glm::mat4(1.f),p.pos),p.scale);
            propSh.setMat4("model",m); propSh.setVec3("uColor",p.color);
            glDrawArrays(GL_TRIANGLES,0,36);
        }
    };

    auto drawCharacters=[&](float gt){
        charSh.use(); charSh.setMat4("view",view); charSh.setMat4("projection",proj);
        charSh.setVec3("uCamPos",camPos); charSh.setVec3("uFogColor",FOG); charSh.setVec3("uSunDir",sunDir);
        charSh.setInt("uTexture",0);
        for(auto& t:targets){
            if(!t.alive) continue;
            float bob=std::sin(gt*1.2f+t.bobPhase)*0.018f;
            glm::mat4 m=glm::scale(
                glm::rotate(glm::translate(glm::mat4(1.f),t.pos+glm::vec3(0,bob,0)),
                            glm::radians(t.rotY),{0,1,0}),
                glm::vec3(t.scale));
            charSh.setMat4("model",m); charSh.setFloat("uHitFlash",t.hitFlash);
            Model& mdl=humans[t.modelIdx%(int)humans.size()];
            if(!mdl.loaded) continue;
            for(auto& mesh:mdl.meshes){
                charSh.setVec3("uColor",mesh.color);
                if(mesh.hasTexture){ glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D,mesh.texID); charSh.setBool("uUseTexture",true); }
                else charSh.setBool("uUseTexture",false);
                mesh.draw();
            }
        }
    };

    auto drawWeapon=[&](float ndcX, float ndcY){
        if(!weaponMdl.loaded) return;
        glClear(GL_DEPTH_BUFFER_BIT);
        weapSh.use(); weapSh.setMat4("projection",proj); weapSh.setInt("uTexture",0);
        glm::mat4 recoil=glm::translate(glm::mat4(1.f),{0.f,weapRecoil*0.25f,weapRecoil});
        glm::mat4 sway=glm::rotate(
            glm::rotate(
                glm::translate(glm::mat4(1.f),{weapSwayX,weapSwayY,0.f}),
                glm::radians(weapSwayX*30.f),{0.f,1.f,0.f}),
            glm::radians(-weapSwayY*20.f),{1.f,0.f,0.f});
        glm::mat4 wm=sway*recoil*weapBase;
        weapSh.setMat4("model",wm);
        for(auto& mesh:weaponMdl.meshes){
            if(mesh.hasTexture){ glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D,mesh.texID); weapSh.setBool("uUseTexture",true); }
            else weapSh.setBool("uUseTexture",false);
            mesh.draw();
        }
    };

    // Buton çizimi: arka plan + metin
    auto drawButton=[&](float x,float y,float w,float h, const std::string& label,
                        bool hover, bool selected=false){
        glm::vec4 bgCol = selected ? glm::vec4(0.9f,0.6f,0.0f,0.90f)
                        : hover    ? glm::vec4(0.4f,0.4f,0.5f,0.85f)
                                   : glm::vec4(0.15f,0.15f,0.20f,0.80f);
        glDisable(GL_DEPTH_TEST);
        drawRect(uiSh,x,y,w,h,bgCol);
        float tw=txr.measure(label)*0.75f;
        float tx=x+(w-tw)*0.5f, ty=y+(h+52.f*0.75f*0.5f)*0.5f;
        txr.draw(textSh,label,tx,ty,0.75f,{1,1,1,1});
        glEnable(GL_DEPTH_TEST);
    };

    while(!glfwWindowShouldClose(win)){
        glfwPollEvents();
        auto now=std::chrono::steady_clock::now();
        float dt=std::min(std::chrono::duration<float>(now-lastTime).count(),0.05f);
        lastTime=now; gameTime+=dt;

        // Mouse durumu
        double mx,my; glfwGetCursorPos(win,&mx,&my);
        bool lbPress=(glfwGetMouseButton(win,GLFW_MOUSE_BUTTON_LEFT)==GLFW_PRESS);
        bool lbClick=lbPress&&!lbPrevPress; lbPrevPress=lbPress;

        // El verisi
        HandData hand; { std::lock_guard<std::mutex> lk(g_mu); hand=g_hand; }
        float ndcX=hand.x*2.f-1.f;
        float ndcY=-(hand.y*2.f-1.f);

        // ═══════════════════════════════════════════════════
        //  MENÜ
        // ═══════════════════════════════════════════════════
        if(state==GameState::MENU){
            glfwSetInputMode(win,GLFW_CURSOR,GLFW_CURSOR_NORMAL);

            drawScene(gameTime);
            glDisable(GL_DEPTH_TEST);
            // Koyu overlay
            drawRect(uiSh,0,0,WIN_W,WIN_H,{0.0f,0.0f,0.05f,0.62f});

            // Başlık
            float titleScale=1.6f;
            std::string title="IRON MAN HAND SHOOTER";
            float tw=txr.measure(title)*titleScale;
            txr.draw(textSh,title,(WIN_W-tw)*0.5f,WIN_H*0.22f,titleScale,{0.9f,0.55f,0.0f,1.0f});

            // Butonlar
            float bx=(WIN_W-320.f)*0.5f, by=WIN_H*0.38f, bw=320.f, bh=68.f, gap=18.f;
            bool hStart=(mx>=bx&&mx<=bx+bw&&my>=by      &&my<=by+bh);
            bool hSet  =(mx>=bx&&mx<=bx+bw&&my>=by+bh+gap&&my<=by+bh*2+gap);
            bool hExit =(mx>=bx&&mx<=bx+bw&&my>=by+(bh+gap)*2&&my<=by+(bh+gap)*2+bh);

            drawButton(bx,by,bw,bh,"START",hStart);
            drawButton(bx,by+bh+gap,bw,bh,"SETTINGS",hSet);
            drawButton(bx,by+(bh+gap)*2,bw,bh,"EXIT",hExit);

            if(lbClick){
                if(hStart){
                    // Oyunu başlat
                    score=0; timeLeft=gameDuration; prevFist=false; hitTextAlpha=0;
                    for(int i=0;i<TARGET_N;++i) targets[i]=spawnHuman(i,mc);
                    lastSpawn=now;
                    state=GameState::PLAYING;
                } else if(hSet){
                    state=GameState::SETTINGS;
                } else if(hExit){
                    glfwSetWindowShouldClose(win,GLFW_TRUE);
                }
            }
            glEnable(GL_DEPTH_TEST);
            glfwSwapBuffers(win); continue;
        }

        // ═══════════════════════════════════════════════════
        //  SETTINGS
        // ═══════════════════════════════════════════════════
        if(state==GameState::SETTINGS){
            glfwSetInputMode(win,GLFW_CURSOR,GLFW_CURSOR_NORMAL);
            drawScene(gameTime);
            glDisable(GL_DEPTH_TEST);
            drawRect(uiSh,0,0,WIN_W,WIN_H,{0.0f,0.0f,0.05f,0.70f});

            std::string stitle="SETTINGS";
            float stw=txr.measure(stitle)*1.2f;
            txr.draw(textSh,stitle,(WIN_W-stw)*0.5f,WIN_H*0.22f,1.2f,{0.9f,0.55f,0.0f,1.0f});

            std::string dlbl="GAME DURATION";
            float dtw=txr.measure(dlbl)*0.85f;
            txr.draw(textSh,dlbl,(WIN_W-dtw)*0.5f,WIN_H*0.38f,0.85f,{0.8f,0.8f,0.8f,1.0f});

            // Süre butonları yan yana
            float dbw=150.f,dbh=64.f,dgap=20.f;
            float totalW=dbw*3+dgap*2, dstartX=(WIN_W-totalW)*0.5f;
            float dby=WIN_H*0.44f;
            int durations[]={30,60,120};
            for(int i=0;i<3;++i){
                float bx=dstartX+i*(dbw+dgap);
                bool hov=(mx>=bx&&mx<=bx+dbw&&my>=dby&&my<=dby+dbh);
                bool sel=(gameDuration==durations[i]);
                drawButton(bx,dby,dbw,dbh,std::to_string(durations[i])+"s",hov,sel);
                if(lbClick&&hov) gameDuration=durations[i];
            }

            // BACK butonu
            float backX=(WIN_W-220.f)*0.5f, backY=WIN_H*0.62f;
            bool hBack=(mx>=backX&&mx<=backX+220.f&&my>=backY&&my<=backY+60.f);
            drawButton(backX,backY,220.f,60.f,"BACK",hBack);
            if(lbClick&&hBack) state=GameState::MENU;

            glEnable(GL_DEPTH_TEST);
            glfwSwapBuffers(win); continue;
        }

        // ═══════════════════════════════════════════════════
        //  GAME OVER
        // ═══════════════════════════════════════════════════
        if(state==GameState::GAMEOVER){
            glfwSetInputMode(win,GLFW_CURSOR,GLFW_CURSOR_NORMAL);
            drawScene(gameTime);
            glDisable(GL_DEPTH_TEST);
            drawRect(uiSh,0,0,WIN_W,WIN_H,{0.0f,0.0f,0.0f,0.72f});

            std::string go="GAME OVER";
            float gs=1.8f, gtw=txr.measure(go)*gs;
            txr.draw(textSh,go,(WIN_W-gtw)*0.5f,WIN_H*0.28f,gs,{0.9f,0.1f,0.1f,1.0f});

            std::string sc="Score: "+std::to_string(score);
            float ss=1.2f, stw=txr.measure(sc)*ss;
            txr.draw(textSh,sc,(WIN_W-stw)*0.5f,WIN_H*0.42f,ss,{1.0f,0.85f,0.2f,1.0f});

            std::string msg = (score>=20)?"Excellent!" : (score>=10)?"Well done!" : "Keep training!";
            float ms=0.85f, mtw=txr.measure(msg)*ms;
            txr.draw(textSh,msg,(WIN_W-mtw)*0.5f,WIN_H*0.50f,ms,{0.75f,0.75f,0.75f,1.0f});

            float bx=(WIN_W-260.f)*0.5f, by=WIN_H*0.60f;
            bool hAgain=(mx>=bx&&mx<=bx+260.f&&my>=by&&my<=by+64.f);
            bool hMenu =(mx>=bx&&mx<=bx+260.f&&my>=by+84.f&&my<=by+148.f);
            drawButton(bx,by,    260.f,64.f,"PLAY AGAIN",hAgain);
            drawButton(bx,by+84.f,260.f,64.f,"MAIN MENU",hMenu);

            if(lbClick&&hAgain){
                score=0; timeLeft=gameDuration; prevFist=false; hitTextAlpha=0;
                for(int i=0;i<TARGET_N;++i) targets[i]=spawnHuman(i,mc);
                lastSpawn=now;
                state=GameState::PLAYING;
            }
            if(lbClick&&hMenu) state=GameState::MENU;

            glEnable(GL_DEPTH_TEST);
            glfwSwapBuffers(win); continue;
        }

        // ═══════════════════════════════════════════════════
        //  PLAYING
        // ═══════════════════════════════════════════════════
        glfwSetInputMode(win,GLFW_CURSOR,GLFW_CURSOR_DISABLED);

        // Zamanlayıcı
        timeLeft-=dt;
        if(timeLeft<=0.f){ timeLeft=0.f; state=GameState::GAMEOVER; }

        // Ateş
        bool curFist=(hand.state==1&&hand.detected);
        bool justFired=curFist&&!prevFist; prevFist=curFist;
        if(justFired){
            playLaser();
            weapRecoil=0.09f;
            for(auto& t:targets){
                if(!t.alive) continue;
                glm::vec4 clip=proj*view*glm::vec4(t.pos+glm::vec3(0,1,0),1.f);
                if(clip.w<=0.f) continue;
                glm::vec2 tn={clip.x/clip.w,clip.y/clip.w};
                float dist=glm::length(tn-glm::vec2(ndcX,ndcY));
                float thresh=(t.scale*0.55f)/clip.w*proj[1][1]+0.06f;
                if(dist<thresh){
                    t.hitFlash=0.30f; t.alive=false; ++score;
                    hitTextAlpha=1.0f;
                    std::cout<<"HIT! Skor: "<<score<<"\n";
                }
            }
        }

        for(auto& t:targets) t.hitFlash=std::max(0.f,t.hitFlash-dt*3.f);
        weapRecoil=std::max(0.f,weapRecoil-dt*5.f);
        hitTextAlpha=std::max(0.f,hitTextAlpha-dt*1.8f);

        float targetSwayX=(hand.detected?ndcX:0.f)*0.22f;
        float targetSwayY=(hand.detected?ndcY:0.f)*0.07f;
        float lerpT=std::min(dt*7.f,1.f);
        weapSwayX+=(targetSwayX-weapSwayX)*lerpT;
        weapSwayY+=(targetSwayY-weapSwayY)*lerpT;

        if(std::chrono::duration<float>(now-lastSpawn).count()>RESPAWN_T){
            for(int i=0;i<(int)targets.size();++i)
                if(!targets[i].alive) targets[i]=spawnHuman(i,mc);
            lastSpawn=now;
        }

        // ── Render: 3D sahne ─────────────────────────────────
        drawScene(gameTime);
        drawCharacters(gameTime);
        drawWeapon(ndcX,ndcY);

        // ── Crosshair ─────────────────────────────────────────
        glDisable(GL_DEPTH_TEST);
        crossSh.use();
        crossSh.setVec2("uOffset",{ndcX,ndcY}); crossSh.setFloat("uScale",1.0f);
        crossSh.setVec4("uColor",curFist?glm::vec4(1.f,0.25f,0.f,1.f):glm::vec4(1.f,1.f,1.f,1.f));
        glLineWidth(2.f);
        glBindVertexArray(crossVAO); glDrawArrays(GL_LINES,0,crossN);

        // ── HUD: Skor (sol üst) ────────────────────────────────
        std::string scoreStr="SCORE: "+std::to_string(score);
        txr.draw(textSh,scoreStr,24.f,56.f,0.85f,{1.0f,0.85f,0.15f,1.0f});

        // ── HUD: Zamanlayıcı (sağ üst) ────────────────────────
        int tSec=(int)std::ceil(timeLeft);
        std::string timerStr="TIME: "+std::to_string(tSec)+"s";
        float timW=txr.measure(timerStr)*0.85f;
        glm::vec4 timeCol=(tSec<=10)?glm::vec4(1.f,0.2f,0.1f,1.f):glm::vec4(0.85f,0.85f,1.f,1.f);
        txr.draw(textSh,timerStr,WIN_W-timW-24.f,56.f,0.85f,timeCol);

        // ── HUD: HIT! yazısı ──────────────────────────────────
        if(hitTextAlpha>0.01f){
            std::string hitStr="HIT!";
            float hs=1.4f, htw=txr.measure(hitStr)*hs;
            txr.draw(textSh,hitStr,(WIN_W-htw)*0.5f,WIN_H*0.38f,hs,
                     {1.0f,0.25f,0.05f,hitTextAlpha});
        }

        glEnable(GL_DEPTH_TEST);
        glfwSwapBuffers(win);
    }

    g_running=false; udp.join();
    for(auto& m:humans) m.release();
    weaponMdl.release();
    glfwDestroyWindow(win); glfwTerminate();
    return 0;
}
