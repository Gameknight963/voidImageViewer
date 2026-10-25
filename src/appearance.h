// Appearance support, compiled with viv.c so all existing solution files work.
// Copyright (c) 2026. MIT licensed, like voidImageViewer.
#include <commctrl.h>

#define APPEARANCE_WM_APPLY (WM_APP + 93)
#define APPEARANCE_WM_COMPOSITIONCHANGED 0x031e

typedef struct appearance_margins_s { int left, right, top, bottom; } appearance_margins_t;
static HRESULT (WINAPI *appearance_extend)(HWND,const appearance_margins_t *);
static HRESULT (WINAPI *appearance_composition)(BOOL *);
static HRESULT (WINAPI *appearance_attribute)(HWND,DWORD,LPCVOID,DWORD);
static HRESULT (WINAPI *appearance_set_theme)(HWND,LPCWSTR,LPCWSTR);
static HTHEME (WINAPI *appearance_open_theme)(HWND,LPCWSTR);
static HRESULT (WINAPI *appearance_close_theme)(HTHEME);
static HRESULT (WINAPI *appearance_theme_text)(HTHEME,HDC,int,int,LPCWSTR,int,DWORD,LPRECT,const DTTOPTS *);
static HRESULT (WINAPI *appearance_theme_background)(HTHEME,HDC,int,int,const RECT *,const RECT *);
static BOOLEAN (WINAPI *appearance_allow_window)(HWND,BOOLEAN);
static int (WINAPI *appearance_app_mode)(int);
static BOOLEAN (WINAPI *appearance_allow_app)(BOOLEAN);
static void (WINAPI *appearance_flush_menus)(void);
static BYTE appearance_dark, appearance_glass, appearance_menu_glass, appearance_high_contrast;
static DWORD appearance_build;
static HBRUSH appearance_background;
static int appearance_top_glass_height;

extern int __cdecl glow_available(void);
extern int __cdecl glow_render(const DWORD *,DWORD *,unsigned,unsigned,unsigned,unsigned,COLORREF);

typedef struct appearance_style_s
{
    int text,icon,size,strength,custom;
    COLORREF color;
} appearance_style_t;

static appearance_style_t appearance_style(void)
{
    appearance_style_t style;
    style.text=config_text_style; style.icon=config_icon_style;
    style.size=config_glow_size; style.strength=config_glow_strength;
    style.custom=config_custom_glow; style.color=config_glow_color;
    return style;
}

static COLORREF appearance_bg(void) { return appearance_dark ? RGB(32,32,32) : GetSysColor(COLOR_BTNFACE); }
static COLORREF appearance_fg(void) { return appearance_dark ? RGB(240,240,240) : GetSysColor(COLOR_BTNTEXT); }

static void appearance_init(void)
{
    HMODULE dwm, theme;
    void (WINAPI *version)(DWORD *,DWORD *,DWORD *);
    DWORD major = 0, minor = 0;
    dwm = LoadLibraryW(L"dwmapi.dll");
    theme = LoadLibraryW(L"uxtheme.dll");
    version = (void *)GetProcAddress(GetModuleHandleW(L"ntdll.dll"),"RtlGetNtVersionNumbers");
    if (version) version(&major,&minor,&appearance_build);
    appearance_build &= 0x0fffffff;
    if (dwm)
    {
        appearance_extend = (void *)GetProcAddress(dwm,"DwmExtendFrameIntoClientArea");
        appearance_composition = (void *)GetProcAddress(dwm,"DwmIsCompositionEnabled");
        appearance_attribute = (void *)GetProcAddress(dwm,"DwmSetWindowAttribute");
    }
    if (theme)
    {
        appearance_set_theme = (void *)GetProcAddress(theme,"SetWindowTheme");
        appearance_open_theme = (void *)GetProcAddress(theme,"OpenThemeData");
        appearance_close_theme = (void *)GetProcAddress(theme,"CloseThemeData");
        appearance_theme_text = (void *)GetProcAddress(theme,"DrawThemeTextEx");
        appearance_theme_background = (void *)GetProcAddress(theme,"DrawThemeBackground");
        // Ordinal 135 changed ABI in Windows 10 1903. Never use these on Win7.
        if (major == 10 && appearance_build >= 17763)
        {
            appearance_allow_window = (void *)GetProcAddress(theme,MAKEINTRESOURCEA(133));
            if (appearance_build >= 18362)
                appearance_app_mode = (void *)GetProcAddress(theme,MAKEINTRESOURCEA(135));
            else
                appearance_allow_app = (void *)GetProcAddress(theme,MAKEINTRESOURCEA(135));
            appearance_flush_menus = (void *)GetProcAddress(theme,MAKEINTRESOURCEA(136));
        }
    }
}

static void appearance_refresh(void)
{
    HKEY key;
    DWORD light = 1, size = sizeof(light), type;
    HIGHCONTRASTW hc;
    os_zero_memory(&hc,sizeof(hc));
    hc.cbSize = sizeof(hc);
    appearance_high_contrast = SystemParametersInfoW(SPI_GETHIGHCONTRAST,sizeof(hc),&hc,0) && (hc.dwFlags & HCF_HIGHCONTRASTON);
    if (RegOpenKeyExW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",0,KEY_QUERY_VALUE,&key) == ERROR_SUCCESS)
    {
        if (RegQueryValueExW(key,L"AppsUseLightTheme",NULL,&type,(BYTE *)&light,&size) != ERROR_SUCCESS || type != REG_DWORD)
            light = 1;
        RegCloseKey(key);
    }
    appearance_dark = !appearance_high_contrast && (config_color_mode == 2 || (config_color_mode == 0 && !light));
    if (appearance_background) DeleteObject(appearance_background);
    appearance_background = CreateSolidBrush(appearance_bg());
    if (appearance_app_mode) appearance_app_mode(appearance_high_contrast ? 0 : (appearance_dark ? 2 : 3));
    if (appearance_allow_app) appearance_allow_app(appearance_dark);
    if (appearance_flush_menus) appearance_flush_menus();
}

static void appearance_window(HWND hwnd)
{
    BOOL dark = appearance_dark;
    if (appearance_allow_window) appearance_allow_window(hwnd,(BOOLEAN)dark);
    if (appearance_attribute && appearance_build >= 17763)
    {
        if (FAILED(appearance_attribute(hwnd,20,&dark,sizeof(dark))))
            appearance_attribute(hwnd,19,&dark,sizeof(dark));
    }
}

static void appearance_update_glass(HWND hwnd,int height,int fullscreen)
{
    BOOL composition = FALSE;
    BYTE previous = appearance_glass;
    BYTE previous_menu = appearance_menu_glass;
    HRESULT result;
    appearance_margins_t margins = {0,0,0,0};
    if (appearance_composition) appearance_composition(&composition);
    margins.bottom = config_bottom_glass && !appearance_high_contrast && !fullscreen && composition ? height : 0;
    // Align the glass edge with the image area to retain the complete inner frame.
    margins.top = config_menu_glass && !appearance_high_contrast && !fullscreen && composition && GetMenu(hwnd) ? 1 : 0;
    if (margins.top)
    {
        MENUBARINFO menu;
        os_zero_memory(&menu,sizeof(menu));
        menu.cbSize = sizeof(menu);
        if (GetMenuBarInfo(hwnd,OBJID_MENU,0,&menu)) margins.top += menu.rcBar.bottom-menu.rcBar.top;
    }
    result = appearance_extend ? appearance_extend(hwnd,&margins) : E_NOTIMPL;
    appearance_glass = SUCCEEDED(result) && margins.bottom > 0;
    appearance_menu_glass = SUCCEEDED(result) && margins.top > 0;
    appearance_top_glass_height = appearance_menu_glass ? margins.top : 0;
    if (previous != appearance_glass || previous_menu != appearance_menu_glass)
        RedrawWindow(hwnd,NULL,NULL,RDW_INVALIDATE|RDW_ALLCHILDREN|RDW_FRAME);
}

static void appearance_status_style(HWND hwnd)
{
    LONG_PTR style;
    if (!hwnd) return;
    style = GetWindowLongPtr(hwnd,GWL_EXSTYLE);
    // WS_EX_COMPOSITED redirects the status control through an opaque buffer.
    if (!!(style & WS_EX_COMPOSITED) == !!appearance_glass)
    {
        SetWindowLongPtr(hwnd,GWL_EXSTYLE,appearance_glass ? style & ~WS_EX_COMPOSITED : style | WS_EX_COMPOSITED);
        SetWindowPos(hwnd,NULL,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE|SWP_FRAMECHANGED);
        InvalidateRect(hwnd,NULL,TRUE);
    }
}

// Paint in a top-down 32-bit DIB: DWM needs alpha, which ordinary GDI text loses.
typedef struct appearance_buffer_s
{
    HDC dc;
    HBITMAP bitmap;
    HGDIOBJ old;
    DWORD *pixels;
    int width, height;
} appearance_buffer_t;

static int appearance_begin(HDC target,RECT *rect,appearance_buffer_t *buffer,int glass)
{
    BITMAPINFO info;
    RECT fill;
    os_zero_memory(buffer,sizeof(*buffer));
    buffer->width = rect->right - rect->left;
    buffer->height = rect->bottom - rect->top;
    if (buffer->width <= 0 || buffer->height <= 0) return 0;
    os_zero_memory(&info,sizeof(info));
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = buffer->width;
    info.bmiHeader.biHeight = -buffer->height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    buffer->dc = CreateCompatibleDC(target);
    if (!buffer->dc) return 0;
    buffer->bitmap = CreateDIBSection(target,&info,DIB_RGB_COLORS,(void **)&buffer->pixels,NULL,0);
    if (!buffer->bitmap) { DeleteDC(buffer->dc); return 0; }
    buffer->old = SelectObject(buffer->dc,buffer->bitmap);
    os_zero_memory(buffer->pixels,buffer->width * buffer->height * 4);
    if (!glass)
    {
        SetRect(&fill,0,0,buffer->width,buffer->height);
        FillRect(buffer->dc,&fill,appearance_background);
    }
    SetBkMode(buffer->dc,TRANSPARENT);
    return 1;
}

static void appearance_end(HDC target,RECT *rect,appearance_buffer_t *buffer,int glass)
{
    if (!glass)
    {
        int i;
        GdiFlush();
        for (i=0;i<buffer->width * buffer->height;i++) buffer->pixels[i] |= 0xff000000;
    }
    BitBlt(target,rect->left,rect->top,buffer->width,buffer->height,buffer->dc,0,0,SRCCOPY);
    SelectObject(buffer->dc,buffer->old);
    DeleteObject(buffer->bitmap);
    DeleteDC(buffer->dc);
}

static void appearance_opaque_image_top(HWND hwnd,HDC dc)
{
    RECT rect;
    appearance_buffer_t buffer;
    if (!appearance_top_glass_height) return;
    GetClientRect(hwnd,&rect);
    if (rect.bottom > appearance_top_glass_height) rect.bottom = appearance_top_glass_height;
    if (appearance_begin(dc,&rect,&buffer,0))
    {
        BitBlt(buffer.dc,0,0,buffer.width,buffer.height,dc,0,0,SRCCOPY);
        appearance_end(dc,&rect,&buffer,0);
    }
}

static void appearance_dispose(appearance_buffer_t *layer)
{
    SelectObject(layer->dc,layer->old);
    DeleteObject(layer->bitmap);
    DeleteDC(layer->dc);
}

static void appearance_blend_layer(HDC dc,const appearance_buffer_t *layer,int dx,int dy)
{
    BITMAP bitmap;
    int x,y,stride,height;
    if (!GetObject(GetCurrentObject(dc,OBJ_BITMAP),sizeof(bitmap),&bitmap) || !bitmap.bmBits || bitmap.bmBitsPixel != 32) return;
    stride=bitmap.bmWidthBytes/4;
    height=bitmap.bmHeight < 0 ? -bitmap.bmHeight : bitmap.bmHeight;
    GdiFlush();
    for (y=0;y<layer->height;y++) for (x=0;x<layer->width;x++)
    {
        DWORD source,destination,alpha,inverse,r,g,b,a;
        DWORD *pixel;
        if (x+dx<0 || y+dy<0 || x+dx>=bitmap.bmWidth || y+dy>=height) continue;
        source=layer->pixels[y*layer->width+x];
        alpha=source>>24;
        if (!alpha) continue;
        pixel=(DWORD *)bitmap.bmBits+(y+dy)*stride+x+dx;
        destination=*pixel;
        inverse=255-alpha;
        a=alpha+(destination>>24)*inverse/255;
        r=((source>>16)&255)+((destination>>16)&255)*inverse/255;
        g=((source>>8)&255)+((destination>>8)&255)*inverse/255;
        b=(source&255)+(destination&255)*inverse/255;
        *pixel=(a<<24)|(r<<16)|(g<<8)|b;
    }
}

static void appearance_render_layer(HDC dc,appearance_buffer_t *layer,int x,int y,int effect,const appearance_style_t *style)
{
    if (effect && (effect!=1 || (style->size>0 && style->strength>0)))
    {
        appearance_buffer_t halo=*layer;
        int count=layer->width*layer->height;
        halo.pixels=mem_alloc(count*4);
        os_zero_memory(halo.pixels,count*4);
        if (effect==1)
            glow_render(layer->pixels,halo.pixels,layer->width,layer->height,MulDiv(style->size,os_logical_high,96),style->strength,style->color);
        else
        {
            int px,py,ox,oy;
            int radius=MulDiv(1,os_logical_high,96);
            for (py=0;py<layer->height;py++) for (px=0;px<layer->width;px++)
            {
                DWORD alpha=0;
                for (oy=-radius;oy<=radius;oy++) for (ox=-radius;ox<=radius;ox++)
                {
                    if (px+ox>=0 && py+oy>=0 && px+ox<layer->width && py+oy<layer->height)
                    {
                        DWORD coverage=layer->pixels[(py+oy)*layer->width+px+ox]>>24;
                        if (coverage>alpha) alpha=coverage;
                    }
                }
                halo.pixels[py*layer->width+px]=alpha<<24;
            }
        }
        appearance_blend_layer(dc,&halo,x,y);
        mem_free(halo.pixels);
    }
    appearance_blend_layer(dc,layer,x,y);
}

static void appearance_styled_text(HWND hwnd,HDC dc,LPCWSTR text,RECT *rect,UINT flags,const appearance_style_t *style)
{
    COLORREF color=style->text==0 || style->text==3 ? RGB(0,0,0) : RGB(255,255,255);
    int pad=MulDiv(style->size+2,os_logical_high,96);
    appearance_buffer_t layer;
    RECT bounds,local;
    int i;
    if (style->text==0 && style->size>0 && !style->custom && appearance_open_theme && appearance_theme_text)
    {
        HTHEME theme=appearance_open_theme(NULL,L"CompositedWindow::Window");
        if (!theme) theme=appearance_open_theme(hwnd,L"WINDOW");
        if (theme)
        {
            DTTOPTS options;
            HRESULT result;
            os_zero_memory(&options,sizeof(options));
            options.dwSize=sizeof(options);
            options.dwFlags=DTT_COMPOSITED|DTT_TEXTCOLOR|DTT_GLOWSIZE|DTT_SHADOWTYPE;
            options.crText=color;
            options.iGlowSize=MulDiv(style->size,os_logical_high,96);
            result=appearance_theme_text(theme,dc,0,0,text,-1,flags,rect,&options);
            appearance_close_theme(theme);
            if (SUCCEEDED(result)) return;
        }
    }
    bounds=*rect;
    InflateRect(&bounds,pad,pad);
    if (!appearance_begin(dc,&bounds,&layer,1)) return;
    SelectObject(layer.dc,GetCurrentObject(dc,OBJ_FONT));
    local=*rect;
    OffsetRect(&local,-bounds.left,-bounds.top);
    SetTextColor(layer.dc,RGB(255,255,255));
    DrawTextW(layer.dc,text,-1,&local,flags);
    GdiFlush();
    // GDI's RGB glyph coverage becomes a grayscale alpha mask. This path does
    // not require a visual style or a theme-provided text-glow implementation.
    for (i=0;i<layer.width*layer.height;i++)
    {
        DWORD pixel=layer.pixels[i];
        DWORD alpha=(((pixel>>16)&255)*77+((pixel>>8)&255)*150+(pixel&255)*29)/256;
        layer.pixels[i]=(alpha<<24)|((GetRValue(color)*alpha/255)<<16)|((GetGValue(color)*alpha/255)<<8)|(GetBValue(color)*alpha/255);
    }
    appearance_render_layer(dc,&layer,bounds.left,bounds.top,
        style->text==0 && style->custom ? 1 : style->text==2 ? 2 : 0,style);
    appearance_dispose(&layer);
}

static void appearance_text(HWND hwnd,HDC dc,LPCWSTR text,RECT *rect,UINT flags,int glass)
{
    appearance_style_t style=appearance_style();
    if (appearance_high_contrast)
    {
        SetTextColor(dc,GetSysColor(COLOR_WINDOWTEXT));
        DrawTextW(dc,text,-1,rect,flags);
        return;
    }
    appearance_styled_text(hwnd,dc,text,rect,flags,&style);
}

static void appearance_icon(HDC dc,HICON icon,int x,int y,int width,int height,int enabled,const appearance_style_t *style)
{
    int pad=MulDiv(style->size+2,os_logical_high,96);
    RECT bounds={0,0,width+pad*2,height+pad*2};
    appearance_buffer_t layer;
    ICONINFO info;
    int px,py,has_alpha=0;
    if (!icon || !appearance_begin(dc,&bounds,&layer,1)) return;
    DrawIconEx(layer.dc,pad,pad,icon,width,height,0,NULL,DI_NORMAL);
    GdiFlush();
    for (py=0;py<height;py++) for (px=0;px<width;px++)
        if (layer.pixels[(py+pad)*layer.width+px+pad]>>24) has_alpha=1;
    if (GetIconInfo(icon,&info))
    {
        HDC mask=CreateCompatibleDC(dc);
        HGDIOBJ previous=SelectObject(mask,info.hbmMask);
        BITMAP mask_info;
        GetObject(info.hbmMask,sizeof(mask_info),&mask_info);
        for (py=0;py<height;py++) for (px=0;px<width;px++)
        {
            DWORD *pixel=layer.pixels+(py+pad)*layer.width+px+pad;
            DWORD alpha=*pixel>>24,r=(*pixel>>16)&255,g=(*pixel>>8)&255,b=*pixel&255;
            if (!has_alpha) alpha=GetPixel(mask,MulDiv(px,mask_info.bmWidth,width),MulDiv(py,mask_info.bmWidth,width))==RGB(0,0,0) ? 255 : 0;
            if (style->icon)
            {
                r=g=b=(style->icon==2 || style->icon==3) ? alpha : 0;
            }
            if (!enabled)
            {
                DWORD gray=(r*77+g*150+b*29)/256;
                alpha=alpha*100/255;
                r=g=b=gray*100/255;
            }
            *pixel=alpha ? (alpha<<24)|(r<<16)|(g<<8)|b : 0;
        }
        SelectObject(mask,previous);
        DeleteDC(mask);
        DeleteObject(info.hbmMask);
        if (info.hbmColor) DeleteObject(info.hbmColor);
    }
    appearance_render_layer(dc,&layer,x-pad,y-pad,style->icon==1 ? 1 : style->icon==3 ? 2 : 0,style);
    appearance_dispose(&layer);
}

// Keep native hit targets fixed. Only the shared paint surface needs room
// outside the first and last icons; this gutter never depends on glow size.
static int appearance_toolbar_gutter(void)
{
    return MulDiv(14,os_logical_wide,96);
}

static int appearance_toolbar_canvas(int dpi)
{
    return MulDiv(16,dpi,96);
}

static void appearance_toolbar_add_icon(HIMAGELIST images,int resource)
{
    HICON icon=(HICON)LoadImage(os_hinstance,MAKEINTRESOURCE(resource),IMAGE_ICON,
        MulDiv(16,os_logical_wide,96),MulDiv(16,os_logical_high,96),0);
    if (icon) { ImageList_AddIcon(images,icon); DestroyIcon(icon); }
}

static int appearance_toolbar_height(void)
{
    return MulDiv(32,os_logical_high,96);
}

static void appearance_toolbar_layout(HWND hwnd)
{
    int image_width=0,image_height=0;
    int width;
    ImageList_GetIconSize((HIMAGELIST)SendMessage(hwnd,TB_GETIMAGELIST,0,0),&image_width,&image_height);
    width=image_width+MulDiv(8,os_logical_wide,96);
    int height=appearance_toolbar_height();
    DWORD size=MAKELONG(width,height);
    RECT first={0,0,0,0};
    SendMessage(hwnd,TB_GETITEMRECT,0,(LPARAM)&first);
    if ((DWORD)(UINT_PTR)GetPropW(hwnd,L"VIVAppearanceButtonSize")!=size || first.right-first.left!=width || first.bottom-first.top<height)
    {
        TBMETRICS metrics;
        int i,count;
        os_zero_memory(&metrics,sizeof(metrics));
        metrics.cbSize=sizeof(metrics);
        metrics.dwMask=TBMF_PAD;
        metrics.cxPad=width-image_width;
        metrics.cyPad=height-image_height;
        SendMessage(hwnd,TB_SETINDENT,appearance_toolbar_gutter(),0);
        SendMessage(hwnd,TB_SETMETRICS,0,(LPARAM)&metrics);
        SendMessage(hwnd,TB_SETBUTTONWIDTH,0,MAKELPARAM(width,width));
        SendMessage(hwnd,TB_SETBUTTONSIZE,0,MAKELPARAM(width,height));
        count=(int)SendMessage(hwnd,TB_BUTTONCOUNT,0,0);
        for (i=0;i<count;i++)
        {
            TBBUTTON button;
            if (SendMessage(hwnd,TB_GETBUTTON,i,(LPARAM)&button) && !(button.fsStyle&TBSTYLE_SEP))
            {
                TBBUTTONINFO info;
                os_zero_memory(&info,sizeof(info));
                info.cbSize=sizeof(info);
                info.dwMask=TBIF_SIZE;
                info.cx=width;
                SendMessage(hwnd,TB_SETBUTTONINFO,button.idCommand,(LPARAM)&info);
            }
        }
        SendMessage(hwnd,TB_AUTOSIZE,0,0);
        SetPropW(hwnd,L"VIVAppearanceButtonSize",(HANDLE)(UINT_PTR)size);
    }
}

static LRESULT CALLBACK appearance_control_proc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp,UINT_PTR id,DWORD_PTR data)
{
    if (msg == WM_NCDESTROY) RemoveWindowSubclass(hwnd,appearance_control_proc,id);
    if (appearance_dark && (msg == WM_PAINT || msg == WM_PRINTCLIENT))
    {
        WCHAR name[32], text[512];
        GetClassNameW(hwnd,name,32);
        if (lstrcmpiW(name,L"Button") == 0)
        {
            UINT type = (UINT)GetWindowLongPtr(hwnd,GWL_STYLE) & BS_TYPEMASK;
            if (type == BS_CHECKBOX || type == BS_AUTOCHECKBOX || type == BS_RADIOBUTTON ||
                type == BS_AUTORADIOBUTTON || type == BS_3STATE || type == BS_AUTO3STATE || type == BS_GROUPBOX)
            {
                PAINTSTRUCT ps;
                HDC dc = msg == WM_PAINT ? BeginPaint(hwnd,&ps) : (HDC)wp;
                RECT rect, label, glyph;
                HFONT old_font;
                UINT flags = DT_LEFT|DT_VCENTER|DT_SINGLELINE;
                UINT state = (UINT)SendMessage(hwnd,BM_GETSTATE,0,0);
                int size = MulDiv(13,os_logical_high,96);
                GetClientRect(hwnd,&rect);
                FillRect(dc,&rect,appearance_background);
                old_font = SelectObject(dc,(HFONT)SendMessage(hwnd,WM_GETFONT,0,0));
                SetBkMode(dc,TRANSPARENT);
                SetTextColor(dc,IsWindowEnabled(hwnd) ? appearance_fg() : RGB(140,140,140));
                GetWindowTextW(hwnd,text,512);
                label = rect;
                if (SendMessage(hwnd,WM_QUERYUISTATE,0,0) & UISF_HIDEACCEL) flags |= DT_HIDEPREFIX;
                if (type == BS_GROUPBOX)
                {
                    RECT measure = rect;
                    HBRUSH line = CreateSolidBrush(RGB(100,100,100));
                    DrawTextW(dc,text,-1,&measure,DT_CALCRECT|DT_SINGLELINE);
                    rect.top += (measure.bottom - measure.top)/2;
                    FrameRect(dc,&rect,line);
                    DeleteObject(line);
                    label.left += MulDiv(8,os_logical_wide,96);
                    label.right = label.left + measure.right - measure.left + MulDiv(6,os_logical_wide,96);
                    label.bottom = label.top + measure.bottom - measure.top;
                    FillRect(dc,&label,appearance_background);
                    flags = DT_CENTER|DT_SINGLELINE|(flags & DT_HIDEPREFIX);
                }
                else
                {
                    HTHEME theme = appearance_open_theme ? appearance_open_theme(hwnd,L"Button") : NULL;
                    int radio = type == BS_RADIOBUTTON || type == BS_AUTORADIOBUTTON;
                    int check = (int)SendMessage(hwnd,BM_GETCHECK,0,0);
                    int theme_state = !IsWindowEnabled(hwnd) ? 4 : (state & BST_PUSHED) ? 3 : (state & BST_HOT) ? 2 : 1;
                    theme_state += check == BST_CHECKED ? 4 : check == BST_INDETERMINATE ? 8 : 0;
                    SetRect(&glyph,0,(rect.bottom-size)/2,size,(rect.bottom+size)/2);
                    if (theme && appearance_theme_background)
                        appearance_theme_background(theme,dc,radio ? 2 : 3,theme_state,&glyph,NULL);
                    else
                        DrawFrameControl(dc,&glyph,DFC_BUTTON,(radio ? DFCS_BUTTONRADIO : DFCS_BUTTONCHECK) | (check ? DFCS_CHECKED : 0));
                    if (theme) appearance_close_theme(theme);
                    label.left = glyph.right + MulDiv(4,os_logical_wide,96);
                }
                DrawTextW(dc,text,-1,&label,flags);
                if (type != BS_GROUPBOX && GetFocus() == hwnd && !(SendMessage(hwnd,WM_QUERYUISTATE,0,0) & UISF_HIDEFOCUS))
                {
                    RECT focus = label;
                    DrawTextW(dc,text,-1,&focus,DT_CALCRECT|DT_SINGLELINE);
                    OffsetRect(&focus,0,(label.bottom-focus.bottom)/2);
                    DrawFocusRect(dc,&focus);
                }
                SelectObject(dc,old_font);
                if (msg == WM_PAINT) EndPaint(hwnd,&ps);
                return 0;
            }
        }
        if (lstrcmpiW(name,L"SysTabControl32") == 0)
        {
            PAINTSTRUCT ps;
            HDC dc = msg == WM_PAINT ? BeginPaint(hwnd,&ps) : (HDC)wp;
            RECT rect, tab;
            TCITEMW item;
            GetClientRect(hwnd,&rect);
            FillRect(dc,&rect,appearance_background);
            TabCtrl_GetItemRect(hwnd,0,&tab);
            item.mask = TCIF_TEXT; item.pszText = text; item.cchTextMax = 512;
            if (SendMessageW(hwnd,TCM_GETITEMW,0,(LPARAM)&item))
            {
                SelectObject(dc,(HFONT)SendMessage(hwnd,WM_GETFONT,0,0));
                SetBkMode(dc,TRANSPARENT);
                SetTextColor(dc,appearance_fg());
                DrawTextW(dc,text,-1,&tab,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
            }
            if (msg == WM_PAINT) EndPaint(hwnd,&ps);
            return 0;
        }
    }
    return DefSubclassProc(hwnd,msg,wp,lp);
}

static BOOL CALLBACK appearance_child(HWND hwnd,LPARAM unused)
{
    WCHAR name[64];
    GetClassNameW(hwnd,name,64);
    if (appearance_allow_window) appearance_allow_window(hwnd,appearance_dark);
    if (appearance_set_theme)
    {
        appearance_set_theme(hwnd,appearance_dark ? L"DarkMode_Explorer" : L"Explorer",NULL);
        if (lstrcmpiW(name,L"ComboBox") == 0)
            appearance_set_theme(hwnd,appearance_dark ? L"DarkMode_CFD" : L"Explorer",NULL);
    }
    if (lstrcmpiW(name,L"SysTreeView32") == 0)
    {
        TreeView_SetBkColor(hwnd,appearance_dark ? appearance_bg() : GetSysColor(COLOR_WINDOW));
        TreeView_SetTextColor(hwnd,appearance_dark ? appearance_fg() : GetSysColor(COLOR_WINDOWTEXT));
    }
    if (lstrcmpiW(name,L"#32770") == 0) appearance_window(hwnd);
    SetWindowSubclass(hwnd,appearance_control_proc,1,0);
    InvalidateRect(hwnd,NULL,TRUE);
    return TRUE;
}

static void appearance_dialog(HWND hwnd)
{
    appearance_window(hwnd);
    EnumChildWindows(hwnd,appearance_child,0);
    if (appearance_dark && os_EnableThemeDialogTexture) os_EnableThemeDialogTexture(hwnd,ETDT_DISABLE);
}

static BOOL CALLBACK appearance_thread_window(HWND hwnd,LPARAM unused)
{
    WCHAR name[32];
    appearance_window(hwnd);
    EnumChildWindows(hwnd,appearance_child,0);
    GetClassNameW(hwnd,name,32);
    if (lstrcmpiW(name,L"#32770") == 0)
    {
        if (os_EnableThemeDialogTexture) os_EnableThemeDialogTexture(hwnd,appearance_dark ? ETDT_DISABLE : ETDT_ENABLETAB);
        PostMessage(hwnd,APPEARANCE_WM_APPLY,0,0);
    }
    RedrawWindow(hwnd,NULL,NULL,RDW_INVALIDATE|RDW_ALLCHILDREN|RDW_FRAME);
    return TRUE;
}

static INT_PTR appearance_dialog_color(UINT msg,WPARAM wp)
{
    if (appearance_dark && msg >= WM_CTLCOLORMSGBOX && msg <= WM_CTLCOLORSTATIC)
    {
        SetTextColor((HDC)wp,appearance_fg());
        SetBkColor((HDC)wp,appearance_bg());
        return (INT_PTR)appearance_background;
    }
    return 0;
}

static void appearance_status_paint(HWND hwnd,HDC target)
{
    RECT rect;
    appearance_buffer_t buffer;
    int count, i;
    GetClientRect(hwnd,&rect);
    if (!appearance_begin(target,&rect,&buffer,appearance_glass)) return;
    SelectObject(buffer.dc,(HFONT)SendMessage(hwnd,WM_GETFONT,0,0));
    count = (int)SendMessage(hwnd,SB_GETPARTS,0,0);
    for (i=0;i<count;i++)
    {
        RECT part;
        DWORD length = LOWORD(SendMessageW(hwnd,SB_GETTEXTLENGTHW,i,0));
        WCHAR *text = mem_alloc((length + 1) * sizeof(WCHAR));
        if (SendMessage(hwnd,SB_GETRECT,i,(LPARAM)&part))
        {
            SendMessageW(hwnd,SB_GETTEXTW,i,(LPARAM)text);
            InflateRect(&part,-MulDiv(3,os_logical_wide,96),0);
            // Leave room for the glow above and below the text.
            appearance_text(hwnd,buffer.dc,text,&part,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_END_ELLIPSIS|DT_NOPREFIX,appearance_glass);
        }
        mem_free(text);
    }
    if (GetWindowLongPtr(hwnd,GWL_STYLE) & SBARS_SIZEGRIP)
    {
        RECT grip = rect;
        appearance_style_t style=appearance_style();
        style.text=style.icon==0 ? (appearance_dark ? 1 : 3) : style.icon-1;
        style.custom=1; // The grip is a glyph, but follows the icon settings.
        grip.left = grip.right - MulDiv(14,os_logical_wide,96);
        appearance_styled_text(hwnd,buffer.dc,L"\x25e2",&grip,DT_RIGHT|DT_BOTTOM|DT_SINGLELINE,&style);
    }
    appearance_end(target,&rect,&buffer,appearance_glass);
}

static int appearance_rounded_contains(double x,double y,int width,int height,int inset,int radius)
{
    double cx,cy,dx,dy;
    if (x < inset || y < inset || x >= width-inset || y >= height-inset) return 0;
    cx = x < inset+radius ? inset+radius : x > width-inset-radius ? width-inset-radius : x;
    cy = y < inset+radius ? inset+radius : y > height-inset-radius ? height-inset-radius : y;
    dx = x-cx;
    dy = y-cy;
    return dx*dx + dy*dy <= radius*radius;
}

static void appearance_toolbar_highlight(appearance_buffer_t *buffer,UINT state)
{
    int x,y,sx,sy;
    int inset = MulDiv(1,os_logical_high,96);
    int radius = MulDiv(3,os_logical_high,96);
    int fill_alpha = (state & CDIS_SELECTED) ? 90 : (state & CDIS_HOT) ? 45 : 28;
    int border_alpha = (state & CDIS_SELECTED) ? 135 : (state & CDIS_HOT) ? 95 : 65;
    COLORREF tint = appearance_dark ? RGB(255,255,255) : RGB(100,160,220);
    if (!(state & (CDIS_HOT|CDIS_SELECTED|CDIS_CHECKED))) return;
    if ((state & (CDIS_HOT|CDIS_CHECKED)) == (CDIS_HOT|CDIS_CHECKED)) fill_alpha += 15;
    GdiFlush();
    for (y=0;y<buffer->height;y++) for (x=0;x<buffer->width;x++)
    {
        int outer = 0,inner = 0;
        DWORD alpha,old,r,g,b,a;
        for (sy=0;sy<2;sy++) for (sx=0;sx<2;sx++)
        {
            double px = x + 0.25 + sx*0.5, py = y + 0.25 + sy*0.5;
            outer += appearance_rounded_contains(px,py,buffer->width,buffer->height,inset,radius);
            inner += appearance_rounded_contains(px,py,buffer->width,buffer->height,inset+1,radius > 1 ? radius-1 : 0);
        }
        alpha = (inner*fill_alpha + (outer-inner)*border_alpha)/4;
        if (!alpha) continue;
        // Source-over in premultiplied BGRA preserves the glass below the button.
        old = buffer->pixels[y*buffer->width+x];
        a = alpha + (old >> 24)*(255-alpha)/255;
        r = (GetRValue(tint)*alpha + ((old >> 16)&255)*(255-alpha))/255;
        g = (GetGValue(tint)*alpha + ((old >> 8)&255)*(255-alpha))/255;
        b = (GetBValue(tint)*alpha + (old&255)*(255-alpha))/255;
        buffer->pixels[y*buffer->width+x] = (a<<24)|(r<<16)|(g<<8)|b;
    }
}

static LRESULT appearance_toolbar_draw(HWND hwnd,NMTBCUSTOMDRAW *draw,HIMAGELIST images)
{
    RECT bounds;
    appearance_buffer_t buffer,icons;
    appearance_style_t style=appearance_style(),sharp=style;
    int index,count,hot,iw=0,ih=0;
    if (draw->nmcd.dwDrawStage != CDDS_PREPAINT) return CDRF_DODEFAULT;
    GetClientRect(hwnd,&bounds);
    if (!appearance_begin(draw->nmcd.hdc,&bounds,&buffer,appearance_glass)) return CDRF_DODEFAULT;
    if (!appearance_begin(buffer.dc,&bounds,&icons,1))
    {
        appearance_dispose(&buffer);
        return CDRF_DODEFAULT;
    }
    count=(int)SendMessage(hwnd,TB_BUTTONCOUNT,0,0);
    hot=(int)SendMessage(hwnd,TB_GETHOTITEM,0,0);
    ImageList_GetIconSize(images,&iw,&ih);
    // Gather silhouettes before blurring so neighboring halos blend together.
    // Sharp icons are composited last, keeping them clear over the shared glow.
    if (sharp.icon==1) sharp.icon=4;
    for (index=0;index<count;index++)
    {
        TBBUTTON button;
        RECT rect,highlight_bounds;
        appearance_buffer_t highlight;
        HICON icon;
        UINT state=0;
        int enabled,x,y;
        if (!SendMessage(hwnd,TB_GETBUTTON,index,(LPARAM)&button) ||
            (button.fsStyle&TBSTYLE_SEP) || (button.fsState&TBSTATE_HIDDEN) ||
            !SendMessage(hwnd,TB_GETITEMRECT,index,(LPARAM)&rect)) continue;
        enabled=(button.fsState&TBSTATE_ENABLED)!=0;
        if (index==hot) state|=CDIS_HOT;
        if (button.fsState&TBSTATE_PRESSED) state|=CDIS_SELECTED;
        if (button.fsState&TBSTATE_CHECKED) state|=CDIS_CHECKED;
        if (GetFocus()==hwnd && index==hot) state|=CDIS_FOCUS;
        highlight_bounds=rect;
        InflateRect(&highlight_bounds,0,-MulDiv(6,os_logical_high,96));
        if (enabled && appearance_begin(buffer.dc,&highlight_bounds,&highlight,1))
        {
            appearance_toolbar_highlight(&highlight,state);
            appearance_blend_layer(buffer.dc,&highlight,highlight_bounds.left,highlight_bounds.top);
            appearance_dispose(&highlight);
        }
        x=(rect.left+rect.right-iw)/2;
        y=(rect.top+rect.bottom-ih)/2;
        icon=ImageList_GetIcon(images,button.iBitmap,ILD_NORMAL);
        if (icon)
        {
            appearance_icon(icons.dc,icon,x,y,iw,ih,enabled,&sharp);
            DestroyIcon(icon);
        }
        // Native toolbar strings remain available to accessibility and tooltips.
        if (enabled && (state&CDIS_FOCUS)) DrawFocusRect(buffer.dc,&highlight_bounds);
    }
    appearance_render_layer(buffer.dc,&icons,0,0,style.icon==1 ? 1 : 0,&style);
    appearance_dispose(&icons);
    appearance_end(draw->nmcd.hdc,&bounds,&buffer,appearance_glass);
    return CDRF_SKIPDEFAULT;
}

// The menu bar still uses light colors after dark-mode opt-in. These private
// UAH draw messages let us color it while retaining native menu interaction.
typedef struct appearance_uah_menu_s { HMENU menu; HDC dc; DWORD flags; } appearance_uah_menu_t;
typedef struct appearance_uah_item_s
{
    int position;
    DWORD metrics[10];
    DWORD popup_metrics[4];
} appearance_uah_item_t;
typedef struct appearance_uah_draw_s
{
    DRAWITEMSTRUCT draw;
    appearance_uah_menu_t menu;
    appearance_uah_item_t item;
} appearance_uah_draw_t;

static int appearance_menu(HWND hwnd,UINT msg,LPARAM lp)
{
    if (appearance_high_contrast) return 0;
    if (msg == 0x0091) // WM_UAHDRAWMENU
    {
        appearance_uah_menu_t *menu = (void *)lp;
        MENUBARINFO info;
        RECT window;
        os_zero_memory(&info,sizeof(info));
        info.cbSize = sizeof(info);
        if (GetMenuBarInfo(hwnd,OBJID_MENU,0,&info))
        {
            appearance_buffer_t buffer;
            GetWindowRect(hwnd,&window);
            OffsetRect(&info.rcBar,-window.left,-window.top);
            if (appearance_begin(menu->dc,&info.rcBar,&buffer,appearance_menu_glass))
                appearance_end(menu->dc,&info.rcBar,&buffer,appearance_menu_glass);
        }
        return 1;
    }
    if (msg == 0x0092) // WM_UAHDRAWMENUITEM
    {
        appearance_uah_draw_t *item = (void *)lp;
        MENUITEMINFOW info;
        WCHAR text[256];
        RECT bounds = item->draw.rcItem, rect;
        appearance_buffer_t buffer;
        NONCLIENTMETRICSW metrics;
        HFONT font = NULL;
        UINT state = 0;
        UINT flags = DT_CENTER|DT_VCENTER|DT_SINGLELINE;
        os_zero_memory(&info,sizeof(info));
        info.cbSize = sizeof(info);
        info.fMask = MIIM_STRING;
        info.dwTypeData = text;
        info.cch = 256;
        if (!GetMenuItemInfoW(item->menu.menu,item->item.position,TRUE,&info)) return 0;
        if (!appearance_begin(item->draw.hDC,&bounds,&buffer,appearance_menu_glass)) return 0;
        SetRect(&rect,0,0,buffer.width,buffer.height);
        if (item->draw.itemState & ODS_HOTLIGHT) state |= CDIS_HOT;
        if (item->draw.itemState & ODS_SELECTED) state |= CDIS_SELECTED;
        appearance_toolbar_highlight(&buffer,state);
        os_zero_memory(&metrics,sizeof(metrics));
        metrics.cbSize = sizeof(metrics);
        if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS,sizeof(metrics),&metrics,0))
        {
            font = CreateFontIndirectW(&metrics.lfMenuFont);
            if (font) SelectObject(buffer.dc,font);
        }
        if (item->draw.itemState & ODS_NOACCEL) flags |= DT_HIDEPREFIX;
        appearance_text(hwnd,buffer.dc,text,&rect,flags,appearance_menu_glass);
        appearance_end(item->draw.hDC,&bounds,&buffer,appearance_menu_glass);
        if (font) DeleteObject(font);
        return 1;
    }
    return 0;
}

static appearance_style_t appearance_page_read(HWND hwnd)
{
    appearance_style_t style=appearance_style();
    BOOL valid;
    int value;
    value=ComboBox_GetCurSel(GetDlgItem(hwnd,IDC_TEXT_STYLE));
    if (value>=0 && value<=3) style.text=value;
    value=ComboBox_GetCurSel(GetDlgItem(hwnd,IDC_ICON_STYLE));
    if (value>=0 && value<=4) style.icon=value;
    style.custom=IsDlgButtonChecked(hwnd,IDC_CUSTOM_GLOW)==BST_CHECKED;
    value=GetDlgItemInt(hwnd,IDC_GLOW_SIZE,&valid,FALSE);
    if (valid) style.size=value<0 ? 0 : value>12 ? 12 : value;
    value=GetDlgItemInt(hwnd,IDC_GLOW_STRENGTH,&valid,FALSE);
    if (valid) style.strength=value<0 ? 0 : value>100 ? 100 : value;
    style.color=(COLORREF)GetWindowLongPtr(GetDlgItem(hwnd,IDC_GLOW_COLOR),GWLP_USERDATA);
    return style;
}

static void appearance_page_update(HWND hwnd)
{
    appearance_style_t style;
    // Edit/spin controls send notifications even before WM_INITDIALOG.
    // Wait until all controls have their final values before updating them.
    if (!GetWindowLongPtr(hwnd,DWLP_USER)) return;
    style=appearance_page_read(hwnd);
    int glow=style.text==0 || style.icon==1;
    int custom=style.icon==1 || (style.text==0 && style.custom);
    int size_ids[]={IDC_GLOW_SIZE,IDC_GLOW_SIZE_SPIN,IDC_GLOW_SIZE_SLIDER};
    int effect_ids[]={IDC_GLOW_STRENGTH,IDC_GLOW_STRENGTH_SPIN,IDC_GLOW_STRENGTH_SLIDER,IDC_GLOW_COLOR};
    int i;
    for (i=0;i<3;i++) EnableWindow(GetDlgItem(hwnd,size_ids[i]),glow);
    for (i=0;i<4;i++) EnableWindow(GetDlgItem(hwnd,effect_ids[i]),custom);
    SendDlgItemMessage(hwnd,IDC_GLOW_SIZE_SLIDER,TBM_SETPOS,TRUE,style.size);
    SendDlgItemMessage(hwnd,IDC_GLOW_STRENGTH_SLIDER,TBM_SETPOS,TRUE,style.strength);
    EnableWindow(GetDlgItem(hwnd,IDC_CUSTOM_GLOW),glow);
    os_SetDlgItemText_localization_id(hwnd,IDC_GLOW_DESCRIPTION,
        custom && style.size>0 && style.strength>0 && !glow_available() ? LOCALIZATION_ID_GLOW_UNAVAILABLE :
        style.custom ? LOCALIZATION_ID_GLOW_DESC_CUSTOM : LOCALIZATION_ID_GLOW_DESC_NATIVE);
    InvalidateRect(GetDlgItem(hwnd,IDC_GLOW_PREVIEW),NULL,FALSE);
    InvalidateRect(GetDlgItem(hwnd,IDC_GLOW_COLOR),NULL,FALSE);
}

static void appearance_page_init(HWND hwnd)
{
    appearance_style_t style=appearance_style();
    int labels[]={IDC_TEXT_STYLE_LABEL,IDC_ICON_STYLE_LABEL,IDC_CUSTOM_GLOW,IDC_GLOW_SIZE_LABEL,IDC_GLOW_STRENGTH_LABEL,IDC_GLOW_COLOR_LABEL};
    localization_id_t strings[]={LOCALIZATION_ID_TEXT_STYLE,LOCALIZATION_ID_ICON_STYLE,LOCALIZATION_ID_CUSTOM_GLOW,LOCALIZATION_ID_GLOW_SIZE,LOCALIZATION_ID_GLOW_STRENGTH,LOCALIZATION_ID_GLOW_COLOR};
    localization_id_t styles[]={LOCALIZATION_ID_STYLE_GLOW,LOCALIZATION_ID_STYLE_WHITE,LOCALIZATION_ID_STYLE_SHADOW,LOCALIZATION_ID_STYLE_BLACK};
    int i;
    for (i=0;i<6;i++) os_SetDlgItemText_localization_id(hwnd,labels[i],strings[i]);
    os_ComboBox_AddString_localization_id(hwnd,IDC_ICON_STYLE,LOCALIZATION_ID_STYLE_ORIGINAL);
    for (i=0;i<4;i++)
    {
        os_ComboBox_AddString_localization_id(hwnd,IDC_TEXT_STYLE,styles[i]);
        os_ComboBox_AddString_localization_id(hwnd,IDC_ICON_STYLE,styles[i]);
    }
    ComboBox_SetCurSel(GetDlgItem(hwnd,IDC_TEXT_STYLE),style.text);
    ComboBox_SetCurSel(GetDlgItem(hwnd,IDC_ICON_STYLE),style.icon);
    CheckDlgButton(hwnd,IDC_CUSTOM_GLOW,style.custom ? BST_CHECKED : BST_UNCHECKED);
    SendDlgItemMessage(hwnd,IDC_GLOW_SIZE_SPIN,UDM_SETRANGE32,0,12);
    SendDlgItemMessage(hwnd,IDC_GLOW_STRENGTH_SPIN,UDM_SETRANGE32,0,100);
    SendDlgItemMessage(hwnd,IDC_GLOW_SIZE_SLIDER,TBM_SETRANGE,TRUE,MAKELPARAM(0,12));
    SendDlgItemMessage(hwnd,IDC_GLOW_STRENGTH_SLIDER,TBM_SETRANGE,TRUE,MAKELPARAM(0,100));
    SetDlgItemInt(hwnd,IDC_GLOW_SIZE,style.size,FALSE);
    SetDlgItemInt(hwnd,IDC_GLOW_STRENGTH,style.strength,FALSE);
    SetWindowLongPtr(GetDlgItem(hwnd,IDC_GLOW_COLOR),GWLP_USERDATA,style.color);
    SetWindowLongPtr(hwnd,DWLP_USER,1);
    appearance_page_update(hwnd);
}

static int appearance_page_save(HWND hwnd)
{
    appearance_style_t style=appearance_page_read(hwnd);
    if (style.size>0 && style.strength>0 && (style.icon==1 || (style.text==0 && style.custom)) && !glow_available())
    {
        WCHAR message[STRING_SIZE];
        string_copy_utf8_string(message,localization_get_string(LOCALIZATION_ID_GLOW_UNAVAILABLE));
        MessageBoxW(hwnd,message,L"void Image Viewer",MB_OK|MB_ICONERROR);
        return 0;
    }
    config_text_style=style.text; config_icon_style=style.icon;
    config_custom_glow=style.custom; config_glow_size=style.size;
    config_glow_strength=style.strength; config_glow_color=style.color;
    return 1;
}

static int appearance_page_draw(HWND hwnd,DRAWITEMSTRUCT *draw)
{
    appearance_style_t style=appearance_page_read(hwnd);
    if (draw->CtlID==IDC_GLOW_COLOR)
    {
        RECT rect=draw->rcItem;
        HBRUSH brush;
        DrawFrameControl(draw->hDC,&rect,DFC_BUTTON,DFCS_BUTTONPUSH | ((draw->itemState&ODS_SELECTED) ? DFCS_PUSHED : 0));
        InflateRect(&rect,-4,-4);
        brush=CreateSolidBrush((draw->itemState&ODS_DISABLED) ? RGB(105,105,105) : style.color);
        FillRect(draw->hDC,&rect,brush);
        DeleteObject(brush);
        FrameRect(draw->hDC,&rect,GetSysColorBrush(COLOR_3DSHADOW));
        if (draw->itemState&ODS_FOCUS) { rect=draw->rcItem; InflateRect(&rect,-2,-2); DrawFocusRect(draw->hDC,&rect); }
        return 1;
    }
    if (draw->CtlID==IDC_GLOW_PREVIEW)
    {
        appearance_buffer_t buffer;
        RECT text;
        WCHAR label[STRING_SIZE];
        int x,y,i,icon_size=MulDiv(16,os_logical_high,96);
        int icons[]={IDI_PREV,IDI_PLAY,IDI_BESTFIT};
        if (!appearance_begin(draw->hDC,&draw->rcItem,&buffer,1)) return 1;
        for (y=0;y<buffer.height;y++) for (x=0;x<buffer.width;x++)
        {
            DWORD r=170-130*x/buffer.width,g=190-135*x/buffer.width,b=210-140*x/buffer.width;
            buffer.pixels[y*buffer.width+x]=0xff000000|(r<<16)|(g<<8)|b;
        }
        SelectObject(buffer.dc,(HFONT)SendMessage(hwnd,WM_GETFONT,0,0));
        SetRect(&text,MulDiv(12,os_logical_wide,96),0,buffer.width/2,buffer.height);
        string_copy_utf8_string(label,localization_get_string(LOCALIZATION_ID_GLOW_PREVIEW));
        appearance_styled_text(hwnd,buffer.dc,label,&text,DT_LEFT|DT_VCENTER|DT_SINGLELINE,&style);
        for (i=0;i<3;i++)
        {
            HICON icon=(HICON)LoadImage(os_hinstance,MAKEINTRESOURCE(icons[i]),IMAGE_ICON,icon_size,icon_size,LR_SHARED);
            appearance_icon(buffer.dc,icon,buffer.width/2+i*(buffer.width/6),
                (buffer.height-icon_size)/2,icon_size,icon_size,1,&style);
        }
        appearance_end(draw->hDC,&draw->rcItem,&buffer,0);
        return 1;
    }
    return 0;
}

static void appearance_menu_separator(HWND hwnd)
{
    RECT window,client,line;
    POINT origin = {0,0};
    HDC dc;
    appearance_buffer_t buffer;
    if ((!appearance_dark && !appearance_menu_glass) || !GetMenu(hwnd)) return;
    GetWindowRect(hwnd,&window);
    GetClientRect(hwnd,&client);
    ClientToScreen(hwnd,&origin);
    // Clear only the native menu separator. The glass frame edge on the row
    // above it remains intact, matching the sides and bottom of the image.
    SetRect(&line,origin.x-window.left,origin.y-window.top-1,
        origin.x-window.left+client.right,origin.y-window.top);
    dc = GetWindowDC(hwnd);
    if (dc)
    {
        if (appearance_begin(dc,&line,&buffer,appearance_menu_glass))
        {
            if (!appearance_menu_glass)
            {
                RECT fill = {0,0,buffer.width,buffer.height};
                FillRect(buffer.dc,&fill,GetSysColorBrush(COLOR_3DSHADOW));
            }
            appearance_end(dc,&line,&buffer,appearance_menu_glass);
        }
        ReleaseDC(hwnd,dc);
    }
}
