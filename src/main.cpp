#include "window.h"
#include <QApplication>
#include <QOperatingSystemVersion>
#include <QMessageBox>
#include <windows.h>
#include <QStandardPaths>
#include <QDir>
#include <QLockFile>
#include "runtime_lock.h"
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    RuntimeLock runtime;
    if (!runtime.valid())
        return 1;
    app.setOrganizationName("Baawiil");
    app.setApplicationName("Monitor");
    auto data = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QDir().mkpath(data);
    QLockFile lock(data + "/application.lock");
    if (!lock.tryLock(0)) {
        QMessageBox::warning(
            nullptr, "Baawiil Monitor",
            "Une autre instance est active ou le dossier de données est inaccessible.");
        return 1;
    }
    qRegisterMetaType<Snapshot>();
    QObject::connect(&app, &QCoreApplication::aboutToQuit, [] { features::shutdownWorkers(); });
    // Windows 11 retains major version 10; check the actual OS build.
    using RtlGetVersionFn = LONG(WINAPI *)(OSVERSIONINFOW *);
    auto versionFn = reinterpret_cast<RtlGetVersionFn>(
        GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion"));
    OSVERSIONINFOEXW version{};
    version.dwOSVersionInfoSize = sizeof(version);
    if (!versionFn || versionFn(reinterpret_cast<OSVERSIONINFOW *>(&version)) != 0 ||
        version.dwMajorVersion != 10 || version.dwBuildNumber < 22000 ||
        version.wProductType != VER_NT_WORKSTATION) {
        QMessageBox::critical(nullptr, "Windows 11 requis",
                              "Cette application nécessite Windows 11 x64.");
        return 1;
    }
    Window window;
    window.show();
    return app.exec();
}
