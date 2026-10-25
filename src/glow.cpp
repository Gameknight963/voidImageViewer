// Copyright (c) 2026. Distributed under the project's MIT license.
// Independent implementation using documented Microsoft Direct2D effects:
// https://learn.microsoft.com/windows/win32/direct2d/morphology
// https://learn.microsoft.com/windows/win32/direct2d/how-to-apply-effects-to-primitives
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <initguid.h>
#include <d3d11.h>
#include <d2d1_1.h>
#include <d2d1effects.h>

static ID3D11Device *glow_d3d;
static ID2D1Device *glow_device;
static ID2D1DeviceContext *glow_context;
static HMODULE glow_d3d_module, glow_d2d_module;
static bool glow_attempted;

template<class T> static void glow_release(T *&object)
{
    if (object) { object->Release(); object = 0; }
}

extern "C" int __cdecl glow_available(void)
{
    if (glow_context) return 1;
    if (glow_attempted) return 0;
    glow_attempted = true;
    glow_d3d_module = LoadLibraryW(L"d3d11.dll");
    glow_d2d_module = LoadLibraryW(L"d2d1.dll");
    if (!glow_d3d_module || !glow_d2d_module) return 0;
    typedef HRESULT (WINAPI *CreateD3D)(IDXGIAdapter *,D3D_DRIVER_TYPE,HMODULE,UINT,const D3D_FEATURE_LEVEL *,UINT,UINT,ID3D11Device **,D3D_FEATURE_LEVEL *,ID3D11DeviceContext **);
    typedef HRESULT (WINAPI *CreateD2D)(IDXGIDevice *,const D2D1_CREATION_PROPERTIES *,ID2D1Device **);
    CreateD3D create_d3d = reinterpret_cast<CreateD3D>(GetProcAddress(glow_d3d_module,"D3D11CreateDevice"));
    CreateD2D create_d2d = reinterpret_cast<CreateD2D>(GetProcAddress(glow_d2d_module,"D2D1CreateDevice"));
    if (!create_d3d || !create_d2d) return 0;
    HRESULT result = create_d3d(0,D3D_DRIVER_TYPE_HARDWARE,0,D3D11_CREATE_DEVICE_BGRA_SUPPORT,0,0,D3D11_SDK_VERSION,&glow_d3d,0,0);
    if (FAILED(result)) result = create_d3d(0,D3D_DRIVER_TYPE_WARP,0,D3D11_CREATE_DEVICE_BGRA_SUPPORT,0,0,D3D11_SDK_VERSION,&glow_d3d,0,0);
    IDXGIDevice *dxgi = 0;
    if (SUCCEEDED(result)) result = glow_d3d->QueryInterface(__uuidof(IDXGIDevice),reinterpret_cast<void **>(&dxgi));
    if (SUCCEEDED(result)) result = create_d2d(dxgi,0,&glow_device);
    if (SUCCEEDED(result)) result = glow_device->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE,&glow_context);
    glow_release(dxgi);
    if (FAILED(result)) { glow_release(glow_device); glow_release(glow_d3d); return 0; }
    glow_context->SetDpi(96,96); // Caller supplies physical pixels, including DPI scaling.
    return 1;
}

struct glow_cache_entry
{
    unsigned width,height,radius,strength,color;
    BYTE *alpha;
    DWORD *pixels;
};
static glow_cache_entry glow_cache[24];
static unsigned glow_next_cache;

extern "C" int __cdecl glow_render(const DWORD *source,DWORD *output,unsigned width,unsigned height,unsigned radius,unsigned strength,COLORREF color)
{
    if (!source || !output || !width || !height || width > 16384 || height > 4096) return 0;
    const SIZE_T count = static_cast<SIZE_T>(width)*height;
    // Off means no effects, caching, or graphics-device initialization.
    if (!radius || !strength) { ZeroMemory(output,count*4); return 1; }
    if (!glow_available()) return 0;
    for (unsigned i=0;i<24;i++)
    {
        glow_cache_entry &entry = glow_cache[i];
        if (!entry.alpha || entry.width!=width || entry.height!=height || entry.radius!=radius || entry.strength!=strength || entry.color!=color) continue;
        SIZE_T p;
        for (p=0;p<count;p++) if (entry.alpha[p] != (source[p]>>24)) break;
        if (p==count) { for (p=0;p<count;p++) output[p]=entry.pixels[p]; return 1; }
    }

    ID2D1Bitmap1 *input=0,*target=0,*readback=0;
    ID2D1Effect *spread=0,*shadow=0;
    const D2D1_SIZE_U size = {width,height};
    D2D1_BITMAP_PROPERTIES1 properties = {};
    properties.pixelFormat.format = DXGI_FORMAT_B8G8R8A8_UNORM;
    properties.pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;
    properties.dpiX = properties.dpiY = 96;
    HRESULT result = glow_context->CreateBitmap(size,source,width*4,&properties,&input);
    properties.bitmapOptions = D2D1_BITMAP_OPTIONS_TARGET;
    if (SUCCEEDED(result)) result = glow_context->CreateBitmap(size,0,0,&properties,&target);
    properties.bitmapOptions = D2D1_BITMAP_OPTIONS_CPU_READ | D2D1_BITMAP_OPTIONS_CANNOT_DRAW;
    if (SUCCEEDED(result)) result = glow_context->CreateBitmap(size,0,0,&properties,&readback);
    if (SUCCEEDED(result)) result = glow_context->CreateEffect(CLSID_D2D1Morphology,&spread);
    if (SUCCEEDED(result)) result = glow_context->CreateEffect(CLSID_D2D1Shadow,&shadow);
    if (SUCCEEDED(result))
    {
        // Keep the smallest positive halo visible beyond the sharp silhouette.
        spread->SetInput(0,input);
        result = spread->SetValue(D2D1_MORPHOLOGY_PROP_MODE,D2D1_MORPHOLOGY_MODE_DILATE);
        if (SUCCEEDED(result)) result = spread->SetValue(D2D1_MORPHOLOGY_PROP_WIDTH,static_cast<UINT32>(3));
        if (SUCCEEDED(result)) result = spread->SetValue(D2D1_MORPHOLOGY_PROP_HEIGHT,static_cast<UINT32>(3));
        shadow->SetInputEffect(0,spread);
        D2D1_VECTOR_4F tint = {GetRValue(color)/255.f,GetGValue(color)/255.f,GetBValue(color)/255.f,strength/100.f};
        if (SUCCEEDED(result)) result = shadow->SetValue(D2D1_SHADOW_PROP_COLOR,tint);
        if (SUCCEEDED(result)) result = shadow->SetValue(D2D1_SHADOW_PROP_BLUR_STANDARD_DEVIATION,radius/3.f);
        if (SUCCEEDED(result)) result = shadow->SetValue(D2D1_SHADOW_PROP_OPTIMIZATION,D2D1_SHADOW_OPTIMIZATION_BALANCED);
        if (SUCCEEDED(result))
        {
            const D2D1_COLOR_F clear = {0,0,0,0};
            const D2D1_POINT_2F origin = {0,0};
            glow_context->SetTarget(target);
            glow_context->BeginDraw();
            glow_context->Clear(&clear);
            glow_context->DrawImage(shadow,origin);
            result = glow_context->EndDraw();
            glow_context->SetTarget(0);
        }
        if (SUCCEEDED(result)) result = readback->CopyFromBitmap(0,target,0);
        D2D1_MAPPED_RECT mapped = {};
        if (SUCCEEDED(result)) result = readback->Map(D2D1_MAP_OPTIONS_READ,&mapped);
        if (SUCCEEDED(result))
        {
            for (unsigned y=0;y<height;y++)
            {
                const DWORD *row = reinterpret_cast<const DWORD *>(mapped.bits+y*mapped.pitch);
                for (unsigned x=0;x<width;x++) output[y*width+x]=row[x];
            }
            readback->Unmap();
            glow_cache_entry &entry = glow_cache[glow_next_cache++ % 24];
            if (entry.alpha) HeapFree(GetProcessHeap(),0,entry.alpha);
            if (entry.pixels) HeapFree(GetProcessHeap(),0,entry.pixels);
            entry.alpha = static_cast<BYTE *>(HeapAlloc(GetProcessHeap(),0,count));
            entry.pixels = static_cast<DWORD *>(HeapAlloc(GetProcessHeap(),0,count*4));
            if (entry.alpha && entry.pixels)
            {
                entry.width=width; entry.height=height; entry.radius=radius; entry.strength=strength; entry.color=color;
                for (SIZE_T p=0;p<count;p++) { entry.alpha[p]=static_cast<BYTE>(source[p]>>24); entry.pixels[p]=output[p]; }
            }
            else
            {
                if (entry.alpha) HeapFree(GetProcessHeap(),0,entry.alpha);
                if (entry.pixels) HeapFree(GetProcessHeap(),0,entry.pixels);
                entry.alpha=0; entry.pixels=0;
            }
        }
    }
    glow_release(shadow); glow_release(spread);
    glow_release(readback); glow_release(target); glow_release(input);
    if (result==D2DERR_RECREATE_TARGET)
    {
        glow_release(glow_context); glow_release(glow_device); glow_release(glow_d3d);
        glow_attempted=false;
    }
    return SUCCEEDED(result);
}
