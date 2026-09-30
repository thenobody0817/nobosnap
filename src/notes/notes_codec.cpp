/** @fileoverview omasnap-notes/1 encoder/decoder implementation. */
#include "notes/notes_codec.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QtEndian>

#include <libdeflate.h>

#include <array>
#include <cstring>
#include <memory>

namespace {
constexpr char kMagic[4] = {'O', 'M', 'A', 'S'};
constexpr int kHeaderSize = 14;
constexpr quint32 kMaxPayloadBytes = 16 * 1024 * 1024;

[[nodiscard]] QByteArray packHeader(quint8 version, quint8 compression,
                                    quint32 dataLength, const QByteArray &data) {
  QByteArray header;
  header.reserve(kHeaderSize);
  header.append(kMagic, 4);
  header.append(static_cast<char>(version));
  header.append(static_cast<char>(compression));
  char lengthBytes[4];
  qToBigEndian(dataLength, lengthBytes);
  header.append(lengthBytes, 4);
  quint32 crc = libdeflate_crc32(0, header.constData() + 4, 6);
  crc = libdeflate_crc32(crc, data.constData(), data.size());
  char crcBytes[4];
  qToBigEndian(crc, crcBytes);
  header.append(crcBytes, 4);
  return header;
}

[[nodiscard]] bool unpackHeader(const QByteArray &header, quint8 &version,
                                quint8 &compression, quint32 &dataLength,
                                quint32 &storedCrc) {
  if (static_cast<qsizetype>(header.size()) < kHeaderSize)
    return false;
  if (std::memcmp(header.constData(), kMagic, 4) != 0)
    return false;
  version = static_cast<quint8>(header.at(4));
  compression = static_cast<quint8>(header.at(5));
  dataLength = qFromBigEndian<quint32>(
      reinterpret_cast<const uchar *>(header.constData() + 6));
  storedCrc = qFromBigEndian<quint32>(
      reinterpret_cast<const uchar *>(header.constData() + 10));
  return true;
}

[[nodiscard]] bool verifyCrc(const QByteArray &header,
                             const QByteArray &data) {
  quint32 crc =
      libdeflate_crc32(0, header.constData() + 4, 6); // version+compression+length
  crc = libdeflate_crc32(crc, data.constData(), data.size());
  const quint32 stored = qFromBigEndian<quint32>(
      reinterpret_cast<const uchar *>(header.constData() + 10));
  return crc == stored;
}
} // namespace

QJsonObject NotesPayload::toJson() const {
  QJsonObject root;
  root.insert(QStringLiteral("protocol"), protocol);
  QJsonObject image;
  image.insert(QStringLiteral("width"), imageSize.width());
  image.insert(QStringLiteral("height"), imageSize.height());
  root.insert(QStringLiteral("image"), image);
  QJsonArray array;
  for (const NoteTarget &note : notes) {
    QJsonObject object;
    object.insert(QStringLiteral("id"), note.id);
    object.insert(QStringLiteral("number"), note.number);
    QJsonArray position;
    position.append(note.position.x());
    position.append(note.position.y());
    object.insert(QStringLiteral("position"), position);
    object.insert(QStringLiteral("instruction"), note.instruction);
    array.append(object);
  }
  root.insert(QStringLiteral("notes"), array);
  return root;
}

NotesPayload NotesPayload::fromJson(const QJsonObject &object) {
  NotesPayload payload;
  payload.protocol = object.value(QStringLiteral("protocol")).toString();
  const QJsonObject image = object.value(QStringLiteral("image")).toObject();
  payload.imageSize =
      QSize(image.value(QStringLiteral("width")).toInt(),
            image.value(QStringLiteral("height")).toInt());
  for (const QJsonValue value :
       object.value(QStringLiteral("notes")).toArray()) {
    const QJsonObject note = value.toObject();
    NoteTarget target;
    target.id = note.value(QStringLiteral("id")).toString();
    target.number = note.value(QStringLiteral("number")).toInt();
    const QJsonArray position = note.value(QStringLiteral("position")).toArray();
    if (position.size() == 2) {
      target.position = QPointF(position.at(0).toDouble(),
                                position.at(1).toDouble());
    }
    target.instruction = note.value(QStringLiteral("instruction")).toString();
    payload.notes.append(std::move(target));
  }
  return payload;
}

QByteArray encodeNotesPayload(const NotesPayload &payload, QString &error) {
  const QJsonDocument document(payload.toJson());
  QByteArray json = document.toJson(QJsonDocument::Compact);
  if (json.isEmpty()) {
    error = QStringLiteral("Could not serialize notes payload");
    return {};
  }
  if (static_cast<quint32>(json.size()) > kMaxPayloadBytes) {
    error = QStringLiteral("Notes payload exceeds maximum size");
    return {};
  }

  const quint32 uncompressedLength = static_cast<quint32>(json.size());
  const char *payloadBytes = json.constData();
  qsizetype payloadLength = json.size();
  quint8 compression = 0;
  QByteArray compressed;

  std::unique_ptr<libdeflate_compressor, decltype(&libdeflate_free_compressor)>
      compressor(libdeflate_alloc_compressor(6), libdeflate_free_compressor);
  if (compressor) {
    const qsizetype bound =
        static_cast<qsizetype>(libdeflate_zlib_compress_bound(
            compressor.get(), static_cast<size_t>(json.size())));
    compressed.resize(bound);
    const size_t compressedSize = libdeflate_zlib_compress(
        compressor.get(), json.constData(), static_cast<size_t>(json.size()),
        compressed.data(), static_cast<size_t>(compressed.size()));
    if (compressedSize != 0 &&
        compressedSize < static_cast<size_t>(json.size())) {
      payloadBytes = compressed.constData();
      payloadLength = static_cast<qsizetype>(compressedSize);
      compression = 1;
    }
  }

  QByteArray data;
  data.reserve(4 + payloadLength);
  char lengthBytes[4];
  qToBigEndian(uncompressedLength, lengthBytes);
  data.append(lengthBytes, 4);
  data.append(payloadBytes, payloadLength);

  const QByteArray header =
      packHeader(1, compression, static_cast<quint32>(data.size()), data);
  return header + data;
}

NotesCodecError decodeNotesPayload(const QByteArray &data,
                                   NotesPayload &payload, QString &error) {
  quint8 version = 0;
  quint8 compression = 0;
  quint32 dataLength = 0;
  quint32 storedCrc = 0;
  if (!unpackHeader(data, version, compression, dataLength, storedCrc)) {
    error = QStringLiteral("No omasnap-notes payload found");
    return NotesCodecError::NoPayload;
  }
  if (version != 1) {
    error = QStringLiteral("Unsupported omasnap-notes version %1").arg(version);
    return NotesCodecError::UnsupportedVersion;
  }
  // Compute in 64-bit: `kHeaderSize + dataLength` overflows unsigned int for a
  // hostile length near UINT32_MAX, which would otherwise slip past the bound.
  const qint64 declaredSize =
      static_cast<qint64>(kHeaderSize) + static_cast<qint64>(dataLength);
  if (dataLength > kMaxPayloadBytes + 4 || declaredSize > data.size()) {
    error = QStringLiteral("omasnap-notes payload length is out of bounds");
    return NotesCodecError::CorruptPayload;
  }
  const QByteArray payloadData = data.mid(kHeaderSize, dataLength);
  if (!verifyCrc(data.left(kHeaderSize), payloadData)) {
    error = QStringLiteral("omasnap-notes payload checksum failed");
    return NotesCodecError::CorruptPayload;
  }
  if (payloadData.size() < 4) {
    error = QStringLiteral("omasnap-notes payload is truncated");
    return NotesCodecError::CorruptPayload;
  }
  const quint32 uncompressedLength = qFromBigEndian<quint32>(
      reinterpret_cast<const uchar *>(payloadData.constData()));
  if (uncompressedLength > kMaxPayloadBytes ||
      uncompressedLength == 0) {
    error = QStringLiteral("omasnap-notes uncompressed length is invalid");
    return NotesCodecError::CorruptPayload;
  }

  QByteArray json;
  if (compression == 0) {
    if (uncompressedLength !=
        static_cast<quint32>(payloadData.size() - 4)) {
      error = QStringLiteral("omasnap-notes raw payload length mismatch");
      return NotesCodecError::CorruptPayload;
    }
    json = payloadData.mid(4);
  } else if (compression == 1) {
    json.resize(static_cast<qsizetype>(uncompressedLength));
    std::unique_ptr<libdeflate_decompressor,
                    decltype(&libdeflate_free_decompressor)>
        decompressor(libdeflate_alloc_decompressor(),
                     libdeflate_free_decompressor);
    if (!decompressor) {
      error = QStringLiteral("Could not allocate decompressor");
      return NotesCodecError::CorruptPayload;
    }
    size_t actualOut = 0;
    const libdeflate_result result = libdeflate_zlib_decompress(
        decompressor.get(), payloadData.constData() + 4,
        static_cast<size_t>(payloadData.size() - 4), json.data(),
        static_cast<size_t>(json.size()), &actualOut);
    if (result != LIBDEFLATE_SUCCESS ||
        actualOut != static_cast<size_t>(uncompressedLength)) {
      error = QStringLiteral("Could not decompress omasnap-notes payload");
      return NotesCodecError::CorruptPayload;
    }
  } else {
    error = QStringLiteral("omasnap-notes compression identifier is invalid");
    return NotesCodecError::CorruptPayload;
  }

  QJsonParseError parseError;
  const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
  if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
    error = QStringLiteral("Could not parse omasnap-notes JSON: %1")
                .arg(parseError.errorString());
    return NotesCodecError::CorruptPayload;
  }
  payload = NotesPayload::fromJson(document.object());
  if (payload.protocol != QStringLiteral("omasnap-notes/1")) {
    error = QStringLiteral("Unexpected omasnap-notes protocol: %1")
                .arg(payload.protocol);
    return NotesCodecError::CorruptPayload;
  }
  return NotesCodecError::None;
}

bool embedNotesInImage(QImage &image, const NotesPayload &payload,
                       QString &error) {
  if (image.isNull()) {
    error = QStringLiteral("Cannot embed notes in a null image");
    return false;
  }
  const QByteArray data = encodeNotesPayload(payload, error);
  if (data.isEmpty())
    return false;

  const qint64 width = image.width();
  const qint64 height = image.height();
  const qint64 capacityBits = width * height * 3;
  const qint64 neededBits = static_cast<qint64>(data.size()) * 8;
  if (neededBits > capacityBits) {
    error = QStringLiteral(
                "Image is too small to hold the notes payload: need %1 bits, "
                "have %2 bits")
                .arg(neededBits)
                .arg(capacityBits);
    return false;
  }

  // Only 32-bit straight formats may be written in place. Premultiplied and
  // packed (RGB888, etc.) formats are converted first: a PNG save unpremultiplies
  // and repacks, which would destroy the least-significant bits we write.
  const bool straight32 = image.format() == QImage::Format_ARGB32 ||
                          image.format() == QImage::Format_RGB32;
  QImage target =
      straight32 ? image : image.convertToFormat(QImage::Format_ARGB32);
  if (target.isNull()) {
    error = QStringLiteral("Could not convert image to ARGB32 for embedding");
    return false;
  }

  // Raster order, RGB within each pixel, most-significant bit first: three
  // payload bits per pixel, matching the width*height*3 capacity above.
  int bit = 0;
  for (int y = 0; y < target.height() && bit < neededBits; ++y) {
    QRgb *line = reinterpret_cast<QRgb *>(target.scanLine(y));
    for (int x = 0; x < target.width() && bit < neededBits; ++x) {
      QRgb pixel = line[x];
      for (int channel = 0; channel < 3 && bit < neededBits; ++channel, ++bit) {
        const int byteIndex = bit / 8;
        const int bitShift = 7 - (bit % 8);
        const int bitValue = (data[byteIndex] >> bitShift) & 1;
        const quint32 mask = channel == 0   ? 0x00010000u
                             : channel == 1 ? 0x00000100u
                                            : 0x00000001u;
        pixel = bitValue ? (pixel | mask) : (pixel & ~mask);
      }
      line[x] = pixel;
    }
  }
  image = std::move(target);
  return true;
}

NotesCodecError extractNotesFromImage(const QImage &image,
                                      NotesPayload &payload, QString &error) {
  if (image.isNull()) {
    error = QStringLiteral("Cannot extract notes from a null image");
    return NotesCodecError::NoPayload;
  }
  // Match embedNotesInImage: read straight 32-bit formats in place, convert
  // premultiplied and packed formats so the channel bits line up.
  const bool straight32 = image.format() == QImage::Format_ARGB32 ||
                          image.format() == QImage::Format_RGB32;
  QImage source =
      straight32 ? image : image.convertToFormat(QImage::Format_ARGB32);
  if (source.isNull()) {
    error = QStringLiteral("Could not convert image to ARGB32 for extraction");
    return NotesCodecError::NoPayload;
  }

  const qint64 width = source.width();
  const qint64 height = source.height();
  const qint64 capacityBits = width * height * 3;

  // Mirror embedNotesInImage exactly: raster order, RGB within each pixel,
  // most-significant bit first.
  auto readBytes = [&](qsizetype byteCount) -> QByteArray {
    QByteArray out(byteCount, '\0');
    const qint64 neededBits = static_cast<qint64>(byteCount) * 8;
    qint64 written = 0;
    for (int y = 0; y < source.height() && written < neededBits; ++y) {
      const QRgb *line = reinterpret_cast<const QRgb *>(source.scanLine(y));
      for (int x = 0; x < source.width() && written < neededBits; ++x) {
        const QRgb pixel = line[x];
        for (int channel = 0; channel < 3 && written < neededBits;
             ++channel, ++written) {
          const int byteIndex = static_cast<int>(written / 8);
          const int bitShift = 7 - static_cast<int>(written % 8);
          const int bit = (channel == 0   ? qRed(pixel)
                           : channel == 1 ? qGreen(pixel)
                                          : qBlue(pixel)) &
                          1;
          out[byteIndex] |= static_cast<char>(bit << bitShift);
        }
      }
    }
    return out;
  };

  QByteArray header = readBytes(kHeaderSize);
  if (header.size() < kHeaderSize) {
    error = QStringLiteral("Image is too small to contain an omasnap-notes header");
    return NotesCodecError::NoPayload;
  }

  quint8 version = 0;
  quint8 compression = 0;
  quint32 dataLength = 0;
  quint32 storedCrc = 0;
  if (!unpackHeader(header, version, compression, dataLength, storedCrc)) {
    error = QStringLiteral("No omasnap-notes payload found");
    return NotesCodecError::NoPayload;
  }
  if (version != 1) {
    error = QStringLiteral("Unsupported omasnap-notes version %1").arg(version);
    return NotesCodecError::UnsupportedVersion;
  }
  // 64-bit on purpose: a declared length near UINT32_MAX must be rejected, not
  // wrap into a small number that passes the capacity check.
  const qint64 fullSize =
      static_cast<qint64>(kHeaderSize) + static_cast<qint64>(dataLength);
  if (fullSize * 8 > capacityBits) {
    error = QStringLiteral("omasnap-notes payload length exceeds image capacity");
    return NotesCodecError::CorruptPayload;
  }

  QByteArray full = readBytes(static_cast<qsizetype>(fullSize));
  return decodeNotesPayload(full, payload, error);
}

QString notesCodecErrorString(NotesCodecError error) {
  switch (error) {
  case NotesCodecError::None:
    return QStringLiteral("ok");
  case NotesCodecError::NoPayload:
    return QStringLiteral("no payload");
  case NotesCodecError::UnsupportedVersion:
    return QStringLiteral("unsupported version");
  case NotesCodecError::CorruptPayload:
    return QStringLiteral("corrupt payload");
  case NotesCodecError::CapacityExceeded:
    return QStringLiteral("capacity exceeded");
  }
  return QStringLiteral("unknown");
}
