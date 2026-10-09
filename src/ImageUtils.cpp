#include "ImageUtils.h"

#include <algorithm>
#include <cstring>
#include <vector>

namespace {

// One box-blur pass along rows or columns of 4-channel pixels, clamped edges.
void blurPass(const guchar *src, guchar *dst, int w, int h, int stride, int r, bool horizontal)
{
    const int window = 2 * r + 1;
    const int lines = horizontal ? h : w;
    const int length = horizontal ? w : h;
    auto at = [&](const guchar *base, int line, int i) -> const guchar * {
        return horizontal ? base + size_t(line) * stride + size_t(i) * 4
                          : base + size_t(i) * stride + size_t(line) * 4;
    };
    for (int line = 0; line < lines; ++line) {
        int sum[4] = {0, 0, 0, 0};
        for (int i = -r; i <= r; ++i) {
            const guchar *p = at(src, line, std::clamp(i, 0, length - 1));
            for (int c = 0; c < 4; ++c)
                sum[c] += p[c];
        }
        for (int i = 0; i < length; ++i) {
            guchar *out = const_cast<guchar *>(at(dst, line, i));
            for (int c = 0; c < 4; ++c)
                out[c] = guchar(sum[c] / window);
            const guchar *add = at(src, line, std::min(i + r + 1, length - 1));
            const guchar *sub = at(src, line, std::max(i - r, 0));
            for (int c = 0; c < 4; ++c)
                sum[c] += add[c] - sub[c];
        }
    }
}

} // namespace

namespace ImageUtils {

Image adopt(GdkPixbuf *pixbuf)
{
    if (!pixbuf)
        return {};
    return Image(pixbuf, [](GdkPixbuf *p) { g_object_unref(p); });
}

Image fromData(const void *data, size_t size)
{
    if (!data || size == 0)
        return {};
    GdkPixbufLoader *loader = gdk_pixbuf_loader_new();
    GdkPixbuf *result = nullptr;
    if (gdk_pixbuf_loader_write(loader, static_cast<const guchar *>(data), size, nullptr)
        && gdk_pixbuf_loader_close(loader, nullptr)) {
        result = gdk_pixbuf_loader_get_pixbuf(loader);
        if (result)
            g_object_ref(result);
    } else {
        gdk_pixbuf_loader_close(loader, nullptr);
    }
    g_object_unref(loader);
    if (!result)
        return {};
    // Honour EXIF orientation, as image viewers do.
    GdkPixbuf *oriented = gdk_pixbuf_apply_embedded_orientation(result);
    g_object_unref(result);
    return adopt(oriented);
}

Image fromFile(const std::string &path)
{
    GdkPixbuf *pixbuf = gdk_pixbuf_new_from_file(path.c_str(), nullptr);
    if (!pixbuf)
        return {};
    GdkPixbuf *oriented = gdk_pixbuf_apply_embedded_orientation(pixbuf);
    g_object_unref(pixbuf);
    return adopt(oriented);
}

bool saveJpeg(const Image &image, const std::string &path, int quality)
{
    if (!image)
        return false;
    Image rgb = image;
    if (gdk_pixbuf_get_has_alpha(image.get())) {
        // JPEG has no alpha channel: flatten onto black.
        const int w = gdk_pixbuf_get_width(image.get()), h = gdk_pixbuf_get_height(image.get());
        GdkPixbuf *flat = gdk_pixbuf_new(GDK_COLORSPACE_RGB, FALSE, 8, w, h);
        gdk_pixbuf_fill(flat, 0x000000ff);
        gdk_pixbuf_composite(image.get(), flat, 0, 0, w, h, 0, 0, 1, 1, GDK_INTERP_NEAREST, 255);
        rgb = adopt(flat);
    }
    const std::string q = std::to_string(quality);
    return gdk_pixbuf_save(rgb.get(), path.c_str(), "jpeg", nullptr, "quality", q.c_str(), nullptr);
}

bool savePng(const Image &image, const std::string &path)
{
    return image && gdk_pixbuf_save(image.get(), path.c_str(), "png", nullptr, nullptr);
}

Image blurred(const Image &source, int workSize, int radius, int passes)
{
    if (!source)
        return {};
    const int sw = gdk_pixbuf_get_width(source.get());
    const int sh = gdk_pixbuf_get_height(source.get());
    if (sw <= 0 || sh <= 0)
        return {};
    // Scale so that the short side is workSize ("keep aspect ratio by expanding").
    const double scale = double(workSize) / std::min(sw, sh);
    const int w = std::max(1, int(sw * scale + 0.5));
    const int h = std::max(1, int(sh * scale + 0.5));
    GdkPixbuf *scaled = gdk_pixbuf_scale_simple(source.get(), w, h, GDK_INTERP_BILINEAR);
    if (!scaled)
        return {};
    GdkPixbuf *a = gdk_pixbuf_add_alpha(scaled, FALSE, 0, 0, 0);
    g_object_unref(scaled);
    if (!a)
        return {};
    GdkPixbuf *b = gdk_pixbuf_copy(a);
    const int stride = gdk_pixbuf_get_rowstride(a);
    guchar *pa = gdk_pixbuf_get_pixels(a);
    guchar *pb = gdk_pixbuf_get_pixels(b);
    for (int i = 0; i < passes; ++i) {
        blurPass(pa, pb, w, h, stride, radius, true);
        blurPass(pb, pa, w, h, stride, radius, false);
    }
    g_object_unref(b);
    return adopt(a);
}

Image squareThumbnail(const Image &source, int size)
{
    if (!source)
        return {};
    const int w = gdk_pixbuf_get_width(source.get());
    const int h = gdk_pixbuf_get_height(source.get());
    const int side = std::min(w, h);
    if (side <= 0)
        return {};
    GdkPixbuf *sub = gdk_pixbuf_new_subpixbuf(source.get(), (w - side) / 2, (h - side) / 2, side, side);
    GdkPixbuf *thumb = gdk_pixbuf_scale_simple(sub, size, size, GDK_INTERP_HYPER);
    g_object_unref(sub);
    return adopt(thumb);
}

Image fitWithin(const Image &source, int size)
{
    if (!source)
        return {};
    const int w = gdk_pixbuf_get_width(source.get());
    const int h = gdk_pixbuf_get_height(source.get());
    if (w <= size && h <= size)
        return source;
    const double scale = double(size) / std::max(w, h);
    return adopt(gdk_pixbuf_scale_simple(source.get(), std::max(1, int(w * scale + 0.5)),
                                         std::max(1, int(h * scale + 0.5)), GDK_INTERP_HYPER));
}

} // namespace ImageUtils
