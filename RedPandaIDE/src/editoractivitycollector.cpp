#include "editoractivitycollector.h"

#include "editor.h"
#include "editormanager.h"
#include "mainwindow.h"
#include "project.h"

#include <QApplication>
#include <QDateTime>
#include <QEvent>
#include <QTimer>

EditorActivityCollector::EditorActivityCollector(MainWindow *mainWindow):
    QObject(mainWindow),
    mMainWindow(mainWindow)
{
    qApp->installEventFilter(this);
    connect(qApp, &QApplication::applicationStateChanged, this, [this] {
        notifyContextChanged();
    });
}

void EditorActivityCollector::notify(EditorActivityKind kind)
{
    Editor *editor = mMainWindow->editorManager()->getEditor();
    emit activityCollected(editor, snapshot(editor), kind);
}

void EditorActivityCollector::notifyContextChanged()
{
    notify(EditorActivityKind::ContextChanged);
}

bool EditorActivityCollector::eventFilter(QObject *watched, QEvent *event)
{
    QWidget *widget = qobject_cast<QWidget *>(watched);
    if (event->spontaneous() && widget
        && (widget == mMainWindow || mMainWindow->isAncestorOf(widget))) {
        if (event->type() == QEvent::KeyPress || event->type() == QEvent::MouseButtonPress
            || event->type() == QEvent::Wheel)
            notify(EditorActivityKind::UserInteraction);
    }
    return QObject::eventFilter(watched, event);
}

void EditorActivityCollector::attachEditor(Editor *editor)
{
    if (!editor || mAttachedEditors.contains(editor)) {
        return;
    }

    mAttachedEditors.insert(editor);
    connect(editor, &QObject::destroyed, this, [this, editor] {
        mAttachedEditors.remove(editor);
    });
    connect(editor, &Editor::statusChanged, this,
            [this, editor](QSynedit::StatusChanges changes) {
        if (isEditorActivity(changes)) {
            emit activityCollected(editor, snapshot(editor), EditorActivityKind::Interaction);
        }
    });
    connect(editor, &Editor::fileSaved, this, [this](Editor *editor, const QString &) {
        emit activityCollected(editor, snapshot(editor), EditorActivityKind::FileSaved);
    });
    connect(editor, &Editor::focusInOccured, this, [this] { notifyContextChanged(); });
    connect(editor, &Editor::closeOccured, this, [this] {
        QTimer::singleShot(0, this, &EditorActivityCollector::notifyContextChanged);
    });
}

void EditorActivityCollector::onEditorActivated(Editor *editor)
{
    emit activityCollected(editor, snapshot(editor), EditorActivityKind::EditorActivated);
}

void EditorActivityCollector::setBuilding(bool building)
{
    if (mBuilding == building) {
        return;
    }
    mBuilding = building;
    Editor *editor = mMainWindow->editorManager()->getEditor();
    emit activityCollected(editor, snapshot(editor), EditorActivityKind::BuildStateChanged);
}

void EditorActivityCollector::setDebugging(bool debugging)
{
    if (mDebugging == debugging) {
        return;
    }
    mDebugging = debugging;
    Editor *editor = mMainWindow->editorManager()->getEditor();
    emit activityCollected(editor, snapshot(editor), EditorActivityKind::DebugStateChanged);
}

EditorActivitySnapshot EditorActivityCollector::snapshot(Editor *editor) const
{
    EditorActivitySnapshot activity;
    activity.timeMs = QDateTime::currentMSecsSinceEpoch();
    activity.applicationActive = QApplication::applicationState() == Qt::ApplicationActive;
    activity.restoringFiles = mMainWindow->openingFiles() || mMainWindow->openingProject();
    activity.building = mBuilding;
    activity.debugging = mDebugging;
    if (!editor) {
        const std::shared_ptr<Project> project = mMainWindow->project();
        if (project) {
            activity.projectName = project->name();
            activity.projectFolder = project->directory();
        }
        return activity;
    }

    activity.hasEditor = true;
    activity.filename = editor->filename();
    activity.language = fileTypeToName(editor->fileType());
    activity.isNew = editor->isNew();
    activity.editorFocused = editor->hasFocus();
    activity.lineNumber = editor->caretY() + 1;
    activity.cursorPosition = editor->caretX() + 1;
    activity.linesInFile = editor->lineCount();

    if (editor->inProject()) {
        const std::shared_ptr<Project> project = mMainWindow->project();
        if (project && project->inProject(activity.filename)) {
            activity.projectName = project->name();
            activity.projectFolder = project->directory();
        }
    }
    return activity;
}

EditorActivitySnapshot EditorActivityCollector::currentActivity() const
{
    return snapshot(mMainWindow->editorManager()->getEditor());
}
