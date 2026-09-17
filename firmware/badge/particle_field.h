#pragma once
#include <math.h>
#include <stdint.h>
#include <string.h>

// Platform-independent simulation and RGB565 renderer; also used for host QA.
struct VisualInputs {
  float audio = 0, bands[3] = {}, ax = 0, ay = 0, az = 1, gz = 0, temperature = 32;
};

class ParticleField {
public:
  static constexpr int SIZE = 466, COUNT = 80;
  struct Particle { float x, y, vx, vy, phase; uint8_t band, size; };
  Particle points[COUNT];
  float energy = 0, temperature = 32, gravityX = 0, gravityY = 0, spin = 0, impulse = 0;
private:
  float time = 0, bands[3] = {}, fadeCarry = 0;
  uint32_t randomState = 123891;
  float random() { randomState = randomState * 1664525U + 1013904223U; return (randomState >> 8) / 16777216.0f; }
  static float limit(float v, float low, float high) { return fminf(high, fmaxf(low, v)); }
  static float safe(float v, float fallback = 0) { return isfinite(v) ? v : fallback; }
  static void blend(float &a, float b, float speed, float dt) { a += (b - a) * (1 - expf(-speed * dt)); }
  static uint16_t color(float h, float value) {
    h = fmodf(h, 360) / 60;
    float c = value * .72f, x = c * (1 - fabsf(fmodf(h, 2) - 1)), m = value - c;
    float r = 0, g = 0, b = 0;
    if (h < 1) { r=c; g=x; } else if (h < 2) { r=x; g=c; }
    else if (h < 3) { g=c; b=x; } else if (h < 4) { g=x; b=c; }
    else if (h < 5) { r=x; b=c; } else { r=c; b=x; }
    return ((uint16_t)((r+m)*31) << 11) | ((uint16_t)((g+m)*63) << 5) | (uint16_t)((b+m)*31);
  }
  static void dot(uint16_t *pixels, int x, int y, int radius, uint16_t rgb) {
    for (int yy = -radius; yy <= radius; ++yy) {
      for (int xx = -radius; xx <= radius; ++xx) {
        if (xx*xx+yy*yy > radius*radius || x+xx < 0 || y+yy < 0 || x+xx >= SIZE || y+yy >= SIZE) continue;
        uint16_t &p = pixels[(y+yy)*SIZE+x+xx];
        // Saturating RGB565 addition; no bright fixed background.
        unsigned r = (p >> 11) + (rgb >> 11), g = ((p >> 5)&63) + ((rgb >> 5)&63), b = (p&31) + (rgb&31);
        p = ((r>31?31:r)<<11) | ((g>63?63:g)<<5) | (b>31?31:b);
      }
    }
  }
public:
  ParticleField() {
    for (int i=0; i<COUNT; ++i) {
      float a=random()*6.283185f, radius=30+random()*120;
      points[i] = {233+cosf(a)*radius,233+sinf(a)*radius,0,0,random()*6.283185f,
                   (uint8_t)(i%3),(uint8_t)(2+i%2)};
    }
  }
  void step(VisualInputs in, float dt) {
    dt=limit(safe(dt,.067f),.001f,.12f);
    time+=dt;
    if(time>3600)time-=3600; // Keep trig precision on long-running devices.
    float target=limit(safe(in.audio),0,1);
    blend(energy,target,target>energy?12:3,dt);
    blend(temperature,limit(safe(in.temperature,32),-40,85),.08f,dt);
    blend(gravityX,limit(safe(in.ax),-1,1),3,dt);
    blend(gravityY,limit(-safe(in.ay),-1,1),3,dt);
    blend(spin,limit(safe(in.gz)/120,-1.5f,1.5f),4,dt);
    float ax=limit(safe(in.ax),-4,4), ay=limit(safe(in.ay),-4,4), az=limit(safe(in.az,1),-4,4);
    float shake=limit((fabsf(sqrtf(ax*ax+ay*ay+az*az)-1)-.2f)*1.5f,0,1);
    impulse=fmaxf(shake,impulse*expf(-dt*1.8f));
    for(int i=0;i<3;++i)blend(bands[i],limit(safe(in.bands[i]),0,1),5,dt);
    float warmth=limit((temperature-24)/22,0,1);
    float cx=233+gravityX*110+sinf(time*.19f)*18,cy=233+gravityY*110+cosf(time*.15f)*17;
    for(int i=0;i<COUNT;++i){
      auto &p=points[i];
      float dx=p.x-cx,dy=p.y-cy,dist=fmaxf(1,sqrtf(dx*dx+dy*dy));
      float targetRadius=56+28*p.band+26*sinf(p.phase+time*.23f)+bands[p.band]*55+impulse*45;
      float tangent=13+warmth*12+energy*24+spin*42;
      float spring=(targetRadius-dist)*.7f;
      p.vx+=(dx/dist*spring-dy/dist*tangent+gravityX*9+sinf(p.y*.026f+time*.48f+p.phase)*9-p.vx*.75f)*dt;
      p.vy+=(dy/dist*spring+dx/dist*tangent+gravityY*9+cosf(p.x*.03f-time*.31f)*9-p.vy*.75f)*dt;
      p.x+=p.vx*dt;p.y+=p.vy*dt;
      float bx=p.x-233,by=p.y-233,r=sqrtf(bx*bx+by*by);
      if(r>210){float nx=bx/r,ny=by/r;p.x=233+nx*210;p.y=233+ny*210;
        float speed=p.vx*nx+p.vy*ny;if(speed>0){p.vx-=speed*nx*1.5f;p.vy-=speed*ny*1.5f;}}
    }
  }
  void render(uint16_t *pixels, float dt) {
    if(!pixels)return;
    fadeCarry+=limit(safe(dt,.067f),.001f,.12f)*15;
    // Decay at ~15 Hz independent of actual display rate, all channels reach zero.
    int passes=(int)fadeCarry;fadeCarry-=passes;
    for(int n=0;n<passes;++n)for(int i=0;i<SIZE*SIZE;++i){
      uint16_t p=pixels[i];if(!p)continue;
      unsigned r=(p>>11)*7/8,g=((p>>5)&63)*7/8,b=(p&31)*7/8;
      pixels[i]=(r<<11)|(g<<5)|b;
    }
    float temp=limit(temperature,20,50);
    float hue=temp<=32?165+(temp-20)*1.67f:temp<=40?185+(temp-32)*13.75f:295+(temp-40)*10;
    for(int i=0;i<COUNT;++i){
      const auto &p=points[i];
      float flicker=.65f+.35f*sinf(time*.9f+p.phase)*sinf(time*.9f+p.phase);
      float value=limit((.30f+energy*.4f+impulse*.1f)*flicker,0,.85f);
      float h=hue+p.band*16+11*sinf(p.phase+time*.16f);
      dot(pixels,(int)p.x,(int)p.y,p.size+2,color(h,value*.10f));
      dot(pixels,(int)p.x,(int)p.y,p.size,color(h,value));
    }
  }
};
