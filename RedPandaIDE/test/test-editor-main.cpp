#include <QTest>
#include <QGuiApplication>
#include "test_editor_autosave.h"
#include "test_editor_symbol_completion.h"
#include "test_discordpresence_widget.h"

int main(int argc, char *argv[]) {
    int status = 0;
    QTest::setMainSourcePath(__FILE__, QT_TESTCASE_BUILDDIR); // Optional: for source path resolution

    QApplication app(argc,argv);
    {
        TestDiscordPresenceWidget tc;
        status |= QTest::qExec(&tc, argc, argv);
    }
    //CharPos Test
    {
        TestEditorSymbolCompletion tc;
        status |= QTest::qExec(&tc, argc, argv);
    }
    {
        TestEditorAutoSave tc;
        status |= QTest::qExec(&tc, argc, argv);
    }

    return status;
}
