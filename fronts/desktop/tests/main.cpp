#include <QCoreApplication>
#include <QStandardPaths>
#include <QTest>

#include <cstring>
#include <memory>

#include "FakeBackend.h"

namespace Tests {
QObject* makeBrowseTest();
QObject* makeCrashReporterTest();
QObject* makeHotkeysTest();
QObject* makeSingleInstanceTest();
QObject* makeStarPromptTest();
QObject* makeUpdateTest();
int runCrashReporterChildIfAsked(int argc, char** argv);
}

int main(int argc, char** argv)
{
    // The same binary doubles as the backend the tests start — see
    // FakeBackend.h.
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], Tests::kFakeBackendArgument) == 0)
            return Tests::runFakeBackend();
    }

    // ...and as the crashing program CrashReporterTest needs.
    if (const int code = Tests::runCrashReporterChildIfAsked(argc, argv); code >= 0)
        return code;

    QCoreApplication app(argc, argv);
    // Anything a test saves goes to throwaway locations, not the user's.
    QStandardPaths::setTestModeEnabled(true);
    int failures = 0;
    for (auto make : { Tests::makeBrowseTest,
             Tests::makeCrashReporterTest,
             Tests::makeHotkeysTest,
             Tests::makeSingleInstanceTest,
             Tests::makeStarPromptTest,
             Tests::makeUpdateTest }) {
        std::unique_ptr<QObject> test(make());
        failures += QTest::qExec(test.get(), argc, argv);
    }
    return failures == 0 ? 0 : 1;
}
