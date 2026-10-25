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
static BYTE appearance_dark, appearance_glass, appearance_high_contrast;
static DWORD appearance_build;
static HBRUSH appearance_background;

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
    appearance_margins_t margins = {0,0,0,0};
    if (appearance_composition) appearance_composition(&composition);
    margins.bottom = config_bottom_glass && !appearance_high_contrast && !fullscreen && composition ? height : 0;
    appearance_glass = appearance_extend && SUCCEEDED(appearance_extend(hwnd,&margins)) && margins.bottom > 0;
    if (previous != appearance_glass) RedrawWindow(hwnd,NULL,NULL,RDW_INVALIDATE|RDW_ALLCHILDREN|RDW_FRAME);
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

static void appearance_text(HWND hwnd,HDC dc,LPCWSTR text,RECT *rect,UINT flags,int glass)
{
    HTHEME theme = appearance_open_theme ? appearance_open_theme(hwnd,L"WINDOW") : NULL;
    if (glass && theme && appearance_theme_text)
    {
        DTTOPTS options;
        os_zero_memory(&options,sizeof(options));
        options.dwSize = sizeof(options);
        options.dwFlags = DTT_COMPOSITED | DTT_TEXTCOLOR;
        if (appearance_dark)
        {
            options.dwFlags |= DTT_GLOWSIZE | DTT_SHADOWTYPE;
            options.iGlowSize = 0;
            options.iTextShadowType = 0; // TST_NONE
        }
        else
        {
            options.dwFlags |= DTT_GLOWSIZE;
            options.iGlowSize = MulDiv(6,os_logical_high,96);
        }
        options.crText = appearance_dark ? RGB(255,255,255) : appearance_fg();
        appearance_theme_text(theme,dc,0,0,text,-1,flags,rect,&options);
    }
    else
    {
        SetTextColor(dc,appearance_fg());
        DrawTextW(dc,text,-1,rect,flags);
    }
    if (theme) appearance_close_theme(theme);
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
                appearance_text(hwnd,dc,text,&tab,DT_CENTER|DT_VCENTER|DT_SINGLELINE,0);
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
        grip.left = grip.right - MulDiv(14,os_logical_wide,96);
        appearance_text(hwnd,buffer.dc,L"\x25e2",&grip,DT_RIGHT|DT_BOTTOM|DT_SINGLELINE,appearance_glass);
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
    if (draw->nmcd.dwDrawStage == CDDS_PREPAINT)
    {
        RECT rect;
        appearance_buffer_t buffer;
        GetClientRect(hwnd,&rect);
        if (appearance_begin(draw->nmcd.hdc,&rect,&buffer,appearance_glass))
            appearance_end(draw->nmcd.hdc,&rect,&buffer,appearance_glass);
        return CDRF_NOTIFYITEMDRAW;
    }
    if (draw->nmcd.dwDrawStage == CDDS_ITEMPREPAINT)
    {
        appearance_buffer_t buffer;
        RECT bounds = draw->nmcd.rc, rect;
        TBBUTTON button;
        int index, iw = 0, ih = 0, x, y;
        WCHAR text[512];
        HICON icon;
        index = (int)SendMessage(hwnd,TB_COMMANDTOINDEX,draw->nmcd.dwItemSpec,0);
        if (index < 0 || !SendMessage(hwnd,TB_GETBUTTON,index,(LPARAM)&button)) return CDRF_DODEFAULT;
        if (button.fsStyle & TBSTYLE_SEP) return CDRF_SKIPDEFAULT;
        if (!appearance_begin(draw->nmcd.hdc,&bounds,&buffer,appearance_glass)) return CDRF_DODEFAULT;
        SetRect(&rect,0,0,buffer.width,buffer.height);
        appearance_toolbar_highlight(&buffer,draw->nmcd.uItemState);
        ImageList_GetIconSize(images,&iw,&ih);
        x = MulDiv(5,os_logical_wide,96);
        y = (buffer.height - ih)/2;
        icon = ImageList_GetIcon(images,button.iBitmap,ILD_NORMAL);
        if (icon)
        {
            ICONINFO info;
            DrawIconEx(buffer.dc,x,y,icon,iw,ih,0,NULL,DI_NORMAL);
            // Old masked toolbar icons have no alpha. Recover coverage from
            // their AND mask without making their transparent surround opaque.
            if (GetIconInfo(icon,&info))
            {
                HDC mask = CreateCompatibleDC(buffer.dc);
                HGDIOBJ old = SelectObject(mask,info.hbmMask);
                int ix,iy;
                GdiFlush();
                for (iy=0;iy<ih;iy++) for (ix=0;ix<iw;ix++)
                {
                    int px = x+ix, py = y+iy;
                    if (px>=0 && py>=0 && px<buffer.width && py<buffer.height && GetPixel(mask,ix,iy)==RGB(0,0,0))
                    {
                        DWORD *pixel = buffer.pixels + py*buffer.width + px;
                        if (!(*pixel & 0xff000000)) *pixel |= 0xff000000;
                        // The transport icons are black masked glyphs. Use a
                        // light glyph in dark mode while retaining coverage.
                        if (appearance_dark && button.iBitmap < 4)
                        {
                            DWORD alpha = *pixel >> 24;
                            DWORD color = alpha * 240 / 255;
                            *pixel = (alpha << 24) | (color << 16) | (color << 8) | color;
                        }
                    }
                }
                SelectObject(mask,old);
                DeleteDC(mask);
                DeleteObject(info.hbmMask);
                if (info.hbmColor) DeleteObject(info.hbmColor);
            }
            DestroyIcon(icon);
        }
        // Toolbar strings are retained for accessibility and tooltips even
        // when a button is configured to show only its icon.
        if (button.iString)
        {
            int length = (int)SendMessageW(hwnd,TB_GETBUTTONTEXTW,button.idCommand,0);
            if (length > 0 && length < 512 && buffer.width > iw + x*3)
            {
                SendMessageW(hwnd,TB_GETBUTTONTEXTW,button.idCommand,(LPARAM)text);
                SelectObject(buffer.dc,(HFONT)SendMessage(hwnd,WM_GETFONT,0,0));
                rect.left = x + iw + MulDiv(4,os_logical_wide,96);
                rect.right -= x;
                appearance_text(hwnd,buffer.dc,text,&rect,DT_LEFT|DT_VCENTER|DT_SINGLELINE|DT_NOPREFIX,appearance_glass);
            }
        }
        if (draw->nmcd.uItemState & CDIS_FOCUS)
        {
            InflateRect(&rect,-2,-2);
            DrawFocusRect(buffer.dc,&rect);
        }
        appearance_end(draw->nmcd.hdc,&bounds,&buffer,appearance_glass);
        return CDRF_SKIPDEFAULT;
    }
    return CDRF_DODEFAULT;
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
    if (!appearance_dark) return 0;
    if (msg == 0x0091) // WM_UAHDRAWMENU
    {
        appearance_uah_menu_t *menu = (void *)lp;
        MENUBARINFO info;
        RECT window;
        os_zero_memory(&info,sizeof(info));
        info.cbSize = sizeof(info);
        if (GetMenuBarInfo(hwnd,OBJID_MENU,0,&info))
        {
            GetWindowRect(hwnd,&window);
            OffsetRect(&info.rcBar,-window.left,-window.top);
            FillRect(menu->dc,&info.rcBar,appearance_background);
        }
        return 1;
    }
    if (msg == 0x0092) // WM_UAHDRAWMENUITEM
    {
        appearance_uah_draw_t *item = (void *)lp;
        MENUITEMINFOW info;
        WCHAR text[256];
        RECT rect = item->draw.rcItem;
        HBRUSH brush = appearance_background;
        UINT flags = DT_CENTER|DT_VCENTER|DT_SINGLELINE;
        os_zero_memory(&info,sizeof(info));
        info.cbSize = sizeof(info);
        info.fMask = MIIM_STRING;
        info.dwTypeData = text;
        info.cch = 256;
        if (!GetMenuItemInfoW(item->menu.menu,item->item.position,TRUE,&info)) return 0;
        if (item->draw.itemState & (ODS_HOTLIGHT|ODS_SELECTED)) brush = CreateSolidBrush(RGB(65,65,65));
        FillRect(item->draw.hDC,&rect,brush);
        if (brush != appearance_background) DeleteObject(brush);
        if (item->draw.itemState & ODS_NOACCEL) flags |= DT_HIDEPREFIX;
        SetBkMode(item->draw.hDC,TRANSPARENT);
        SetTextColor(item->draw.hDC,appearance_fg());
        DrawTextW(item->draw.hDC,text,-1,&rect,flags);
        return 1;
    }
    return 0;
}
