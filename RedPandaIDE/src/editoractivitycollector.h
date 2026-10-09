#ifndef EDITORACTIVITYCOLLECTOR_H
#define EDITORACTIVITYCOLLECTOR_H

#include "editoractivity.h"

#include <QObject>
#include <QSet>

class Editor;
class MainWindow;

class EditorActivityCollector: public QObject {
    Q_OBJECT
public:
    explicit EditorActivityCollector(MainWindow *mainWindow);

    void attachEditor(Editor *editor);
    void onEditorActivated(Editor *editor);
    void setBuilding(bool building);
    void setDebugging(bool debugging);

    EditorActivitySnapshot snapshot(Editor *editor) const;
    EditorActivitySnapshot currentActivity() const;

signals:
    // The snapshot can be retained; editor is only an event source/lifetime handle.
    void activityCollected(Editor *editor, const EditorActivitySnapshot &activity,
                           EditorActivityKind kind);

private:
    MainWindow *mMainWindow;
    bool mBuilding {false};
    bool mDebugging {false};
    QSet<Editor*> mAttachedEditors;
};

#endif // EDITORACTIVITYCOLLECTOR_H
