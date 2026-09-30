#include "notes/notes_codec.hpp"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDebug>
#include <QImage>
#include <QJsonDocument>
#include <QStringList>
#include <QTextStream>

#include <cstdlib>

namespace {
[[nodiscard]] int exitCodeForError(NotesCodecError error) {
  switch (error) {
  case NotesCodecError::None:
    return 0;
  case NotesCodecError::NoPayload:
    return 1;
  case NotesCodecError::UnsupportedVersion:
    return 2;
  case NotesCodecError::CorruptPayload:
    return 3;
  case NotesCodecError::CapacityExceeded:
    return 4;
  }
  return 3;
}

void printNote(const NoteTarget &note, QTextStream &out) {
  out << QStringLiteral("#%1 [%2, %3]\n%4\n\n")
             .arg(note.number)
             .arg(note.position.x(), 0, 'f', 4)
             .arg(note.position.y(), 0, 'f', 4)
             .arg(note.instruction);
}
} // namespace

int main(int argc, char **argv) {
  QCoreApplication app(argc, argv);
  QCoreApplication::setApplicationName(QStringLiteral("omasnap-notes"));

  QCommandLineParser parser;
  parser.setApplicationDescription(
      QStringLiteral("Inspect and decode omasnap-notes/1 payloads from PNGs."));
  parser.addHelpOption();
  parser.addPositionalArgument(
      QStringLiteral("command"),
      QStringLiteral("Command: inspect | decode"));
  parser.addPositionalArgument(QStringLiteral("image"),
                               QStringLiteral("PNG image file"));
  QCommandLineOption jsonOption(QStringList{QStringLiteral("json")},
                                QStringLiteral("Print decoded payload as JSON"));
  parser.addOption(jsonOption);
  parser.process(app);

  const QStringList args = parser.positionalArguments();
  if (args.size() < 2)
    parser.showHelp(2);

  const QString command = args.at(0);
  const QString imagePath = args.at(1);

  const QImage image(imagePath);
  if (image.isNull()) {
    qCritical().noquote()
        << QStringLiteral("Could not load image: %1").arg(imagePath);
    return 1;
  }

  NotesPayload payload;
  QString error;
  const NotesCodecError result = extractNotesFromImage(image, payload, error);

  QTextStream out(stdout);
  if (command == QStringLiteral("inspect")) {
    if (result == NotesCodecError::None) {
      out << QStringLiteral("omasnap-notes/1 payload found\n");
      out << QStringLiteral("image: %1x%2\n")
                 .arg(payload.imageSize.width())
                 .arg(payload.imageSize.height());
      out << QStringLiteral("notes: %1\n").arg(payload.notes.size());
      for (const NoteTarget &note : payload.notes) {
        out << QStringLiteral("  #%1 [%2, %3]\n")
                   .arg(note.number)
                   .arg(note.position.x(), 0, 'f', 4)
                   .arg(note.position.y(), 0, 'f', 4);
      }
      return 0;
    }
    qWarning().noquote() << error;
    return exitCodeForError(result);
  }

  if (command == QStringLiteral("decode")) {
    if (result != NotesCodecError::None) {
      qCritical().noquote() << error;
      return exitCodeForError(result);
    }
    if (parser.isSet(jsonOption)) {
      out << QString::fromUtf8(
          QJsonDocument(payload.toJson()).toJson(QJsonDocument::Compact));
      out << '\n';
    } else {
      out << QStringLiteral("omasnap-notes/1 (%1 notes)\n")
                 .arg(payload.notes.size());
      for (const NoteTarget &note : payload.notes)
        printNote(note, out);
    }
    return 0;
  }

  parser.showHelp(2);
}
