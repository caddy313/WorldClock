import ctypes
import json
import os
import tkinter as tk
import winreg
from datetime import datetime
from tkinter import colorchooser, messagebox, ttk
from zoneinfo import ZoneInfo, ZoneInfoNotFoundError, available_timezones

from PIL import Image, ImageChops, ImageDraw, ImageFont


class POINT(ctypes.Structure):
    _fields_ = (("x", ctypes.c_long), ("y", ctypes.c_long))


class SIZE(ctypes.Structure):
    _fields_ = (("cx", ctypes.c_long), ("cy", ctypes.c_long))


class BLENDFUNCTION(ctypes.Structure):
    _fields_ = (
        ("BlendOp", ctypes.c_byte),
        ("BlendFlags", ctypes.c_byte),
        ("SourceConstantAlpha", ctypes.c_byte),
        ("AlphaFormat", ctypes.c_byte),
    )


class BITMAPINFOHEADER(ctypes.Structure):
    _fields_ = (
        ("biSize", ctypes.c_uint32),
        ("biWidth", ctypes.c_long),
        ("biHeight", ctypes.c_long),
        ("biPlanes", ctypes.c_ushort),
        ("biBitCount", ctypes.c_ushort),
        ("biCompression", ctypes.c_uint32),
        ("biSizeImage", ctypes.c_uint32),
        ("biXPelsPerMeter", ctypes.c_long),
        ("biYPelsPerMeter", ctypes.c_long),
        ("biClrUsed", ctypes.c_uint32),
        ("biClrImportant", ctypes.c_uint32),
    )


class BITMAPINFO(ctypes.Structure):
    _fields_ = (("bmiHeader", BITMAPINFOHEADER), ("bmiColors", ctypes.c_uint32 * 3))


APP_DIR = os.path.join(os.environ.get("APPDATA", os.path.expanduser("~")), "TimezoneWidget")
CONFIG_PATH = os.path.join(APP_DIR, "config.json")
LAUNCHER_PATH = r"C:\Users\17534\Documents\TimezoneWidget\start_timezone_widget.vbs"
RUN_KEY = r"Software\Microsoft\Windows\CurrentVersion\Run"
RUN_VALUE = "TimezoneWidget"

DEFAULT_CONFIG = {
    "x": 80,
    "y": 80,
    "topmost": True,
    "locked": True,
    "startup": True,
    "size_percent": 75,
    "width_percent": 100,
    "field_spacing_percent": 100,
    "outline_color": "#000000",
    "clocks": [
        {"label": "温哥华", "timezone": "America/Vancouver"},
        {"label": "巴塞尔", "timezone": "Europe/Zurich"},
        {"label": "本地时间", "timezone": "Asia/Shanghai"},
    ],
}


def enable_high_dpi():
    try:
        ctypes.windll.user32.SetProcessDpiAwarenessContext(ctypes.c_void_p(-4))
    except (AttributeError, OSError):
        try:
            ctypes.windll.shcore.SetProcessDpiAwareness(2)
        except (AttributeError, OSError):
            pass


def sync_startup(enabled):
    try:
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, RUN_KEY, 0, winreg.KEY_SET_VALUE) as key:
            if enabled:
                command = f'wscript.exe "{LAUNCHER_PATH}"'
                winreg.SetValueEx(key, RUN_VALUE, 0, winreg.REG_SZ, command)
            else:
                try:
                    winreg.DeleteValue(key, RUN_VALUE)
                except FileNotFoundError:
                    pass
    except OSError:
        pass


def load_config():
    try:
        with open(CONFIG_PATH, "r", encoding="utf-8") as handle:
            saved = json.load(handle)
        config = DEFAULT_CONFIG | saved
        if not isinstance(config.get("clocks"), list) or not config["clocks"]:
            config["clocks"] = DEFAULT_CONFIG["clocks"]
        return config
    except (OSError, ValueError, TypeError):
        return dict(DEFAULT_CONFIG)


class TimezoneWidget:
    BASE_WIDTH = 540
    BASE_HEADER = 34
    BASE_CARD_HEIGHT = 104
    BASE_GAP = 8
    BASE_PAD = 16
    BASE_LOCKED_PAD = 18

    BG = "#f2f4f7"
    CARD = "#ffffff"
    BORDER = "#dfe3e7"
    OUTLINE = "#000000"
    TRANSPARENT_KEY = "#ff00ff"
    TIME = "#5b5d60"
    TEXT = "#202124"
    MUTED = "#74777b"
    SUPERSAMPLE = 4
    FONT_SYMBOL = r"C:\Windows\Fonts\seguisym.ttf"
    FONT_REGULAR = r"C:\Windows\Fonts\segoeui.ttf"
    FONT_BOLD = r"C:\Windows\Fonts\segoeuib.ttf"
    FONT_CHINESE = r"C:\Windows\Fonts\msyh.ttc"

    def __init__(self):
        self.config = load_config()
        self.config.setdefault("locked", True)
        self.config.setdefault("startup", True)
        self.config.setdefault("size_percent", 75)
        self.config.setdefault("width_percent", 100)
        self.config.setdefault("field_spacing_percent", 100)
        self.config.setdefault("outline_color", "#000000")
        self.root = tk.Tk()
        self.root.update_idletasks()
        try:
            self.dpi = ctypes.windll.user32.GetDpiForWindow(self.root.winfo_id())
        except (AttributeError, OSError):
            self.dpi = 96
        self.dpi_scale = max(1.0, self.dpi / 96.0)
        self.user_scale = max(0.5, min(1.5, int(self.config["size_percent"]) / 100.0))
        self.scale = self.dpi_scale * self.user_scale
        self.root.tk.call("tk", "scaling", self.dpi / 72.0)
        self.apply_scale()
        self.root.title("世界时钟")
        self.root.overrideredirect(True)
        self.root.configure(bg="#000000")
        self.root.attributes("-topmost", bool(self.config.get("topmost", True)))

        self.drag_x = 0
        self.drag_y = 0
        self.dragging = False
        self.settings_window = None
        self.row_widgets = []
        self.timezones = sorted(
            zone for zone in available_timezones()
            if "/" in zone and not zone.startswith(("Etc/", "SystemV/", "posix/", "right/"))
        )
        sync_startup(bool(self.config.get("startup", True)))

        self.root.bind("<ButtonPress-1>", self.start_drag)
        self.root.bind("<B1-Motion>", self.do_drag)
        self.root.bind("<ButtonRelease-1>", self.end_drag)
        self.root.bind("<Button-3>", lambda _event: self.open_settings())
        self.root.bind("<Escape>", lambda _event: self.root.destroy())

        self.apply_geometry()
        self.draw()
        self.schedule_update()

    def px(self, value):
        return int(round(value * self.scale))

    def dp(self, value):
        return int(round(value * self.dpi_scale))

    def font_size(self, value):
        return max(7, int(round(value * self.user_scale)))

    def apply_scale(self):
        self.user_scale = max(0.5, min(1.5, int(self.config.get("size_percent", 75)) / 100.0))
        self.scale = self.dpi_scale * self.user_scale
        width_scale = max(0.7, min(1.5, int(self.config.get("width_percent", 100)) / 100.0))
        self.WIDTH = self.px(self.BASE_WIDTH * width_scale)
        self.HEADER = self.px(self.BASE_HEADER)
        self.CARD_HEIGHT = self.px(self.BASE_CARD_HEIGHT)
        self.GAP = self.px(self.BASE_GAP)
        self.PAD = self.px(self.BASE_PAD)
        self.LOCKED_PAD = max(3, self.px(self.BASE_LOCKED_PAD))

    def widget_height(self):
        count = len(self.config["clocks"])
        if self.config.get("locked", True):
            return self.LOCKED_PAD * 2 + count * self.CARD_HEIGHT + max(0, count - 1) * self.GAP
        return self.HEADER + self.PAD * 2 + count * self.CARD_HEIGHT + max(0, count - 1) * self.GAP

    def apply_geometry(self):
        height = self.widget_height()
        screen_w = self.root.winfo_screenwidth()
        screen_h = self.root.winfo_screenheight()
        x = max(0, min(int(self.config.get("x", 80)), screen_w - self.WIDTH))
        y = max(0, min(int(self.config.get("y", 80)), screen_h - height))
        self.root.geometry(f"{self.WIDTH}x{height}+{x}+{y}")
        self.root.update_idletasks()

    def apply_rounded_window(self, height):
        radius = max(14, self.px(22))
        try:
            content_hwnd = self.root.winfo_id()
            wrapper_hwnd = ctypes.windll.user32.GetParent(content_hwnd)
            target_hwnd = wrapper_hwnd or content_hwnd
            region = ctypes.windll.gdi32.CreateRoundRectRgn(
                0, 0, self.WIDTH + 1, height + 1, radius * 2, radius * 2
            )
            if region:
                ctypes.windll.user32.SetWindowRgn(target_hwnd, region, True)
        except (AttributeError, OSError):
            pass

    def draw(self):
        height = self.widget_height()
        factor = self.SUPERSAMPLE
        image = Image.new("RGBA", (self.WIDTH * factor, height * factor), (0, 0, 0, 0))
        painter = ImageDraw.Draw(image)

        def box(coords):
            return tuple(int(round(value * factor)) for value in coords)

        def radius(value):
            return max(1, int(round(value * factor)))

        border_width = max(2, self.px(2))
        outline_color = self.config.get("outline_color", self.OUTLINE)
        painter.rounded_rectangle(
            box((1, 1, self.WIDTH - 1, height - 1)),
            radius=radius(self.px(22)), fill=outline_color,
        )
        inner = 1 + border_width
        painter.rounded_rectangle(
            box((inner, inner, self.WIDTH - inner, height - inner)),
            radius=radius(max(1, self.px(22) - border_width)), fill=self.BG,
        )
        if self.config.get("locked", True):
            y = self.LOCKED_PAD
            card_pad = self.LOCKED_PAD
        else:
            self.draw_text(painter, "世界时钟", self.px(18), self.px(17), self.FONT_CHINESE, 9,
                           self.MUTED, "lm", factor)
            self.draw_text(painter, "⚙", self.WIDTH - self.px(48), self.px(17), self.FONT_SYMBOL, 14,
                           self.MUTED, "mm", factor)
            self.draw_text(painter, "×", self.WIDTH - self.px(20), self.px(17), self.FONT_REGULAR, 16,
                           self.MUTED, "mm", factor)
            y = self.HEADER + self.PAD
            card_pad = self.PAD
        spacing = max(0.7, min(1.2, int(self.config.get("field_spacing_percent", 100)) / 100.0))
        center_x = self.WIDTH / 2
        icon_x = center_x + self.px(68 - self.BASE_WIDTH / 2) * spacing
        time_x = center_x + self.px(235 - self.BASE_WIDTH / 2) * spacing
        label_x = center_x + self.px(380 - self.BASE_WIDTH / 2) * spacing
        for item in self.config["clocks"]:
            try:
                now = datetime.now(ZoneInfo(item["timezone"]))
                time_text = now.strftime("%H:%M")
                icon = "☀" if 7 <= now.hour < 19 else "☾"
            except (ZoneInfoNotFoundError, KeyError):
                time_text = "--:--"
                icon = "?"

            painter.rounded_rectangle(
                box((card_pad, y, self.WIDTH - card_pad, y + self.CARD_HEIGHT)),
                radius=radius(self.px(12)), fill=self.CARD, outline=self.BORDER,
                width=max(factor, self.px(1) * factor),
            )
            center_y = y + self.CARD_HEIGHT / 2
            self.draw_text(painter, icon, icon_x, center_y, self.FONT_SYMBOL, 28, self.TEXT, "mm", factor)
            self.draw_text(painter, time_text, time_x, center_y, self.FONT_BOLD, 34, self.TIME, "mm", factor)
            self.draw_text(painter, item.get("label", ""), label_x, center_y, self.FONT_CHINESE, 18,
                           self.TEXT, "lm", factor)
            y += self.CARD_HEIGHT + self.GAP

        resampling = getattr(Image, "Resampling", Image).LANCZOS
        image = image.resize((self.WIDTH, height), resampling)
        self.update_layered_window(image)

    def draw_text(self, painter, text, x, y, font_path, base_size, color, anchor, factor):
        pixel_size = max(8, int(round(base_size * self.user_scale * self.dpi / 72.0 * factor)))
        font = ImageFont.truetype(font_path, pixel_size)
        painter.text((x * factor, y * factor), text, font=font, fill=color, anchor=anchor)

    def update_layered_window(self, image):
        user32 = ctypes.windll.user32
        gdi32 = ctypes.windll.gdi32
        content_hwnd = self.root.winfo_id()
        hwnd = user32.GetParent(content_hwnd) or content_hwnd
        style = user32.GetWindowLongW(hwnd, -20)
        user32.SetWindowLongW(hwnd, -20, style | 0x00080000)

        red, green, blue, alpha = image.split()
        premultiplied = Image.merge(
            "RGBA",
            (ImageChops.multiply(red, alpha), ImageChops.multiply(green, alpha),
             ImageChops.multiply(blue, alpha), alpha),
        )
        pixels = premultiplied.tobytes("raw", "BGRA")

        screen_dc = user32.GetDC(0)
        memory_dc = gdi32.CreateCompatibleDC(screen_dc)
        bitmap_info = BITMAPINFO()
        bitmap_info.bmiHeader.biSize = ctypes.sizeof(BITMAPINFOHEADER)
        bitmap_info.bmiHeader.biWidth = image.width
        bitmap_info.bmiHeader.biHeight = -image.height
        bitmap_info.bmiHeader.biPlanes = 1
        bitmap_info.bmiHeader.biBitCount = 32
        bits = ctypes.c_void_p()
        bitmap = gdi32.CreateDIBSection(screen_dc, ctypes.byref(bitmap_info), 0, ctypes.byref(bits), None, 0)
        old_bitmap = gdi32.SelectObject(memory_dc, bitmap)
        ctypes.memmove(bits, pixels, len(pixels))

        destination = POINT(self.root.winfo_x(), self.root.winfo_y())
        size = SIZE(image.width, image.height)
        source = POINT(0, 0)
        blend = BLENDFUNCTION(0, 0, 255, 1)
        user32.UpdateLayeredWindow(
            hwnd, screen_dc, ctypes.byref(destination), ctypes.byref(size), memory_dc,
            ctypes.byref(source), 0, ctypes.byref(blend), 2,
        )

        gdi32.SelectObject(memory_dc, old_bitmap)
        gdi32.DeleteObject(bitmap)
        gdi32.DeleteDC(memory_dc)
        user32.ReleaseDC(0, screen_dc)

    def schedule_update(self):
        self.draw()
        delay = max(1000, (60 - datetime.now().second) * 1000)
        self.root.after(delay, self.schedule_update)

    def start_drag(self, event):
        if self.config.get("locked", True):
            return
        if event.y <= self.HEADER and event.x >= self.WIDTH - self.px(34):
            self.root.destroy()
            return
        if event.y <= self.HEADER and event.x >= self.WIDTH - self.px(70):
            self.open_settings()
            return
        self.drag_x = event.x_root - self.root.winfo_x()
        self.drag_y = event.y_root - self.root.winfo_y()
        self.dragging = True

    def do_drag(self, event):
        if self.dragging:
            self.root.geometry(f"+{event.x_root - self.drag_x}+{event.y_root - self.drag_y}")

    def end_drag(self, _event):
        if self.dragging:
            self.dragging = False
            self.config["x"] = self.root.winfo_x()
            self.config["y"] = self.root.winfo_y()
            self.save_config()

    def open_settings(self):
        if self.settings_window and self.settings_window.winfo_exists():
            self.settings_window.lift()
            self.settings_window.focus_force()
            return

        window = tk.Toplevel(self.root)
        self.settings_window = window
        window.title("世界时钟设置")
        window.geometry(f"{self.dp(760)}x{self.dp(460)}")
        window.minsize(self.dp(680), self.dp(340))
        window.attributes("-topmost", True)
        window.configure(bg="#ffffff")
        window.protocol("WM_DELETE_WINDOW", window.destroy)

        header = tk.Frame(window, bg="#ffffff")
        header.pack(fill="x", padx=18, pady=(16, 8))
        tk.Label(header, text="时区与国家/城市", bg="#ffffff", fg=self.TEXT,
                 font=("Microsoft YaHei UI", 14, "bold")).pack(side="left")
        tk.Label(header, text="名称可以填写国家、城市或自定义文字", bg="#ffffff",
                 fg=self.MUTED, font=("Microsoft YaHei UI", 9)).pack(side="left", padx=14)

        table_header = tk.Frame(window, bg="#ffffff")
        table_header.pack(fill="x", padx=18)
        tk.Label(table_header, text="显示名称", width=20, anchor="w", bg="#ffffff",
                 fg=self.MUTED).pack(side="left")
        tk.Label(table_header, text="时区", anchor="w", bg="#ffffff",
                 fg=self.MUTED).pack(side="left", padx=(8, 0))

        container = tk.Frame(window, bg="#ffffff")
        container.pack(fill="both", expand=True, padx=18, pady=6)
        self.rows_container = container
        self.row_widgets = []
        for item in self.config["clocks"]:
            self.add_settings_row(item.get("label", ""), item.get("timezone", "Asia/Shanghai"))

        options = tk.Frame(window, bg="#ffffff")
        options.pack(fill="x", padx=18, pady=(4, 2))
        self.locked_var = tk.BooleanVar(value=bool(self.config.get("locked", True)))
        self.topmost_var = tk.BooleanVar(value=bool(self.config.get("topmost", True)))
        self.startup_var = tk.BooleanVar(value=bool(self.config.get("startup", True)))
        self.size_var = tk.IntVar(value=int(self.config.get("size_percent", 75)))
        self.width_var = tk.IntVar(value=int(self.config.get("width_percent", 100)))
        self.field_spacing_var = tk.IntVar(value=int(self.config.get("field_spacing_percent", 100)))
        self.outline_color_var = tk.StringVar(value=self.config.get("outline_color", "#000000"))
        ttk.Checkbutton(options, text="锁定位置", variable=self.locked_var).pack(side="left")
        ttk.Checkbutton(options, text="始终置顶", variable=self.topmost_var).pack(side="left", padx=20)
        ttk.Checkbutton(options, text="开机自动启动", variable=self.startup_var).pack(side="left")
        ttk.Label(options, text="大小").pack(side="left", padx=(24, 6))
        ttk.Spinbox(options, from_=50, to=150, increment=5, textvariable=self.size_var,
                    width=5).pack(side="left")
        ttk.Label(options, text="%").pack(side="left", padx=(3, 0))

        outline_options = tk.Frame(window, bg="#ffffff")
        outline_options.pack(fill="x", padx=18, pady=(6, 2))
        ttk.Label(outline_options, text="轮廓颜色").pack(side="left")
        ttk.Entry(outline_options, textvariable=self.outline_color_var, width=12).pack(side="left", padx=(8, 6))
        ttk.Button(outline_options, text="选择颜色", command=self.choose_outline_color).pack(side="left")
        ttk.Label(options, text="宽度").pack(side="left", padx=(18, 6))
        ttk.Spinbox(options, from_=70, to=150, increment=5, textvariable=self.width_var,
                    width=5).pack(side="left")
        ttk.Label(options, text="%").pack(side="left", padx=(3, 0))
        ttk.Label(options, text="字段间距").pack(side="left", padx=(18, 6))
        ttk.Spinbox(options, from_=70, to=120, increment=5, textvariable=self.field_spacing_var,
                    width=5).pack(side="left")
        ttk.Label(options, text="%").pack(side="left", padx=(3, 0))

        actions = tk.Frame(window, bg="#ffffff")
        actions.pack(fill="x", padx=18, pady=(6, 16))
        tk.Button(actions, text="＋ 添加时钟", command=self.add_settings_row,
                  relief="flat", bg="#eef2f5", fg=self.TEXT, padx=12, pady=7).pack(side="left")
        tk.Button(actions, text="取消", command=window.destroy,
                  relief="flat", bg="#eef2f5", fg=self.TEXT, padx=16, pady=7).pack(side="right")
        tk.Button(actions, text="保存", command=self.save_settings,
                  relief="flat", bg="#2867c7", fg="#ffffff", padx=18, pady=7).pack(side="right", padx=8)

    def add_settings_row(self, label="", timezone="Asia/Shanghai"):
        if len(self.row_widgets) >= 10:
            messagebox.showinfo("提示", "最多可以添加 10 个时钟。", parent=self.settings_window)
            return
        row = tk.Frame(self.rows_container, bg="#ffffff")
        row.pack(fill="x", pady=4)

        label_var = tk.StringVar(value=label)
        timezone_var = tk.StringVar(value=timezone)
        label_entry = ttk.Entry(row, textvariable=label_var, width=22)
        label_entry.pack(side="left", ipady=5)
        timezone_box = ttk.Combobox(row, textvariable=timezone_var, values=self.timezones, width=48)
        timezone_box.pack(side="left", padx=8, ipady=4, fill="x", expand=True)
        delete_button = tk.Button(row, text="删除", relief="flat", bg="#ffffff", fg="#b3261e",
                                  command=lambda: self.remove_settings_row(row), padx=10)
        delete_button.pack(side="right")
        self.row_widgets.append((row, label_var, timezone_var))

    def remove_settings_row(self, row):
        for item in list(self.row_widgets):
            if item[0] is row:
                self.row_widgets.remove(item)
                row.destroy()
                break

    def choose_outline_color(self):
        _rgb, color = colorchooser.askcolor(
            color=self.outline_color_var.get(), title="选择轮廓颜色", parent=self.settings_window
        )
        if color:
            self.outline_color_var.set(color)

    def save_settings(self):
        clocks = []
        for _row, label_var, timezone_var in self.row_widgets:
            label = label_var.get().strip()
            timezone = timezone_var.get().strip()
            if not label:
                messagebox.showerror("无法保存", "请填写每个时钟的显示名称。", parent=self.settings_window)
                return
            try:
                ZoneInfo(timezone)
            except ZoneInfoNotFoundError:
                messagebox.showerror("无法保存", f"找不到时区：{timezone}", parent=self.settings_window)
                return
            clocks.append({"label": label, "timezone": timezone})

        if not clocks:
            messagebox.showerror("无法保存", "请至少保留一个时钟。", parent=self.settings_window)
            return

        self.config["clocks"] = clocks
        self.config["locked"] = bool(self.locked_var.get())
        self.config["topmost"] = bool(self.topmost_var.get())
        self.config["startup"] = bool(self.startup_var.get())
        try:
            size_percent = int(self.size_var.get())
        except (ValueError, tk.TclError):
            size_percent = 75
        self.config["size_percent"] = max(50, min(150, size_percent))
        try:
            width_percent = int(self.width_var.get())
        except (ValueError, tk.TclError):
            width_percent = 100
        self.config["width_percent"] = max(70, min(150, width_percent))
        try:
            field_spacing_percent = int(self.field_spacing_var.get())
        except (ValueError, tk.TclError):
            field_spacing_percent = 100
        self.config["field_spacing_percent"] = max(70, min(120, field_spacing_percent))
        outline_color = self.outline_color_var.get().strip()
        try:
            self.root.winfo_rgb(outline_color)
        except tk.TclError:
            messagebox.showerror("无法保存", "轮廓颜色无效，请输入例如 #000000。", parent=self.settings_window)
            return
        self.config["outline_color"] = outline_color
        self.root.attributes("-topmost", self.config["topmost"])
        sync_startup(self.config["startup"])
        self.save_config()
        self.apply_scale()
        self.apply_geometry()
        self.draw()
        self.settings_window.destroy()

    def save_config(self):
        os.makedirs(APP_DIR, exist_ok=True)
        with open(CONFIG_PATH, "w", encoding="utf-8") as handle:
            json.dump(self.config, handle, ensure_ascii=False, indent=2)

    def run(self):
        self.root.mainloop()


if __name__ == "__main__":
    enable_high_dpi()
    TimezoneWidget().run()
