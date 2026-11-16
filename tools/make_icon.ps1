# Cuts Nightshift's icons from the artwork: make_icon.ps1 <resources/icon_source.jpg> <resources folder>
# writes icon.png (the whole tile, 1024), icon_small.png (moon and wave, 128) and logo_64.png (the top bar).
# The tile's rim and corner points below were traced from that picture; re-trace them for new art.
param([string]$src, [string]$outDir)
Add-Type -AssemblyName System.Drawing
Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @"
using System;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;

public static class IconCut
{
    static byte[] px; static int W, H, stride;

    public static void Load (string path)
    {
        using (var bmp = new Bitmap (path))
        {
            W = bmp.Width; H = bmp.Height;
            var copy = new Bitmap (W, H, PixelFormat.Format32bppArgb);
            using (var g = Graphics.FromImage (copy)) g.DrawImage (bmp, 0, 0, W, H);
            var data = copy.LockBits (new Rectangle (0, 0, W, H), ImageLockMode.ReadOnly, PixelFormat.Format32bppArgb);
            stride = data.Stride; px = new byte[stride * H];
            Marshal.Copy (data.Scan0, px, 0, px.Length);
            copy.UnlockBits (data);
        }
    }

    // Bilinear sample, BGR
    static void Sample (double x, double y, out double b, out double g, out double r)
    {
        x = Math.Max (0, Math.Min (W - 1.001, x - 0.5)); y = Math.Max (0, Math.Min (H - 1.001, y - 0.5));
        int x0 = (int) x, y0 = (int) y; double fx = x - x0, fy = y - y0;
        b = g = r = 0;
        for (int j = 0; j < 2; ++j)
            for (int i = 0; i < 2; ++i)
            {
                double w = (i == 0 ? 1 - fx : fx) * (j == 0 ? 1 - fy : fy);
                int o = (y0 + j) * stride + (x0 + i) * 4;
                b += w * px[o]; g += w * px[o + 1]; r += w * px[o + 2];
            }
    }

    public static double[] Average (int x0, int y0, int x1, int y1)
    {
        double b = 0, g = 0, r = 0; int n = 0;
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x) { int o = y * stride + x * 4; b += px[o]; g += px[o + 1]; r += px[o + 2]; ++n; }
        return new double[] { b / n, g / n, r / n };
    }

    // The exponent n of a superellipse |x/a|^n + |y/b|^n = 1 through each traced edge point
    public static double FitN (double cx, double cy, double a, double b, double[] xs, double[] ys)
    {
        var ns = new System.Collections.Generic.List<double>();
        for (int k = 0; k < xs.Length; ++k)
        {
            double u = Math.Abs (xs[k] - cx) / a, v = Math.Abs (ys[k] - cy) / b, lo = 1.5, hi = 12;
            for (int it = 0; it < 60; ++it) { double m = (lo + hi) / 2; if (Math.Pow (u, m) + Math.Pow (v, m) > 1) lo = m; else hi = m; }
            ns.Add ((lo + hi) / 2);
        }
        ns.Sort();
        return ns[ns.Count / 2];
    }

    // Renders source rect (sx, sy, sw, sh) into a size x size image, scaled to fit the width
    // and centred, masked by a superellipse (exponent n) round the drawn tile, inset by inset
    // px. rimWidth > 0 draws a rim of that many px in rimColour (BGR) just inside the edge.
    public static void Render (string outPath, int size, double sx, double sy, double sw, double sh, double n, double inset,
                               double rimWidth, double[] rim)
    {
        double scale = size / Math.Max (sw, sh);
        double dw = sw * scale, dh = sh * scale, ox = (size - dw) / 2, oy = (size - dh) / 2;
        double a = dw / 2 - inset, b = dh / 2 - inset, cx = size / 2.0, cy = size / 2.0;
        var outBmp = new Bitmap (size, size, PixelFormat.Format32bppArgb);
        var data = outBmp.LockBits (new Rectangle (0, 0, size, size), ImageLockMode.WriteOnly, PixelFormat.Format32bppArgb);
        var o = new byte[data.Stride * size];
        const int ss = 4;

        for (int v = 0; v < size; ++v)
            for (int u = 0; u < size; ++u)
            {
                double cover = 0, rb = 0, rg = 0, rr = 0;

                for (int j = 0; j < ss; ++j)
                    for (int i = 0; i < ss; ++i)
                    {
                        double x = u + (i + 0.5) / ss, y = v + (j + 0.5) / ss;
                        double d = Math.Pow (Math.Pow (Math.Abs (x - cx) / a, n) + Math.Pow (Math.Abs (y - cy) / b, n), 1.0 / n);

                        if (d > 1) continue;

                        double sb, sg, sr;
                        Sample (sx + (x - ox) / scale, sy + (y - oy) / scale, out sb, out sg, out sr);

                        if (rimWidth > 0)
                        {
                            // How far in from the edge, in px (near enough along the axes)
                            double fromEdge = (1 - d) * Math.Min (a, b);
                            double t = Math.Max (0, Math.Min (1, 1 - (fromEdge - rimWidth * 0.5) / (rimWidth * 0.75)));
                            sb += (rim[0] - sb) * t; sg += (rim[1] - sg) * t; sr += (rim[2] - sr) * t;
                        }

                        cover += 1; rb += sb; rg += sg; rr += sr;
                    }

                int p = v * data.Stride + u * 4;

                if (cover == 0) { o[p] = o[p + 1] = o[p + 2] = o[p + 3] = 0; continue; }

                o[p] = (byte) Math.Round (rb / cover); o[p + 1] = (byte) Math.Round (rg / cover); o[p + 2] = (byte) Math.Round (rr / cover);
                o[p + 3] = (byte) Math.Round (255.0 * cover / (ss * ss));
            }

        Marshal.Copy (o, 0, data.Scan0, o.Length);
        outBmp.UnlockBits (data);
        outBmp.Save (outPath, ImageFormat.Png);
        outBmp.Dispose();
    }
}
"@

[IconCut]::Load($src)

# The tile's outer rim (traced): x 619..1382, y 187..934; its top-left corner's edge points
$left = 619.0; $top = 187.0; $right = 1382.0; $bottom = 934.0
$cx = ($left + $right) / 2; $cy = ($top + $bottom) / 2; $a = ($right - $left) / 2; $b = ($bottom - $top) / 2
$xs = [double[]](744, 730, 720, 703, 689, 678, 664, 653, 641, 633, 628, 623)
$ys = [double[]](200, 205, 210, 220, 230, 240, 255, 270, 290, 310, 330, 360)
$n = [IconCut]::FitN($cx, $cy, $a, $b, $xs, $ys)
"superellipse n = $n"

# The rim's colour, from its middle on the left side
$rim = [IconCut]::Average(621, 520, 627, 600)
"rim BGR = $($rim -join ', ')"

# The whole tile, name and all: 1024 (Windows' 256 px icon and anything bigger)
[IconCut]::Render("$outDir\icon.png", 1024, $left, $top, $right - $left, $bottom - $top, $n, 2.5, 0, $rim)

# The moon and its wave alone on the tile, for small sizes (16-48 px) and the top bar:
# a square of the tile's inside round them, given a rim like the tile's
foreach ($size in 128, 64) {
    $name = if ($size -eq 128) { 'icon_small.png' } else { 'logo_64.png' }
    [IconCut]::Render("$outDir\$name", $size, 792.0, 268.0, 420.0, 420.0, $n, 0.5, [Math]::Max(1.2, $size * 0.022), $rim)
}
"done"
