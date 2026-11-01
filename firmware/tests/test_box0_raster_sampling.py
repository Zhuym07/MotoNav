"""Build an Android executable exercising the actual BOX0 raster sampler.

Run the resulting executable on an ARM64 Android device. An optional raster
packet argument renders the same real map with nearest and smooth sampling.
"""
import os
from pathlib import Path
import subprocess

from test_box0_bounds import ROOT, DEFAULT_NDK, function


SUPPORT = r'''
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <vector>
#include "RasterContinuity.h"
using std::min;
using std::max;
constexpr int LCD_WIDTH=240, LCD_HEIGHT=240, CX=120, MAP_PY=150, PIXB=3;
int rasW=32, rasH=32, rasBW=2, rasX0=0, rasY0=0, rasterFrame=1;
float rasMpp=2;
bool rasterBackValid=false, rasHasPal=false, rasterViceValid=false;
uint8_t viceBaseShift=13;
uint16_t *rasterVice=nullptr;
std::vector<uint8_t> data(512), back(512);
uint8_t *rasterBuf=data.data(), *rasterBackBuf=back.data();
RasterHeader rasterBackHeader={};
struct {float cx,cy,sc,cs,sn;uint16_t pal[16];uint16_t *fb;bool pix2;} RM;
std::vector<uint16_t> frame(240*240+2,0xa55a);
void setPixel(int x,int y,int color) {
  int k=(((y>>4)*rasBW+(x>>4))<<8)|((y&15)<<4)|(x&15);
  data[k>>1]=(data[k>>1]&((k&1)?15:240))|(color<<((k&1)?4:0));
}
'''

TESTS = r'''
void testSampling() {
  RM.pal[0]=0;RM.pal[1]=0xffff;RM.fb=frame.data()+1;
  for(int y=0;y<32;y++)for(int x=16;x<32;x++)setPixel(x,y,1);
  // Centers retain exact source colors, including either side of a block seam.
  for(int y=0;y<32;y++)for(int x=0;x<32;x++)
    assert(rasterSmoothColor((x<<16)+32768,(y<<16)+32768,false,0,0)==(x<16?0:0xffff));
  assert(rasterSmoothColor(16<<16,10<<16,false,0,0)==blendF(0,0xffff,128));
  int previous=-1;
  for(int f=0;f<256;f++) {
    auto c=rasterSmoothColor((15<<16)+32768+(f<<8),10<<16,false,0,0);
    int green=(c>>5)&63;assert(green>=previous);previous=green;
  }
  // Outside both map tiles is background; overlapping previous tiles remain usable.
  rasterBackHeader.width=32;rasterBackHeader.height=32;rasterBackHeader.blocksWide=2;
  std::fill(back.begin(),back.end(),0x11);
  assert(rasterSmoothColor(-65536,0,false,0,0)==0);
  assert(rasterSmoothColor(-65536,10<<16,true,8,0)==0xffff);
  for(int angle=0;angle<360;angle+=7)for(float scale:{0.2f,0.5f,1.0f,2.0f}) {
    RM.cx=32;RM.cy=-32;RM.sc=scale;
    RM.cs=cosf(angle*0.01745329252f);RM.sn=sinf(angle*0.01745329252f);
    RM.pix2=false;rasterRows(-10,250,1);
    assert(frame.front()==0xa55a && frame.back()==0xa55a);
    RM.pix2=true;rasterRows(0,240,1);
    assert(frame.front()==0xa55a && frame.back()==0xa55a);
    for(int y=0;y<240;y+=3)for(int x=0;x<240;x+=3)
      assert(RM.fb[y*240+x]==RM.fb[y*240+x+1] && RM.fb[y*240+x]==RM.fb[y*240+x+2]);
  }
  puts("PASS source centers, block seams, subpixel coverage, previous tile, 52 rotations x 4 scales, framebuffer guards, pixel theme");
}
void image(const char *path) {
  FILE *f=fopen(path,"wb");assert(f);fprintf(f,"P6\n240 240\n255\n");
  for(int i=0;i<240*240;i++) {
    auto c=RM.fb[i];unsigned char p[3]={(unsigned char)((c>>11)*255/31),
      (unsigned char)(((c>>5)&63)*255/63),(unsigned char)((c&31)*255/31)};
    fwrite(p,1,3,f);
  }
  fclose(f);
}
void realMap(const char *path) {
  std::ifstream f(path,std::ios::binary);
  std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(f)),{});
  RasterHeader h={};assert(rasterCheck(bytes.data(),bytes.size(),h,1300*1300,1000000));
  data.resize(h.bytes);rasterBuf=data.data();rasterDecode(bytes.data(),h,rasterBuf);
  rasW=h.width;rasH=h.height;rasBW=h.blocksWide;rasX0=h.x0;rasY0=h.y0;rasMpp=h.mm/1000.f;
  RM.pal[0]=0xdedb;RM.pal[1]=0x3186;RM.pal[2]=0x5deb;RM.pal[3]=0x4c9b;
  RM.pal[4]=blendF(RM.pal[0],RM.pal[1],64);RM.pal[5]=blendF(RM.pal[0],RM.pal[1],112);
  RM.pal[6]=RM.pal[4];RM.pal[7]=RM.pal[5];
  RM.cx=rasX0+rasW*rasMpp/2;RM.cy=rasY0-rasH*rasMpp/2;
  RM.sc=0.6f;RM.cs=cosf(0.47f);RM.sn=sinf(0.47f);RM.pix2=false;
  for(int y=0;y<240;y++)for(int x=0;x<240;x++) {
    float rx=(x+0.5f-CX)/RM.sc,ry=(MAP_PY-y-0.5f)/RM.sc;
    int u=(int)((RM.cx+rx*RM.cs+ry*RM.sn-rasX0)/rasMpp);
    int v=(int)((rasY0-RM.cy+rx*RM.sn-ry*RM.cs)/rasMpp);
    RM.fb[y*240+x]=rasterColorAt(u,v,false,0,0);
  }
  image("raster-before.ppm");rasterRows(0,240,1);image("raster-after.ppm");
  assert(frame.front()==0xa55a && frame.back()==0xa55a);
  puts("PASS actual building map decoded and rendered before/after");
}
int main(int argc,char **argv) {
  testSampling();if(argc>1)realMap(argv[1]);
}
'''


def main():
    sketch = ROOT / "firmware/boards/box0/MotoNav/MotoNav.ino"
    source = sketch.read_text(encoding="utf-8")
    out = ROOT / "artifacts/box0-test"
    out.mkdir(parents=True, exist_ok=True)
    definitions = []
    for name in ("blendF", "rasterColorAt", "rasterSmoothColor", "rasterRows"):
        line, code = function(source, name)
        definitions.append(f'#line {line} "{sketch.as_posix()}"\n{code}\n')
    cpp = out / "raster-sampling-test.cpp"
    cpp.write_text(SUPPORT + "\n".join(definitions) + TESTS, encoding="utf-8")
    prebuilt = DEFAULT_NDK / "toolchains/llvm/prebuilt/windows-x86_64"
    subprocess.run([str(prebuilt / "bin/clang++.exe"),
        "--target=aarch64-linux-android26", f"--sysroot={prebuilt / 'sysroot'}",
        "-std=c++17", "-O1", "-g", "-Wall", "-Wextra", "-Werror", "-UNDEBUG",
        "-static-libstdc++", "-I", str(sketch.parent), str(cpp),
        "-o", str(out / "raster-sampling-test")], check=True,
        env=dict(os.environ, TMP=str(out), TEMP=str(out)))
    print("Built BOX0 production raster sampling regression")


if __name__ == "__main__":
    main()
