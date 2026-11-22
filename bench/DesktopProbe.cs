using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Text;

public static class DesktopProbe {
    [StructLayout(LayoutKind.Sequential)]
    public struct Memory {
        public uint cb, faults;
        public UIntPtr peakWS, ws, peakPaged, paged, peakNonpaged, nonpaged, pagefile, peakPagefile, commit, privateWS;
        public ulong sharedCommit;
    }
    [DllImport("kernel32", SetLastError=true)]
    static extern bool K32GetProcessMemoryInfo(IntPtr h,out Memory counters,uint size);
    public static Memory Read(Process p) {
        Memory counters;
        if (!K32GetProcessMemoryInfo(p.Handle,out counters,(uint)Marshal.SizeOf(typeof(Memory))))
            throw new Win32Exception(Marshal.GetLastWin32Error());
        return counters;
    }
    public delegate bool EnumProc(IntPtr h,IntPtr data);
    [DllImport("user32")] static extern bool EnumWindows(EnumProc cb,IntPtr data);
    [DllImport("user32")] static extern uint GetWindowThreadProcessId(IntPtr w,out uint pid);
    [DllImport("user32")] static extern bool IsWindowVisible(IntPtr w);
    [DllImport("user32",CharSet=CharSet.Unicode)] static extern int GetClassName(IntPtr w,StringBuilder text,int size);
    [DllImport("user32",CharSet=CharSet.Unicode)] static extern int GetWindowText(IntPtr w,StringBuilder text,int size);
    [DllImport("user32")] static extern bool GetWindowRect(IntPtr w,out Rect r);
    [DllImport("user32")] public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr dpi);
    [DllImport("user32")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32")] static extern IntPtr GetDC(IntPtr w);
    [DllImport("user32")] static extern int ReleaseDC(IntPtr w,IntPtr dc);
    [DllImport("gdi32")] static extern int GetPixel(IntPtr dc,int x,int y);
    [DllImport("user32")] public static extern bool PostMessage(IntPtr h,uint msg,IntPtr wp,IntPtr lp);
    [DllImport("user32")] static extern IntPtr GetDlgItem(IntPtr h,int id);
    [DllImport("user32")] static extern bool IsWindowEnabled(IntPtr w);
    public static bool Enabled(IntPtr card,int id) {return IsWindowEnabled(GetDlgItem(card,id));}
    public static string Caption(IntPtr card,int id) {
        var value=new StringBuilder(64);
        // Use WM_GETTEXT for a child control owned by a different process.
        SendMessageTimeout(GetDlgItem(card,id),0x000D,(IntPtr)value.Capacity,value,2,300,out _);
        return value.ToString();
    }
    [DllImport("user32",CharSet=CharSet.Unicode)]
    static extern IntPtr SendMessageTimeout(IntPtr w,uint msg,IntPtr wp,StringBuilder value,uint flags,uint timeout,out IntPtr result);
    public static void Click(IntPtr verifiedFixtureCard,int id) {
        IntPtr button=GetDlgItem(verifiedFixtureCard,id);
        if (button==IntPtr.Zero || !PostMessage(button,0x00F5,IntPtr.Zero,IntPtr.Zero)) throw new Exception("Missing button");
    }
    [StructLayout(LayoutKind.Sequential)] struct Rect {public int left,top,right,bottom;}
    public sealed class Windows {
        public IntPtr card, fixture, host;
        public string title="";
        public bool labelVisible, anyVisibleSong;
    }
    public static Windows Inspect(int process) {
        var found=new Windows();
        EnumWindows((h,data)=>{
            uint pid; GetWindowThreadProcessId(h,out pid);
            if (pid!=process) return true;
            var cls=new StringBuilder(160); GetClassName(h,cls,cls.Capacity);
            string name=cls.ToString();
            if(name=="MusicWidget.SilentFixture") found.fixture=h;
            if(name=="MusicWidget.Host") found.host=h;
            var caption=new StringBuilder(160); GetWindowText(h,caption,caption.Capacity);
            if(caption.ToString().StartsWith("Widget fixture ",StringComparison.Ordinal)) {
                found.title=caption.ToString(); found.labelVisible|=IsWindowVisible(h);
            }
            if(IsWindowVisible(h)) {
                Rect r; GetWindowRect(h,out r);
                if(name=="MusicWidget.Text" || (name.StartsWith("WindowsForms10.Window") && caption.Length>0 &&
                    r.right-r.left>r.bottom-r.top && r.bottom-r.top<100)) found.anyVisibleSong=true;
                double ratio=(r.right-r.left)/(double)Math.Max(1,r.bottom-r.top);
                if(name=="MusicWidget.Card" || (name.StartsWith("WindowsForms10.Window") && ratio>2.95 && ratio<3.25 && r.bottom-r.top>=100))
                    found.card=h;
            }
            return true;
        },IntPtr.Zero);
        return found;
    }
    public static int CoverColor(IntPtr card) {
        Rect r; GetWindowRect(card,out r);
        double scale=(r.right-r.left)/340.0;
        IntPtr dc=GetDC(IntPtr.Zero);
        try {return GetPixel(dc,r.left+(int)(54*scale),r.top+(int)(55*scale));}
        finally {ReleaseDC(IntPtr.Zero,dc);}
    }
}
