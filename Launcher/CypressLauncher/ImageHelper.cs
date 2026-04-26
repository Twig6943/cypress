using SkiaSharp;
using System;
using System.IO;

static class ImageHelper
{
    public static string ResizeToSquarePngBase64(string path, int size)
    {
        if (!File.Exists(path)) return string.Empty;
        using var original = SKBitmap.Decode(path);
        if (original == null) return string.Empty;

        using var resized = original.Resize(new SKImageInfo(size, size), SKFilterQuality.High);
        if (resized == null) return string.Empty;

        using var image = SKImage.FromBitmap(resized);
        using var data = image.Encode(SKEncodedImageFormat.Png, 100);
        return Convert.ToBase64String(data.ToArray());
    }

    public static string ResizeByHeightToPngBase64(string path, int maxHeight)
    {
        if (!File.Exists(path)) return string.Empty;
        using var original = SKBitmap.Decode(path);
        if (original == null) return string.Empty;

        if (original.Height <= maxHeight)
        {
            using var img = SKImage.FromBitmap(original);
            using var d = img.Encode(SKEncodedImageFormat.Png, 100);
            return Convert.ToBase64String(d.ToArray());
        }

        float scale = (float)maxHeight / original.Height;
        int newWidth = (int)(original.Width * scale);
        using var resized = original.Resize(new SKImageInfo(newWidth, maxHeight), SKFilterQuality.High);
        if (resized == null) return string.Empty;

        using var image = SKImage.FromBitmap(resized);
        using var data = image.Encode(SKEncodedImageFormat.Png, 100);
        return Convert.ToBase64String(data.ToArray());
    }

    public static string ResizeByWidthToJpegBase64(string path, int maxWidth, int quality)
    {
        if (!File.Exists(path)) return string.Empty;
        using var original = SKBitmap.Decode(path);
        if (original == null) return string.Empty;

        if (original.Width <= maxWidth)
        {
            using var img = SKImage.FromBitmap(original);
            using var d = img.Encode(SKEncodedImageFormat.Jpeg, quality);
            return Convert.ToBase64String(d.ToArray());
        }

        float scale = (float)maxWidth / original.Width;
        int newHeight = (int)(original.Height * scale);
        using var resized = original.Resize(new SKImageInfo(maxWidth, newHeight), SKFilterQuality.High);
        if (resized == null) return string.Empty;

        using var image = SKImage.FromBitmap(resized);
        using var data = image.Encode(SKEncodedImageFormat.Jpeg, quality);
        return Convert.ToBase64String(data.ToArray());
    }
}
