# /// script
# dependencies = ["pillow>=11,<13"]
# ///
"""Layout preview using firmware button geometry and its actual 5x7 font (not a device capture)."""
from pathlib import Path
import re
from PIL import Image, ImageDraw

root = Path(__file__).resolve().parents[1]
source = (root / 'firmware/badge/interaction.h').read_text()
buttons = re.findall(r'\{Page::(\w+), Control::(\w+), (\d+), (\d+), (\d+), (\d+)\}', source)
font_source = (root / 'libraries/GFX_Library_for_Arduino/src/font/glcdfont.h').read_text()
font_source = re.sub(r'//[^\n]*|/\*.*?\*/', '', font_source, flags=re.S)
font = bytes(int(v, 16) for v in re.findall(r'0x([0-9a-fA-F]{2})', font_source))

def screen(page, pressed=False):
    im = Image.new('RGB', (466, 466), '#151b24')
    draw = ImageDraw.Draw(im)
    draw.ellipse((0, 0, 465, 465), fill='black')
    def text(y, value, size=2, color='white', center=233):
        x = center - len(value) * 6 * size // 2
        for ch in value:
            for col in range(5):
                bits = font[ord(ch) * 5 + col]
                for row in range(8):
                    if bits & (1 << row):
                        draw.rectangle((x + col * size, y + row * size,
                                        x + (col + 1) * size - 1, y + (row + 1) * size - 1), fill=color)
            x += size * 6
    if page == 'Home':
        text(58, '2026-09-18 16:30:00')
        text(94, 'Wi-Fi OK   100% USB', color='yellow')
        text(156, 'LISTENING' if pressed else 'READY', 3, 'red' if pressed else 'cyan')
        text(204, 'Recording: 4s' if pressed else 'Press BOOT to talk')
        text(234, 'BOOT: send' if pressed else 'BOOT: start recording')
        text(420, 'PWR: screen on/off', 1)
    elif page == 'Settings':
        text(64, 'SETTINGS', 3, 'cyan')
        text(106, 'Touch a button')
        text(128, 'Volume: 60')
        text(224, 'Brightness: 160')
    else:
        for y, value in [(64, 'DEVICE INFO'), (110, 'badge-0.7.0'), (148, 'Wi-Fi: connected'),
                         (180, 'SSID: My Wi-Fi'), (212, 'IP: 192.168.8.121'),
                         (252, 'BLE provisioning off'), (282, 'Enable in Settings'), (320, 'Voice: idle')]:
            text(y, value, 3 if y == 64 else 2, 'cyan' if y == 64 else 'white')
    labels = {'Talk': 'Send question' if pressed else 'Start talking', 'Settings': 'Settings',
              'VolumeDown': '-', 'VolumeUp': '+', 'BrightnessDown': '-', 'BrightnessUp': '+',
              'Wifi': 'Wi-Fi setup', 'Info': 'Info', 'Back': 'Back'}
    for bp, control, *coords in buttons:
        if bp != page: continue
        x, y, w, h = map(int, coords)
        active = pressed and control == 'Talk'
        draw.rounded_rectangle((x, y, x+w-1, y+h-1), radius=12,
                               fill='#20acb5' if active else '#102428', outline='white' if active else 'cyan', width=2)
        text(y + (h-16)//2, labels[control], center=x+w//2)
    return im

output = root / 'validation-output/ui'
output.mkdir(parents=True, exist_ok=True)
panels = [screen('Home'), screen('Home', True), screen('Settings'), screen('Info')]
preview = Image.new('RGB', (466*2, 466*2), '#151b24')
for i, panel in enumerate(panels):
    preview.paste(panel, (i%2*466, i//2*466))
preview.save(output / 'controls.png')
print(output / 'controls.png')
