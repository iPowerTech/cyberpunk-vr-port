# Builds resources/launcher_logo.png, the launcher's header picture (src/Overlay/LauncherLogo.rc), from
# images/cp2077Logo.png: the game's logo cropped to its letters and scaled to 1092 px, with a red "VR" in the logo's
# manner -- leaning like its strokes, over an offset cyan copy, a few horizontal cuts and shifted slices -- set in the
# free space under the C, kept clear of the logo's strokes by a margin.
#   python resources/make_launcher_logo.py
import random
from pathlib import Path
from PIL import Image, ImageChops, ImageDraw, ImageFilter, ImageFont

ROOT = Path(__file__).resolve().parent.parent
SOURCE = ROOT / "images" / "cp2077Logo.png"
OUT = ROOT / "resources" / "launcher_logo.png"
FONT = r"C:\Windows\Fonts\Orbitron-Bold.ttf"
WIDTH = 1092              # 3x the launcher's field width at 96 dpi
RED = (230, 30, 45, 255)
CYAN = (83, 196, 222, 255)  # the logo's own blue (its shadow, 2077)
LEAN = 0.38               # horizontal shift per unit of height
MARK_HEIGHT = 76          # the VR's height in the 1092 px picture


def vr_mark():
    random.seed(7)
    mask = Image.new("L", (1200, 600), 0)
    ImageDraw.Draw(mask).text((60, 40), "VR", font=ImageFont.truetype(FONT, 460), fill=255)
    mask = mask.crop(mask.getbbox())
    w, h = mask.size
    mask = mask.transform((int(w + LEAN * h), h), Image.AFFINE, (1, LEAN, -LEAN * h, 0, 1, 0), resample=Image.BICUBIC)
    w, h = mask.size
    cut = Image.new("L", mask.size, 255)
    draw = ImageDraw.Draw(cut)
    for _ in range(7):
        y = random.randint(int(h * 0.15), int(h * 0.9))
        t = random.choice([2, 3, 4])
        x0 = random.randint(0, w // 2)
        draw.rectangle([x0, y, x0 + random.randint(w // 4, w), y + t], fill=0)
    mask = ImageChops.multiply(mask, cut)
    for _ in range(3):
        y = random.randint(int(h * 0.2), int(h * 0.85))
        t = random.randint(6, 14)
        dx = random.choice([-14, -9, 9, 14])
        band = mask.crop((0, y, w, y + t))
        mask.paste(0, (0, y, w, y + t))
        mask.paste(band, (dx, y))
    pad = 24
    mark = Image.new("RGBA", (w + 2 * pad, h + 2 * pad), (0, 0, 0, 0))
    shadow = Image.new("L", mark.size, 0)
    shadow.paste(mask, (pad + 9, pad + 7))
    face = Image.new("L", mark.size, 0)
    face.paste(mask, (pad, pad))
    mark.paste(Image.new("RGBA", mark.size, CYAN), (0, 0), shadow)
    mark.paste(Image.new("RGBA", mark.size, RED), (0, 0), face)
    return mark.crop(mark.getchannel("A").getbbox())


def main():
    logo = Image.open(SOURCE).convert("RGBA")
    logo = logo.crop(logo.getchannel("A").getbbox())
    logo = logo.resize((WIDTH, round(logo.height * WIDTH / logo.width)), Image.LANCZOS)
    mark = vr_mark()
    mark = mark.resize((round(mark.width * MARK_HEIGHT / mark.height), MARK_HEIGHT), Image.LANCZOS)
    strokes = logo.getchannel("A").point(lambda v: 255 if v > 16 else 0).filter(ImageFilter.MaxFilter(15))
    solid = mark.getchannel("A").point(lambda v: 255 if v > 16 else 0)
    w, h = mark.size
    # Under the C: the lowest place, then the leftest, where the mark keeps clear of every stroke.
    for y in range(logo.height - h - 6, 120, -2):
        for x in range(12, 60, 2):
            if ImageChops.multiply(strokes.crop((x, y, x + w, y + h)), solid).getbbox() is None:
                logo.alpha_composite(mark, (x, y))
                logo.save(OUT, optimize=True)
                print(f"{OUT.name}: {logo.size}, VR {w}x{h} at ({x}, {y})")
                return
    raise SystemExit("no room under the C for the mark")


if __name__ == "__main__":
    main()
