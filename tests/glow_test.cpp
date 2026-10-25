#include <windows.h>
#include <stdio.h>

extern "C" int __cdecl glow_available(void);
extern "C" int __cdecl glow_render(const DWORD *,DWORD *,unsigned,unsigned,unsigned,unsigned,COLORREF);

static int check(bool condition,const char *message)
{
    if (!condition) printf("FAIL: %s\n",message);
    return condition ? 0 : 1;
}

int main()
{
    DWORD source[64*64]={},first[64*64]={},second[64*64]={};
    int failures=0;
    if (!glow_available()) { puts("FAIL: Direct2D glow renderer is unavailable."); return 1; }
    for (int y=29;y<=34;y++) for (int x=29;x<=34;x++) source[y*64+x]=0xffffffff;
    failures+=check(glow_render(source,first,64,64,6,100,RGB(255,255,255))!=0,"render white glow");
    double weight=0,x_sum=0,y_sum=0;
    for (int y=0;y<64;y++) for (int x=0;x<64;x++)
    {
        DWORD alpha=first[y*64+x]>>24;
        weight+=alpha; x_sum+=x*alpha; y_sum+=y*alpha;
        failures+=check(((first[y*64+x]>>16)&255)<=alpha && ((first[y*64+x]>>8)&255)<=alpha && (first[y*64+x]&255)<=alpha,"premultiplied output");
    }
    failures+=check(weight>0,"glow contains coverage");
    failures+=check(x_sum/weight>31 && x_sum/weight<32 && y_sum/weight>31 && y_sum/weight<32,"glow stays centered on the source silhouette");
    failures+=check((first[28*64+31]>>24)>0,"halo extends beyond the source");
    failures+=check(glow_render(source,second,64,64,6,100,RGB(255,255,255))!=0,"cached render");
    failures+=check(memcmp(first,second,sizeof(first))==0,"cache preserves pixels exactly");
    failures+=check(glow_render(source,second,64,64,6,0,RGB(255,255,255))!=0,"zero strength render");
    DWORD combined=0;
    for (int i=0;i<64*64;i++) combined|=second[i];
    failures+=check(combined==0,"zero strength is fully transparent");
    failures+=check(glow_render(source,second,64,64,6,50,RGB(0,0,255))!=0,"colored glow render");
    double half_weight=0;
    for (int i=0;i<64*64;i++)
    {
        half_weight+=second[i]>>24;
        failures+=check((second[i]&0x00ffff00)==0,"blue glow has no red or green");
    }
    failures+=check(half_weight>weight*0.4 && half_weight<weight*0.6,"strength controls halo opacity");
    failures+=check(glow_render(source,second,64,64,12,100,RGB(255,255,255))!=0,"larger radius render");
    failures+=check((second[24*64+31]>>24)>(first[24*64+31]>>24),"increasing the radius spreads the halo outward");
    failures+=check(glow_render(source,second,64,64,1,100,RGB(255,255,255))!=0,"one pixel glow render");
    failures+=check((second[28*64+31]>>24)>0,"one pixel glow remains visible outside the silhouette");
    failures+=check(glow_render(source,second,64,64,0,100,RGB(255,255,255))!=0,"zero size render");
    combined=0;
    for (int i=0;i<64*64;i++) combined|=second[i];
    failures+=check(combined==0,"zero size is fully transparent");
    ZeroMemory(source,sizeof(source));
    failures+=check(glow_render(source,second,64,64,6,100,RGB(255,255,255))!=0,"empty mask render");
    combined=0;
    for (int i=0;i<64*64;i++) combined|=second[i];
    failures+=check(combined==0,"empty masks produce no halo");
    if (!failures) puts("PASS: Direct2D halo coverage, alignment, premultiplied alpha, color, strength, empty input, zero/one pixel sizes, and cache.");
    return failures ? 1 : 0;
}
