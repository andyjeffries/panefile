#pragma once

#include <QImage>
#include <QString>

class QMimeData;

namespace pf {

/// Turns a pasted picture into the one-colour mask a folder emblem is made of.
/// Pure functions; no widgets and no IO beyond decoding the bytes it is given.
namespace FolderIconImage {

/// The side of the stored mask, in pixels.
constexpr int kMaskSize = 128;

/// The picture on the clipboard, or null. Tried in order: local files (the
/// last decodable one, since Panefile's copy list is in Ctrl+C order), raw
/// `image/*` bytes, then the platform's image data. `origin` receives the file
/// name, or "clipboard image". Anything over 8192 px a side is refused before
/// it is decoded.
QImage sourceFromClipboard(const QMimeData *mime, QString *origin = nullptr);

/// A 128x128 Format_Grayscale8 mask (white is shape), or null when the picture
/// holds no shape. The background is found from the picture's edge and
/// everything clearly different from it becomes the shape. `invert` swaps
/// shape and background.
QImage toMask(const QImage &source, bool invert);

/// `img:` plus the first 128 bits of SHA-256 over the mask's pixels, in hex.
QString idFor(const QImage &mask);

} // namespace FolderIconImage

} // namespace pf
