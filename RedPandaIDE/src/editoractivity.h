#ifndef EDITORACTIVITY_H
#define EDITORACTIVITY_H

#include <QFlags>
#include <QString>

#include "qsynedit/types.h"

enum class EditorActivityKind {
    Interaction,
    FileSaved,
    EditorActivated,
    BuildStateChanged,
    DebugStateChanged,
};

struct EditorActivitySnapshot {
    QString filename;
    QString language;
    QString projectName;
    QString projectFolder;
    qint64 timeMs {0};
    int lineNumber {0};
    int cursorPosition {0};
    int linesInFile {0};
    bool hasEditor {false};
    bool isNew {false};
    bool applicationActive {false};
    bool editorFocused {false};
    bool restoringFiles {false};
    bool building {false};
    bool debugging {false};
};

inline bool isEditorActivity(QFlags<QSynedit::StatusChange> changes)
{
    return changes.testFlag(QSynedit::StatusChange::CaretX)
        || changes.testFlag(QSynedit::StatusChange::CaretY)
        || changes.testFlag(QSynedit::StatusChange::Selection)
        || changes.testFlag(QSynedit::StatusChange::Modified);
}

#endif // EDITORACTIVITY_H
