#!/usr/bin/env python3
"""Regenerates src/wavesill.ico, src/tray_white.ico and src/tray_black.ico.

Both icons share one wave: eight bars whose tops trace a gentle swell (a raised cosine
so the shoulders round off), mapped onto the range 0.34-0.92 of the glyph height.
The exe icon sits on a backdrop with continuous-curvature corners (Figma-style corner
smoothing, not a superellipse). Small sizes use pixel-snapped bars.

Requires Pillow:  pip install pillow
"""
import math
from pathlib import Path

from PIL import Image, ImageDraw

SRC = Path(__file__).resolve().parent.parent / "src"
BACKDROP = (52, 66, 84, 255)


def wave_shape(n=8, crest=2.6, periods=1.05):
    return [(0.5 * (1 + math.cos((i - crest) / (n - 1) * 2 * math.pi * periods))) ** 0.85 for i in range(n)]


HEIGHTS = [0.34 + (0.92 - 0.34) * v for v in wave_shape()]


def smooth_rect_points(w, h, r, smoothing=0.6, seg=24):
    """Polygon for a rounded square with continuous curvature (Figma corner smoothing)."""
    p = min(min(w, h) / 2, (1 + smoothing) * r)
    arc = 90 * (1 - smoothing)
    L = math.sin(math.radians(arc / 2)) * r * math.sqrt(2)
    alpha = (90 - arc) / 2
    p34 = r * math.tan(math.radians(alpha / 2))
    beta = 45 * smoothing
    c = p34 * math.cos(math.radians(beta))
    d = c * math.tan(math.radians(beta))
    b = (p - L - c - d) / 3
    a = 2 * b

    def cubic(P0, P1, P2, P3, n):
        out = []
        for k in range(n + 1):
            t = k / n
            u = 1 - t
            out.append((u * u * u * P0[0] + 3 * u * u * t * P1[0] + 3 * u * t * t * P2[0] + t * t * t * P3[0],
                        u * u * u * P0[1] + 3 * u * u * t * P1[1] + 3 * u * t * t * P2[1] + t * t * t * P3[1]))
        return out

    P0 = (w - p, 0)
    P3 = (w - p + a + b + c, d)
    pts = cubic(P0, (w - p + a, 0), (w - p + a + b, 0), P3, seg)
    cx, cy = w - r, r
    th1 = math.atan2(P3[1] - cy, P3[0] - cx)
    Q0 = (P3[0] + L, P3[1] + L)
    th2 = math.atan2(Q0[1] - cy, Q0[0] - cx)
    for k in range(1, seg + 1):
        t = th1 + (th2 - th1) * k / seg
        pts.append((cx + r * math.cos(t), cy + r * math.sin(t)))
    pts += cubic(Q0, (Q0[0] + d, Q0[1] + c), (Q0[0] + d, Q0[1] + a + b), (w, p), seg)[1:]

    def rot(pt, k):
        x, y = pt[0] - w / 2, pt[1] - h / 2
        for _ in range(k):
            x, y = -y, x
        return (x + w / 2, y + h / 2)

    poly = []
    for k in range(4):
        poly += [rot(q, k) for q in pts]
    return poly


def bars_px(size, color, margin_frac, bar_frac):
    n = len(HEIGHTS)
    bw = max(1, round(size * bar_frac))
    gap = max(1, round(bw * 0.5))
    total = n * bw + (n - 1) * gap
    while total > size - 2 and bw > 1:
        bw -= 1
        gap = max(1, round(bw * 0.5))
        total = n * bw + (n - 1) * gap
    x0 = (size - total) // 2
    maxh = round(size * (1 - 2 * margin_frac))
    base = size - round(size * margin_frac)
    im = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    for i, hh in enumerate(HEIGHTS):
        ph = max(1, round(hh * maxh))
        for x in range(x0 + i * (bw + gap), x0 + i * (bw + gap) + bw):
            for y in range(base - ph, base):
                im.putpixel((x, y), color)
    return im


def bars_smooth(S, color, x0, x1, y_base, gh):
    im = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    n = len(HEIGHTS)
    units = 3 * n - 1
    u = (x1 - x0) / units
    bw, gap = 2 * u, 1 * u
    for i, hh in enumerate(HEIGHTS):
        x = x0 + i * (bw + gap)
        d.rounded_rectangle([x, y_base - hh * gh, x + bw, y_base], radius=bw * 0.5, fill=color)
    return im


def app_icon(size):
    if size <= 24:
        im = Image.new("RGBA", (size, size), (0, 0, 0, 0))
        ImageDraw.Draw(im).rounded_rectangle([0, 0, size - 1, size - 1], radius=max(2, size // 5), fill=BACKDROP)
        im.alpha_composite(bars_px(size, (255, 255, 255, 255), 0.25, 0.09))
        return im
    S = size * 8
    im = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    pad = S * 0.05
    side = S - 2 * pad
    ImageDraw.Draw(im).polygon([(x + pad, y + pad) for x, y in smooth_rect_points(side, side, S * 0.22)], fill=BACKDROP)
    gw, gh = S * 0.70, S * 0.40
    im.alpha_composite(bars_smooth(S, (255, 255, 255, 242), (S - gw) / 2, (S + gw) / 2, (S + gh) / 2, gh))
    return im.resize((size, size), Image.LANCZOS)


def tray_icon(size, color):
    if size <= 32:
        return bars_px(size, color, 0.18, 0.10)
    S = size * 8
    gw, gh = S * 0.92, S * 0.62
    return bars_smooth(S, color, (S - gw) / 2, (S + gw) / 2, (S + gh) / 2, gh).resize((size, size), Image.LANCZOS)


def save_ico(path, images):
    images[-1].save(path, format="ICO", sizes=[im.size for im in images], append_images=images[:-1])


def main():
    sizes = [16, 20, 24, 32, 40, 48, 64, 128, 256]
    save_ico(SRC / "wavesill.ico", [app_icon(s) for s in sizes])
    tray_sizes = [16, 20, 24, 32, 40, 48, 64]
    save_ico(SRC / "tray_white.ico", [tray_icon(s, (255, 255, 255, 255)) for s in tray_sizes])
    save_ico(SRC / "tray_black.ico", [tray_icon(s, (0, 0, 0, 255)) for s in tray_sizes])
    print("Icons written to", SRC)


if __name__ == "__main__":
    main()
