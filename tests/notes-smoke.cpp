#include "notes-smoke.hpp"

#include "notes/notes_codec.hpp"

#include <QBuffer>
#include <QImage>
#include <QJsonDocument>

#include <cmath>

namespace {
NotesPayload samplePayload(int count, const QSize &size) {
  NotesPayload payload;
  payload.imageSize = size;
  for (int i = 0; i < count; ++i) {
    NoteTarget note;
    note.id = QStringLiteral("note-%1").arg(i);
    note.number = i + 1;
    note.position = QPointF(0.1 + i * 0.05, 0.2 + i * 0.05);
    note.instruction = QStringLiteral("Instruction %1").arg(i);
    payload.notes.append(std::move(note));
  }
  return payload;
}

bool fuzzyPositionEqual(const QPointF &a, const QPointF &b) {
  return std::abs(a.x() - b.x()) < 0.0001 && std::abs(a.y() - b.y()) < 0.0001;
}

/// Writes one byte of a raw stream into the RGB LSBs of `image`, mirroring the
/// codec's traversal (raster order, R/G/B, most-significant bit first).
void writeStreamByte(QImage &image, int byteIndex, int value) {
  const int channels[3] = {0x00010000, 0x00000100, 0x00000001};
  for (int i = 0; i < 8; ++i) {
    const int bit = byteIndex * 8 + i;
    const int pixel = bit / 3;
    const int channel = bit % 3;
    const int x = pixel % image.width();
    const int y = pixel / image.width();
    const int bitValue = (value >> (7 - i)) & 1;
    QRgb rgb = image.pixel(x, y);
    rgb = bitValue ? (rgb | channels[channel]) : (rgb & ~channels[channel]);
    image.setPixel(x, y, rgb);
  }
}

bool savePngRoundTrip(const QImage &image, QImage &reloaded) {
  QByteArray png;
  QBuffer buffer(&png);
  if (!buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "PNG")) {
    buffer.close();
    return false;
  }
  buffer.close();
  return reloaded.loadFromData(png, "PNG");
}
} // namespace

bool runNotesSmokeTests(QString &error) {
  {
    QImage image(200, 100, QImage::Format_ARGB32);
    image.fill(Qt::black);
    NotesPayload original = samplePayload(3, image.size());
    QString embedError;
    if (!embedNotesInImage(image, original, embedError)) {
      error = QStringLiteral("roundTrip embed failed: %1").arg(embedError);
      return false;
    }

    NotesPayload decoded;
    QString decodeError;
    if (extractNotesFromImage(image, decoded, decodeError) !=
        NotesCodecError::None) {
      error = QStringLiteral("roundTrip decode failed: %1").arg(decodeError);
      return false;
    }
    if (decoded.protocol != original.protocol) {
      error = QStringLiteral("roundTrip protocol mismatch");
      return false;
    }
    if (decoded.imageSize != original.imageSize) {
      error = QStringLiteral("roundTrip image size mismatch");
      return false;
    }
    if (decoded.notes.size() != original.notes.size()) {
      error = QStringLiteral("roundTrip note count mismatch");
      return false;
    }
    for (int i = 0; i < original.notes.size(); ++i) {
      if (decoded.notes.at(i).id != original.notes.at(i).id) {
        error = QStringLiteral("roundTrip id mismatch at %1").arg(i);
        return false;
      }
      if (decoded.notes.at(i).number != original.notes.at(i).number) {
        error = QStringLiteral("roundTrip number mismatch at %1").arg(i);
        return false;
      }
      if (!fuzzyPositionEqual(decoded.notes.at(i).position,
                              original.notes.at(i).position)) {
        error = QStringLiteral("roundTrip position mismatch at %1").arg(i);
        return false;
      }
      if (decoded.notes.at(i).instruction !=
          original.notes.at(i).instruction) {
        error = QStringLiteral("roundTrip instruction mismatch at %1").arg(i);
        return false;
      }
    }
  }

  {
    QImage image(400, 200, QImage::Format_ARGB32);
    image.fill(Qt::white);
    NotesPayload payload;
    payload.imageSize = image.size();
    NoteTarget note;
    note.id = QStringLiteral("unicode");
    note.number = 7;
    note.position = QPointF(0.5, 0.5);
    note.instruction = QStringLiteral("First line\nSecond line 日本語 🎉\n");
    payload.notes.append(note);

    QString embedError;
    if (!embedNotesInImage(image, payload, embedError)) {
      error = QStringLiteral("unicodeMultiline embed failed: %1")
                  .arg(embedError);
      return false;
    }
    NotesPayload decoded;
    QString decodeError;
    if (extractNotesFromImage(image, decoded, decodeError) !=
        NotesCodecError::None) {
      error = QStringLiteral("unicodeMultiline decode failed: %1")
                  .arg(decodeError);
      return false;
    }
    if (decoded.notes.first().instruction != note.instruction) {
      error = QStringLiteral("unicodeMultiline instruction mismatch");
      return false;
    }
  }

  {
    QImage smallImage(10, 10, QImage::Format_RGB32);
    smallImage.fill(Qt::black);
    NotesPayload bigPayload;
    bigPayload.imageSize = smallImage.size();
    NoteTarget note;
    note.instruction = QString(2000, QChar('a'));
    bigPayload.notes.append(note);

    const QRgb before = smallImage.pixel(3, 3);
    QString capacityError;
    if (embedNotesInImage(smallImage, bigPayload, capacityError)) {
      error = QStringLiteral("capacityCheck should have failed");
      return false;
    }
    if (capacityError.isEmpty()) {
      error = QStringLiteral("capacityCheck produced no error message");
      return false;
    }
    if (smallImage.pixel(3, 3) != before) {
      error = QStringLiteral("capacityCheck modified the image on failure");
      return false;
    }

    QImage bigImage(400, 400, QImage::Format_RGB32);
    bigImage.fill(Qt::black);
    QString okError;
    if (!embedNotesInImage(bigImage, bigPayload, okError)) {
      error = QStringLiteral("capacityCheck large embed failed: %1")
                  .arg(okError);
      return false;
    }
  }

  {
    // Too small to even hold the header: extraction must say "no payload",
    // not claim a corrupt one.
    QImage tiny(2, 2, QImage::Format_ARGB32);
    tiny.fill(Qt::black);
    NotesPayload decoded;
    QString decodeError;
    if (extractNotesFromImage(tiny, decoded, decodeError) !=
        NotesCodecError::NoPayload) {
      error = QStringLiteral("tinyImage should report NoPayload");
      return false;
    }
  }

  {
    QImage image(100, 100, QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    NotesPayload payload = samplePayload(2, image.size());
    QString embedError;
    if (!embedNotesInImage(image, payload, embedError)) {
      error = QStringLiteral("transparency embed failed: %1").arg(embedError);
      return false;
    }
    NotesPayload decoded;
    QString decodeError;
    if (extractNotesFromImage(image, decoded, decodeError) !=
        NotesCodecError::None) {
      error = QStringLiteral("transparency decode failed: %1").arg(decodeError);
      return false;
    }
    if (decoded.notes.size() != payload.notes.size()) {
      error = QStringLiteral("transparency note count mismatch");
      return false;
    }
  }

  {
    // A premultiplied image with partial alpha must survive a PNG save: the
    // codec writes into the straight ARGB32 form so unpremultiplying on save
    // cannot destroy the payload bits.
    QImage image(300, 200, QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < image.height(); ++y) {
      QRgb *line = reinterpret_cast<QRgb *>(image.scanLine(y));
      for (int x = 0; x < image.width(); ++x) {
        line[x] = qRgba(40 + (x % 200), 80 + (y % 120), 160, 96 + (x % 128));
      }
    }
    NotesPayload payload;
    payload.imageSize = image.size();
    NoteTarget note;
    note.id = QStringLiteral("alpha");
    note.number = 3;
    note.position = QPointF(0.25, 0.75);
    note.instruction = QStringLiteral("Translucent target");
    payload.notes.append(note);

    QString embedError;
    if (!embedNotesInImage(image, payload, embedError)) {
      error = QStringLiteral("premultipliedAlpha embed failed: %1")
                  .arg(embedError);
      return false;
    }
    QImage reloaded;
    if (!savePngRoundTrip(image, reloaded)) {
      error = QStringLiteral("premultipliedAlpha PNG round trip failed");
      return false;
    }
    NotesPayload decoded;
    QString decodeError;
    if (extractNotesFromImage(reloaded, decoded, decodeError) !=
        NotesCodecError::None) {
      error = QStringLiteral("premultipliedAlpha decode failed: %1")
                  .arg(decodeError);
      return false;
    }
    if (decoded.notes.isEmpty() ||
        decoded.notes.first().instruction != note.instruction) {
      error = QStringLiteral("premultipliedAlpha instruction mismatch");
      return false;
    }
  }

  {
    // Packed 24-bit input must be converted, not reinterpreted as QRgb.
    QImage image(200, 120, QImage::Format_RGB888);
    image.fill(QColor(12, 200, 40));
    NotesPayload payload = samplePayload(2, image.size());
    QString embedError;
    if (!embedNotesInImage(image, payload, embedError)) {
      error = QStringLiteral("rgb888 embed failed: %1").arg(embedError);
      return false;
    }
    NotesPayload decoded;
    QString decodeError;
    if (extractNotesFromImage(image, decoded, decodeError) !=
        NotesCodecError::None) {
      error = QStringLiteral("rgb888 decode failed: %1").arg(decodeError);
      return false;
    }
    if (decoded.notes.size() != payload.notes.size()) {
      error = QStringLiteral("rgb888 note count mismatch");
      return false;
    }
  }

  {
    QImage image(200, 100, QImage::Format_ARGB32);
    image.fill(Qt::black);
    NotesPayload payload = samplePayload(1, image.size());
    QString embedError;
    if (!embedNotesInImage(image, payload, embedError)) {
      error = QStringLiteral("corruptHeader embed failed: %1").arg(embedError);
      return false;
    }

    image = image.convertToFormat(QImage::Format_ARGB32);
    QRgb pixel = image.pixel(0, 0);
    pixel ^= 0x00010000;
    image.setPixel(0, 0, pixel);

    NotesPayload decoded;
    QString decodeError;
    if (extractNotesFromImage(image, decoded, decodeError) !=
        NotesCodecError::NoPayload) {
      error = QStringLiteral("corruptHeader should report NoPayload");
      return false;
    }
  }

  {
    // Tamper with a byte that is actually inside the payload, so the checksum
    // mismatch is what rejects it.
    QImage image(200, 100, QImage::Format_ARGB32);
    image.fill(Qt::black);
    NotesPayload payload = samplePayload(1, image.size());
    QString embedError;
    if (!embedNotesInImage(image, payload, embedError)) {
      error = QStringLiteral("checksumFailure embed failed: %1")
                  .arg(embedError);
      return false;
    }

    QString encodeError;
    const QByteArray stream = encodeNotesPayload(payload, encodeError);
    if (stream.size() <= 20) {
      error = QStringLiteral("checksumFailure stream unexpectedly short");
      return false;
    }
    writeStreamByte(image, 20,
                    static_cast<unsigned char>(stream.at(20)) ^ 0xFF);

    NotesPayload decoded;
    QString decodeError;
    if (extractNotesFromImage(image, decoded, decodeError) !=
        NotesCodecError::CorruptPayload) {
      error = QStringLiteral("checksumFailure should report CorruptPayload");
      return false;
    }
  }

  {
    QImage image(200, 100, QImage::Format_ARGB32);
    image.fill(Qt::black);
    NotesPayload payload = samplePayload(1, image.size());
    QString embedError;
    if (!embedNotesInImage(image, payload, embedError)) {
      error = QStringLiteral("unsupportedVersion embed failed: %1")
                  .arg(embedError);
      return false;
    }
    // Bump the version byte (header offset 4); decode checks it before the CRC.
    writeStreamByte(image, 4, 2);

    NotesPayload decoded;
    QString decodeError;
    if (extractNotesFromImage(image, decoded, decodeError) !=
        NotesCodecError::UnsupportedVersion) {
      error = QStringLiteral("unsupportedVersion should report "
                             "UnsupportedVersion");
      return false;
    }
  }

  {
    // A header whose declared length overruns the image must be rejected as
    // corrupt, never partially decoded.
    QImage image(200, 100, QImage::Format_ARGB32);
    image.fill(Qt::black);
    NotesPayload payload = samplePayload(1, image.size());
    QString embedError;
    if (!embedNotesInImage(image, payload, embedError)) {
      error = QStringLiteral("oversizedLength embed failed: %1")
                  .arg(embedError);
      return false;
    }
    // Header offset 6 holds the big-endian data length; set it far past both
    // the image and the acceptable maximum.
    writeStreamByte(image, 6, 0xFF);
    writeStreamByte(image, 7, 0xFF);
    writeStreamByte(image, 8, 0xFF);
    writeStreamByte(image, 9, 0xFF);

    NotesPayload decoded;
    QString decodeError;
    if (extractNotesFromImage(image, decoded, decodeError) !=
        NotesCodecError::CorruptPayload) {
      error = QStringLiteral("oversizedLength should report CorruptPayload");
      return false;
    }
  }

  {
    QImage image(200, 100, QImage::Format_ARGB32);
    image.fill(Qt::darkGray);
    NotesPayload payload = samplePayload(4, image.size());
    QString embedError;
    if (!embedNotesInImage(image, payload, embedError)) {
      error = QStringLiteral("pngRecompressionPreservesData embed failed: %1")
                  .arg(embedError);
      return false;
    }

    QImage reloaded;
    if (!savePngRoundTrip(image, reloaded)) {
      error = QStringLiteral("pngRecompressionPreservesData save failed");
      return false;
    }
    NotesPayload decoded;
    QString decodeError;
    if (extractNotesFromImage(reloaded, decoded, decodeError) !=
        NotesCodecError::None) {
      error = QStringLiteral("pngRecompressionPreservesData decode failed: %1")
                  .arg(decodeError);
      return false;
    }
    if (decoded.notes.size() != payload.notes.size()) {
      error = QStringLiteral("pngRecompressionPreservesData count mismatch");
      return false;
    }
    if (decoded.notes.first().instruction != payload.notes.first().instruction) {
      error = QStringLiteral(
          "pngRecompressionPreservesData instruction mismatch");
      return false;
    }
  }

  return true;
}
