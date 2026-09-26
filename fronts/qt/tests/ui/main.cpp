#include <QApplication>
#include <QStandardPaths>
#include <QTest>

#include <cstring>
#include <memory>

#include "FakeBackend.h"

namespace Tests {
QObject* makeWindowHostTest();
}

// The window's tests: a QApplication, run offscreen (see CMakeLists.txt's
// add_test()). Like cloudmus-qt-tests, this binary doubles as the fake
// backend the tests start.
int main(int argc, char** argv)
{
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], Tests::kFakeBackendArgument) == 0)
            return Tests::runFakeBackend();
    }

    QApplication app(argc, argv);
    // Anything a test saves goes to throwaway locations, not the user's.
    QStandardPaths::setTestModeEnabled(true);
    int failures = 0;
    for (auto make : { Tests::makeWindowHostTest }) {
        std::unique_ptr<QObject> test(make());
        failures += QTest::qExec(test.get(), argc, argv);
    }
    return failures == 0 ? 0 : 1;
}
