/** @fileoverview omasnap-notes/1 steganographic codec for marker directions. */
#pragma once

#include <QImage>
#include <QJsonArray>
#include <QJsonObject>
#include <QPointF>
#include <QSize>
#include <QString>
#include <QVector>

struct NoteTarget {
  QString id;
  int number = 0;
  QPointF position; // normalized to [0,1] over the exported image
  QString instruction;
  bool operator==(const NoteTarget &) const = default;
};

struct NotesPayload {
  QString protocol = QStringLiteral("omasnap-notes/1");
  QSize imageSize;
  QVector<NoteTarget> notes;

  [[nodiscard]] QJsonObject toJson() const;
  [[nodiscard]] static NotesPayload fromJson(const QJsonObject &object);
};

enum class NotesCodecError {
  None,
  NoPayload,
  UnsupportedVersion,
  CorruptPayload,
  CapacityExceeded
};

/** Compress and wrap a payload with the binary header. */
[[nodiscard]] QByteArray encodeNotesPayload(const NotesPayload &payload,
                                            QString &error);

/** Decode a header+payload byte stream. */
[[nodiscard]] NotesCodecError decodeNotesPayload(const QByteArray &data,
                                                 NotesPayload &payload,
                                                 QString &error);

/** Embed a payload into the RGB LSBs of `image`. Alpha is preserved. */
[[nodiscard]] bool embedNotesInImage(QImage &image, const NotesPayload &payload,
                                     QString &error);

/** Extract a payload from the RGB LSBs of `image`. */
[[nodiscard]] NotesCodecError extractNotesFromImage(const QImage &image,
                                                    NotesPayload &payload,
                                                    QString &error);

/** Human-readable status for a decode result. */
[[nodiscard]] QString notesCodecErrorString(NotesCodecError error);
