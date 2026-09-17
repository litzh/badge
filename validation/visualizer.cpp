#include "../firmware/badge/particle_field.h"
#include "../firmware/badge/audio_features.h"
#include <assert.h>
#include <stdio.h>
#include <vector>

AudioFeatures tone(float frequency) {
  AudioAnalyzer analyzer;
  int16_t block[512];
  AudioFeatures result;
  for (int k=0;k<30;++k) {
    for(int i=0;i<256;++i) {
      int16_t sample=(int16_t)(8000*sinf(6.283185f*frequency*(k*256+i)/16000));
      block[i*2]=sample; block[i*2+1]=-sample; // Opposite phase microphones must not cancel.
    }
    result=analyzer.process(block,256);
  }
  return result;
}

int main(int argc,char **argv) {
  AudioAnalyzer analyzer;
  int16_t zeros[512]={};
  assert(analyzer.process(zeros,256).level==0);
  assert(analyzer.process(zeros,0).level==0);
  auto low=tone(80),mid=tone(800),high=tone(6000);
  assert(low.low>low.mid && low.low>low.high);
  assert(mid.mid>mid.low && mid.mid>mid.high);
  assert(high.high>high.low);
  assert(low.level>.8f && high.level>.8f);
  int16_t dc[512]; for(auto &v:dc)v=10000;
  for(int i=0;i<300;++i)analyzer.process(dc,256);
  assert(analyzer.process(dc,256).level==0);

  ParticleField field;
  VisualInputs input;
  for(int i=0;i<100000;++i) {
    input.ax=sinf(i*.011f)*4;input.ay=cosf(i*.013f)*4;input.az=1;
    input.gz=sinf(i*.018f)*500;input.audio=(i%100)/100.0f;
    input.temperature=i%1000<500?-40:85;
    field.step(input,.067f);
    for(const auto &p:field.points) {
      assert(isfinite(p.x)&&isfinite(p.y)&&isfinite(p.vx)&&isfinite(p.vy));
      assert(hypotf(p.x-233,p.y-233)<210.01f);
    }
  }
  input.ax=NAN;input.ay=INFINITY;input.gz=NAN;input.temperature=NAN;input.audio=NAN;
  field.step(input,NAN);
  assert(isfinite(field.temperature)&&isfinite(field.energy));
  ParticleField quiet,tilted;
  input={}; input.ax=.8f;
  for(int i=0;i<300;++i){quiet.step({},.067f);tilted.step(input,.067f);}
  float qx=0,tx=0;for(int i=0;i<ParticleField::COUNT;++i){qx+=quiet.points[i].x;tx+=tilted.points[i].x;}
  assert(tx>qx+ParticleField::COUNT*20);
  // Render with guard words on each end and verify circular panel bounds.
  std::vector<uint16_t> buffer(ParticleField::SIZE*ParticleField::SIZE+2,0);
  buffer.front()=0xA123;buffer.back()=0xB234;
  field=ParticleField();input={};input.audio=.65f;input.bands[0]=.7f;input.bands[1]=.4f;input.bands[2]=.2f;
  for(int i=0;i<180;++i){field.step(input,.067f);field.render(buffer.data()+1,.067f);}
  assert(buffer.front()==0xA123&&buffer.back()==0xB234);
  size_t lit=0;
  for(int y=0;y<466;++y)for(int x=0;x<466;++x){
    if(buffer[1+y*466+x]){++lit;assert(hypotf(x-233,y-233)<216);}
  }
  assert(lit>100 && lit<466*466/5);
  if(argc>1){
    FILE *f=fopen(argv[1],"wb");assert(f);
    fprintf(f,"P6\n466 466\n255\n");
    for(int i=1;i<=466*466;++i){uint16_t p=buffer[i];unsigned char rgb[]={
      (unsigned char)((p>>11)*255/31),(unsigned char)(((p>>5)&63)*255/63),(unsigned char)((p&31)*255/31)};
      fwrite(rgb,1,3,f);}
    fclose(f);
  }
  printf("PASS audio bands/DC rejection; 100000 physics steps; boundaries; frame guard words; lit pixels=%zu\n",lit);
}
