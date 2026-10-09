#pragma once

#include <gdk-pixbuf/gdk-pixbuf.h>

#include <memory>
#include <string>

// Reference-counted GdkPixbuf. Pixbufs are immutable once built, so they can
// be handed from worker threads to the main thread freely.
using Image = std::shared_ptr<GdkPixbuf>;

namespace ImageUtils {

// Takes ownership of a new reference (nullptr gives an empty Image).
Image adopt(GdkPixbuf *pixbuf);
Image fromData(const void *data, size_t size);
Image fromFile(const std::string &path);
bool saveJpeg(const Image &image, const std::string &path, int quality);
bool savePng(const Image &image, const std::string &path);

// A blurred copy of `source`, `workSize` pixels on its short side.
Image blurred(const Image &source, int workSize = 96, int radius = 6, int passes = 3);
// Centre crop to a square, scaled to `size` pixels.
Image squareThumbnail(const Image &source, int size);
// Scaled to fit within size x size, keeping the aspect ratio (never enlarged).
Image fitWithin(const Image &source, int size);

} // namespace ImageUtils
