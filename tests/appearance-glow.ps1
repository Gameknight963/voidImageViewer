param([string]$BuildExe = "$PSScriptRoot/../vs2026/x64/Release/voidImageViewer.exe")
$ErrorActionPreference='Stop'
Add-Type -AssemblyName System.Drawing
Add-Type @'
using System;
using System.Text;
using System.Runtime.InteropServices;
public static class GlowUI {
    public delegate bool EnumProc(IntPtr hwnd, IntPtr data);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc callback, IntPtr data);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hwnd, out uint pid);
    [DllImport("user32.dll",CharSet=CharSet.Unicode)] public static extern int GetClassName(IntPtr hwnd,StringBuilder text,int count);
    [DllImport("user32.dll",CharSet=CharSet.Unicode)] public static extern IntPtr GetProp(IntPtr hwnd,string key);
    [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr hwnd,int id);
    [DllImport("user32.dll")] public static extern bool IsWindow(IntPtr hwnd);
    [DllImport("user32.dll")] public static extern bool IsWindowEnabled(IntPtr hwnd);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr hwnd,int command);
    [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr hwnd,IntPtr after,int x,int y,int w,int h,uint flags);
    [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr hwnd,uint msg,IntPtr wp,IntPtr lp);
    [DllImport("user32.dll",CharSet=CharSet.Unicode,EntryPoint="SendMessageW")] public static extern IntPtr SendText(IntPtr hwnd,uint msg,IntPtr wp,string text);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr hwnd,uint msg,IntPtr wp,IntPtr lp);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hwnd,out Rect rect);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr hwnd,IntPtr dc,uint flags);
    [StructLayout(LayoutKind.Sequential)] public struct Rect { public int Left,Top,Right,Bottom; }
    public static IntPtr Find(uint pid,string className,int child) {
        IntPtr found=IntPtr.Zero;
        EnumWindows((window,data)=>{
            uint owner; GetWindowThreadProcessId(window,out owner);
            var name=new StringBuilder(128); GetClassName(window,name,128);
            if(owner==pid && name.ToString()==className && (child==0 || GetDlgItem(window,child)!=IntPtr.Zero)) { found=window; return false; }
            return true;
        },IntPtr.Zero);
        return found;
    }
}
'@
$testRoot=Join-Path $env:TEMP ('viv-glow-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $testRoot | Out-Null
$testExe=Join-Path $testRoot 'voidImageViewer.exe'
$testIni=Join-Path $testRoot 'voidImageViewer.ini'
Copy-Item -LiteralPath $BuildExe -Destination $testExe
Set-Content -LiteralPath $testIni -Encoding Ascii -Value "[voidImageViewer]`r`nappdata=0`r`nmultiple_instances=1`r`ncolor_mode=2`r`nbottom_glass=1`r`nmenu_glass=1`r`noptions_last_page=3`r`nwide=800`r`nhigh=600`r`ntext_style=0`r`nicon_style=1`r`ncustom_glow=1`r`nglow_size=6`r`nglow_strength=75`r`nglow_color=16777215"

function Assert($condition,[string]$message) { if (!$condition) { throw $message } }
function Message($window,$message,$wp=0,$lp=0) { return [GlowUI]::SendMessage($window,$message,[IntPtr]$wp,[IntPtr]$lp) }
function Capture($window,[string]$name,[bool]$screen=$false) {
    $rect=New-Object GlowUI+Rect
    [void][GlowUI]::GetWindowRect($window,[ref]$rect)
    $bitmap=New-Object System.Drawing.Bitmap(($rect.Right-$rect.Left),($rect.Bottom-$rect.Top))
    $graphics=[System.Drawing.Graphics]::FromImage($bitmap)
    if ($screen) { $graphics.CopyFromScreen($rect.Left,$rect.Top,0,0,$bitmap.Size) }
    else {
        $dc=$graphics.GetHdc()
        try { [void][GlowUI]::PrintWindow($window,$dc,2) } finally { $graphics.ReleaseHdc($dc) }
    }
    $graphics.Dispose()
    $bitmap.Save((Join-Path $testRoot ($name+'.png')))
    $bitmap.Dispose()
}
function Open-Options {
    [void][GlowUI]::PostMessage($script:main,0x111,[IntPtr]1029,[IntPtr]::Zero)
    for ($attempt=0;$attempt -lt 60;$attempt++) {
        Start-Sleep -Milliseconds 100
        $dialog=[GlowUI]::Find($script:app.Id,'#32770',1136)
        if ($dialog -ne [IntPtr]::Zero) {
            $page=[GlowUI]::GetDlgItem($dialog,1136)
            if ((Message ([GlowUI]::GetDlgItem($page,1078)) 0x146).ToInt32() -eq 5) { Start-Sleep -Milliseconds 200; return $dialog }
        }
        if ($script:app.HasExited) { throw 'Viewer exited during initialization.' }
    }
    throw 'Appearance page did not initialize.'
}
function Finish-Options($dialog,[int]$command) {
    [void][GlowUI]::PostMessage($dialog,0x111,[IntPtr]$command,[IntPtr]::Zero)
    for ($attempt=0;$attempt -lt 80 -and [GlowUI]::IsWindow($dialog);$attempt++) { Start-Sleep -Milliseconds 50 }
    Assert (![GlowUI]::IsWindow($dialog)) 'Options did not finish applying settings.'
    Start-Sleep -Milliseconds 300
}
function Choice($page,[int]$id,[int]$value) {
    $control=[GlowUI]::GetDlgItem($page,$id)
    [void](Message $control 0x14e $value)
    [void](Message $page 0x111 (($id -bor (1 -shl 16))) $control)
}
function Number($page,[int]$id,[int]$value) {
    [void][GlowUI]::SendText([GlowUI]::GetDlgItem($page,$id),0x0c,[IntPtr]::Zero,$value.ToString())
}
function Select-Style($page,[int]$text,[int]$icon) { Choice $page 1076 $text; Choice $page 1078 $icon }

$app=Start-Process -FilePath $testExe -WorkingDirectory $testRoot -WindowStyle Hidden -PassThru
try {
    $main=[IntPtr]::Zero
    for ($attempt=0;$attempt -lt 60 -and $main -eq [IntPtr]::Zero;$attempt++) {
        Start-Sleep -Milliseconds 100
        $main=[GlowUI]::Find($app.Id,'VOIDIMAGEVIEWER',0)
    }
    Assert ($main -ne [IntPtr]::Zero) 'Viewer did not start.'
    [void][GlowUI]::ShowWindow($main,4)
    [void][GlowUI]::SetWindowPos($main,[IntPtr](-1),0,0,0,0,0x13)
    $tb=[GlowUI]::GetDlgItem([GlowUI]::GetDlgItem($main,1114),1114)
    $initialToolbar=New-Object GlowUI+Rect
    [void][GlowUI]::GetWindowRect($tb,[ref]$initialToolbar)
    $dialog=Open-Options
    $page=[GlowUI]::GetDlgItem($dialog,1136)
    Assert ((Message ([GlowUI]::GetDlgItem($page,1076)) 0x146).ToInt32() -eq 4) 'Expected four text styles.'
    Assert ((Message ([GlowUI]::GetDlgItem($page,1076)) 0x147).ToInt32() -eq 0) 'Text style did not load.'
    Assert ((Message ([GlowUI]::GetDlgItem($page,1078)) 0x147).ToInt32() -eq 1) 'Icon style did not load.'
    Assert ((Message ([GlowUI]::GetDlgItem($page,1079)) 0xf0).ToInt32() -eq 1) 'Custom glow did not load.'
    Assert ([GlowUI]::IsWindowEnabled([GlowUI]::GetDlgItem($page,1085))) 'Custom strength control is disabled.'
    Capture $dialog 'custom-glow'
    # Every explicit style must remain selectable without changing color mode.
    for ($style=0;$style -lt 4;$style++) {
        Select-Style $page $style ($style+1)
        Start-Sleep -Milliseconds 100
        Capture $dialog ('style-'+$style)
        Assert ((Message ([GlowUI]::GetDlgItem($page,1070)) 0x147).ToInt32() -eq 2) 'Style selection changed the interface theme.'
    }
    Select-Style $page 0 0
    [void](Message ([GlowUI]::GetDlgItem($page,1079)) 0xf5)
    Assert (![GlowUI]::IsWindowEnabled([GlowUI]::GetDlgItem($page,1085))) 'Native text-only glow should not expose custom strength.'
    Assert ([GlowUI]::IsWindowEnabled([GlowUI]::GetDlgItem($page,1082))) 'Native glow size should remain configurable.'
    Capture $dialog 'native-glow'
    Choice $page 1078 1
    Assert ([GlowUI]::IsWindowEnabled([GlowUI]::GetDlgItem($page,1085))) 'Icon glow should expose strength with native text selected.'
    [void](Message ([GlowUI]::GetDlgItem($page,1079)) 0xf5)
    # Sliders must update their numeric buddies and preview.
    [void](Message ([GlowUI]::GetDlgItem($page,1090)) 0x405 1 8)
    [void](Message $page 0x114 5 ([GlowUI]::GetDlgItem($page,1090)))
    [void](Message ([GlowUI]::GetDlgItem($page,1091)) 0x405 1 60)
    [void](Message $page 0x114 5 ([GlowUI]::GetDlgItem($page,1091)))
    # Open the real native color picker, enter an orange color, and accept it.
    [void][GlowUI]::PostMessage($page,0x111,[IntPtr]1088,[GlowUI]::GetDlgItem($page,1088))
    $picker=[IntPtr]::Zero
    for ($attempt=0;$attempt -lt 40 -and $picker -eq [IntPtr]::Zero;$attempt++) {
        Start-Sleep -Milliseconds 100
        $picker=[GlowUI]::Find($app.Id,'#32770',706)
    }
    Assert ($picker -ne [IntPtr]::Zero) 'Glow color picker did not open.'
    Number $picker 706 220; Number $picker 707 110; Number $picker 708 20
    [void][GlowUI]::PostMessage($picker,0x111,[IntPtr]1,[IntPtr]::Zero)
    Start-Sleep -Milliseconds 150
    Capture $dialog 'custom-orange'
    Finish-Options $dialog 1
    Start-Sleep -Milliseconds 250
    $saved=Get-Content -LiteralPath $testIni -Raw
    foreach ($entry in @('text_style=0','icon_style=1','custom_glow=1','glow_size=8','glow_strength=60','glow_color=1339100','color_mode=2')) {
        Assert ($saved -match ('(?m)^'+[regex]::Escape($entry)+'\r?$')) ('Setting was not saved: '+$entry)
    }
    Capture $main 'main-custom-glow' $true
    $dialog=Open-Options
    $page=[GlowUI]::GetDlgItem($dialog,1136)
    Select-Style $page 3 4
    Number $page 1082 12; Number $page 1085 10
    Finish-Options $dialog 2
    Start-Sleep -Milliseconds 100
    Assert ((Get-Content -LiteralPath $testIni -Raw) -eq $saved) 'Cancel changed persisted settings.'
    # Glow radius and icon styles must preserve the shared toolbar geometry.
    foreach ($pair in @(@(0,1,1),@(0,1,6),@(0,1,12),@(1,2,12),@(2,3,12),@(3,4,12),@(0,0,12))) {
        $dialog=Open-Options; $page=[GlowUI]::GetDlgItem($dialog,1136)
        Select-Style $page $pair[0] $pair[1]
        Number $page 1082 $pair[2]
        Finish-Options $dialog 1
        Start-Sleep -Milliseconds 150
        $tb=[GlowUI]::GetDlgItem([GlowUI]::GetDlgItem($main,1114),1114)
        $tbRect=New-Object GlowUI+Rect
        [void][GlowUI]::GetWindowRect($tb,[ref]$tbRect)
        Assert (($tbRect.Right-$tbRect.Left) -eq ($initialToolbar.Right-$initialToolbar.Left)) 'Glow settings changed toolbar width.'
        Assert (($tbRect.Bottom-$tbRect.Top) -eq ($initialToolbar.Bottom-$initialToolbar.Top)) 'Glow settings changed toolbar height.'
        Assert ($tbRect.Left -eq $initialToolbar.Left -and $tbRect.Top -eq $initialToolbar.Top) 'Glow settings moved the toolbar.'
        Capture $main ('main-style-'+$pair[0]+'-'+$pair[1]+'-radius-'+$pair[2]) $true
    }
    [void][GlowUI]::PostMessage($main,0x10,[IntPtr]::Zero,[IntPtr]::Zero)
    Assert ($app.WaitForExit(5000)) 'Viewer did not close.'
    Assert ($app.ExitCode -eq 0) 'Viewer exited with an error.'
    Write-Output 'PASS: explicit styles, renderer toggle, control enablement, sliders, color picker, persistence, Cancel, preview, and fixed layouts across glow sizes.'
    Write-Output ('Artifacts: '+$testRoot)
}
finally { if (!$app.HasExited) { $app.Kill(); $app.WaitForExit() } }
