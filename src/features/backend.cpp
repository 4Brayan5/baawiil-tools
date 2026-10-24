#include "backend.h"
#include "../core.h"
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>
#include <netioapi.h>
#include <tlhelp32.h>
#include <wintrust.h>
#include <softpub.h>
#include <wincrypt.h>
#include <winver.h>
#include <pdh.h>
#include <pdhmsg.h>
#include <shellapi.h>
#include <shlobj.h>
#include <QStandardPaths>
#include <QCryptographicHash>
#include <QCoreApplication>
#include <QProcess>
#include <QJsonDocument>
#include <QDirIterator>
#include <QFileInfo>
#include <QFile>
#include <QTemporaryFile>
#include <QStorageInfo>
#include <QElapsedTimer>
#include <QDateTime>
#include <QRegularExpression>
#include <algorithm>
#include <thread>
#include <vector>
#include <random>
#include <numeric>
#include <chrono>
#include <mutex>
#include <map>
#include <QTemporaryDir>

namespace features {
namespace {
std::atomic_bool stopping{false};
}
void shutdownWorkers() {
    stopping.store(true);
}
Result isolatedTask(const QString &task, const QJsonObject &parameters,
                    const std::shared_ptr<std::atomic_bool> &cancel) {
    const QString executable = QCoreApplication::applicationDirPath() + "/baawiil-worker.exe";
    if (!QFileInfo::exists(executable))
        return {{}, "Composant de collecte isolé absent : " + executable};
    auto payload = parameters;
    std::unique_ptr<QTemporaryDir> temporary;
    if (task == "benchmark" && parameters["kind"].toString().startsWith("disk")) {
        temporary = std::make_unique<QTemporaryDir>(
            QDir(parameters["directory"].toString()).filePath("baawiil-benchmark-job-XXXXXX"));
        if (!temporary->isValid())
            return {{}, "Impossible de préparer le dossier temporaire dédié."};
        payload["directory"] = temporary->path();
    }
    QProcess process;
    process.setCreateProcessArgumentsModifier(
        [](QProcess::CreateProcessArguments *args) { args->flags |= CREATE_NO_WINDOW; });
    process.start(executable, {"--request"});
    if (!process.waitForStarted(5000))
        return {{}, process.errorString()};
    auto bytes = QJsonDocument(QJsonObject{{"task", task}, {"parameters", payload}})
                     .toJson(QJsonDocument::Compact);
    if (process.write(bytes) != bytes.size()) {
        process.kill();
        process.waitForFinished();
        return {{}, "Requête du collecteur non transmise."};
    }
    process.closeWriteChannel();
    QElapsedTimer elapsed;
    elapsed.start();
    QByteArray output;
    const int timeout = task == "scan" ? 1800000 : task == "benchmark" ? 120000 : 30000;
    while (process.state() != QProcess::NotRunning) {
        process.waitForFinished(100);
        output += process.readAllStandardOutput();
        if (cancel->load() || stopping.load() || elapsed.elapsed() > timeout ||
            output.size() > 16 * 1024 * 1024) {
            process.kill();
            process.waitForFinished();
            return {{},
                    cancel->load() ? "Opération annulée. Le processus de travail a été arrêté."
                                   : "Collecteur arrêté : délai ou volume de résultat dépassé."};
        }
    }
    output += process.readAllStandardOutput();
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
        return {{},
                "Le collecteur isolé a échoué : " +
                    QString::fromUtf8(process.readAllStandardError()).left(2000)};
    QJsonParseError error;
    auto doc = QJsonDocument::fromJson(output, &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject())
        return {{}, "Réponse du collecteur isolé invalide."};
    return {doc.object()["rows"].toArray(), doc.object()["error"].toString()};
}
QString launchInstaller(const QString &path, const QString &signerSha256) {
    HANDLE lock =
        CreateFileW(reinterpret_cast<const wchar_t *>(path.utf16()), GENERIC_READ, FILE_SHARE_READ,
                    nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (lock == INVALID_HANDLE_VALUE)
        return "Installateur inaccessible ou déjà utilisé.";
    auto error = verifyInstaller(path, signerSha256);
    if (!error.isEmpty()) {
        CloseHandle(lock);
        return error;
    }
    SHELLEXECUTEINFOW request{};
    request.cbSize = sizeof(request);
    request.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_NO_UI;
    request.lpVerb = L"open";
    request.lpFile = reinterpret_cast<const wchar_t *>(path.utf16());
    request.nShow = SW_SHOWNORMAL;
    BOOL launched = ShellExecuteExW(&request);
    DWORD code = GetLastError();
    if (request.hProcess)
        CloseHandle(request.hProcess);
    CloseHandle(lock);
    return launched ? QString() : QString("Installateur non lancé, code Windows %1.").arg(code);
}
QString verifyInstaller(const QString &path, const QString &signerSha256) {
    if (!QRegularExpression("^[a-fA-F0-9]{64}$").match(signerSha256).hasMatch())
        return "Empreinte SHA-256 de certificat invalide.";
    HANDLE lock =
        CreateFileW(reinterpret_cast<const wchar_t *>(path.utf16()), GENERIC_READ, FILE_SHARE_READ,
                    nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (lock == INVALID_HANDLE_VALUE)
        return "Impossible d'ouvrir l'installateur en lecture protégée.";
    WINTRUST_FILE_INFO file{};
    file.cbStruct = sizeof(file);
    file.pcwszFilePath = reinterpret_cast<const wchar_t *>(path.utf16());
    file.hFile = lock;
    WINTRUST_DATA trust{};
    trust.cbStruct = sizeof(trust);
    trust.dwUIChoice = WTD_UI_NONE;
    trust.fdwRevocationChecks = WTD_REVOKE_NONE;
    trust.dwUnionChoice = WTD_CHOICE_FILE;
    trust.pFile = &file;
    trust.dwStateAction = WTD_STATEACTION_VERIFY;
    trust.dwProvFlags = WTD_CACHE_ONLY_URL_RETRIEVAL;
    GUID policy = WINTRUST_ACTION_GENERIC_VERIFY_V2;
    LONG status = WinVerifyTrust(nullptr, &policy, &trust);
    trust.dwStateAction = WTD_STATEACTION_CLOSE;
    WinVerifyTrust(nullptr, &policy, &trust);
    if (status != ERROR_SUCCESS) {
        CloseHandle(lock);
        return "Signature Authenticode invalide ou non vérifiable avec la chaîne locale.";
    }
    HCERTSTORE store = nullptr;
    HCRYPTMSG message = nullptr;
    if (!CryptQueryObject(CERT_QUERY_OBJECT_FILE, path.utf16(),
                          CERT_QUERY_CONTENT_FLAG_PKCS7_SIGNED_EMBED, CERT_QUERY_FORMAT_FLAG_BINARY,
                          0, nullptr, nullptr, nullptr, &store, &message, nullptr)) {
        CloseHandle(lock);
        return "Signature embarquée illisible.";
    }
    DWORD bytes = 0;
    CryptMsgGetParam(message, CMSG_SIGNER_INFO_PARAM, 0, nullptr, &bytes);
    std::vector<BYTE> data(bytes);
    QString error = "Certificat signataire non identifiable.";
    if (bytes && CryptMsgGetParam(message, CMSG_SIGNER_INFO_PARAM, 0, data.data(), &bytes)) {
        auto *signer = reinterpret_cast<CMSG_SIGNER_INFO *>(data.data());
        CERT_INFO identity{};
        identity.Issuer = signer->Issuer;
        identity.SerialNumber = signer->SerialNumber;
        auto cert = CertFindCertificateInStore(store, X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, 0,
                                               CERT_FIND_SUBJECT_CERT, &identity, nullptr);
        if (cert) {
            BYTE hash[32]{};
            DWORD count = 32;
            if (CertGetCertificateContextProperty(cert, CERT_SHA256_HASH_PROP_ID, hash, &count) &&
                count == 32) {
                auto actual = QByteArray(reinterpret_cast<char *>(hash), 32).toHex();
                error = actual == signerSha256.toLatin1().toLower()
                            ? QString()
                            : "Le certificat signataire ne correspond pas à l'empreinte épinglée.";
            }
            CertFreeCertificateContext(cert);
        }
    }
    CryptMsgClose(message);
    CertCloseStore(store, 0);
    CloseHandle(lock);
    return error;
}
QString elevatedAction(const QStringList &arguments) {
    for (const auto &argument : arguments)
        if (!QRegularExpression("^[A-Za-z0-9_-]{1,1024}$").match(argument).hasMatch())
            return "Argument d'élévation invalide.";
    const QString executable = QCoreApplication::applicationDirPath() + "/baawiil-admin.exe";
    if (!QFileInfo::exists(executable))
        return "Composant administrateur absent.";
    const auto parameters = arguments.join(' ');
    SHELLEXECUTEINFOW request{};
    request.cbSize = sizeof(request);
    request.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_NO_UI;
    request.lpVerb = L"runas";
    request.lpFile = reinterpret_cast<const wchar_t *>(executable.utf16());
    request.lpParameters = reinterpret_cast<const wchar_t *>(parameters.utf16());
    request.nShow = SW_SHOWNORMAL;
    if (!ShellExecuteExW(&request))
        return GetLastError() == ERROR_CANCELLED
                   ? "Élévation annulée."
                   : QString("Échec de l'élévation, code %1").arg(GetLastError());
    if (!request.hProcess)
        return "Aucun résultat du composant administrateur.";
    // Never assume completion from successful launch. The helper confirms its own exact operation.
    DWORD wait = WAIT_TIMEOUT, exitCode = 1;
    QElapsedTimer elapsed;
    elapsed.start();
    while (!stopping.load() && elapsed.elapsed() < 300000) {
        wait = WaitForSingleObject(request.hProcess, 100);
        if (wait != WAIT_TIMEOUT)
            break;
    }
    if (wait == WAIT_OBJECT_0)
        GetExitCodeProcess(request.hProcess, &exitCode);
    CloseHandle(request.hProcess);
    return wait != WAIT_OBJECT_0 ? "Composant encore actif ou état inconnu après cinq minutes."
           : exitCode == 0
               ? QString()
               : "Opération administrateur annulée ou échouée ; consulter le message du composant.";
}
namespace {
QString winError(DWORD code = GetLastError()) {
    wchar_t *message = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                       FORMAT_MESSAGE_IGNORE_INSERTS,
                   nullptr, code, 0, reinterpret_cast<wchar_t *>(&message), 0, nullptr);
    QString result = message ? QString::fromWCharArray(message).trimmed()
                             : QString("Erreur Windows %1").arg(code);
    if (message)
        LocalFree(message);
    return result;
}
QString powershell() {
    wchar_t system[MAX_PATH]{};
    GetSystemDirectoryW(system, MAX_PATH);
    return QString::fromWCharArray(system) + "/WindowsPowerShell/v1.0/powershell.exe";
}
Result cim(const QString &script) {
    QProcess process;
    process.start(powershell(), {"-NoLogo", "-NoProfile", "-NonInteractive", "-Command",
                                 "[Console]::OutputEncoding=[System.Text.UTF8Encoding]::new(); "
                                 "$ErrorActionPreference='Stop'; " +
                                     script + " | ConvertTo-Json -Depth 5 -Compress"});
    if (!process.waitForStarted(5000))
        return {{}, process.errorString()};
    if (!process.waitForFinished(20000)) {
        process.kill();
        process.waitForFinished();
        return {{}, "Inventaire interrompu après 20 secondes."};
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
        return {{}, QString::fromUtf8(process.readAllStandardError()).left(4000)};
    auto bytes = process.readAllStandardOutput();
    if (bytes.trimmed().isEmpty())
        return {{}, "Aucune information disponible."};
    QJsonParseError error;
    auto doc = QJsonDocument::fromJson(bytes, &error);
    if (error.error != QJsonParseError::NoError)
        return {{}, "Réponse d’inventaire non valide : " + error.errorString()};
    return {doc.isArray() ? doc.array() : QJsonArray{doc.object()}, {}};
}
quint64 ft(FILETIME t) {
    return quint64(t.dwHighDateTime) << 32 | t.dwLowDateTime;
}
struct Handle {
    HANDLE h = nullptr;
    ~Handle() {
        if (h && h != INVALID_HANDLE_VALUE)
            CloseHandle(h);
    }
};
struct Service {
    SC_HANDLE h = nullptr;
    ~Service() {
        if (h)
            CloseServiceHandle(h);
    }
};
QString regString(HKEY key, const wchar_t *name) {
    DWORD type = 0, bytes = 0;
    if (RegQueryValueExW(key, name, nullptr, &type, nullptr, &bytes) != ERROR_SUCCESS ||
        (type != REG_SZ && type != REG_EXPAND_SZ) || bytes > 1048576)
        return {};
    std::vector<wchar_t> data(bytes / sizeof(wchar_t) + 1, 0);
    if (RegQueryValueExW(key, name, nullptr, &type, reinterpret_cast<BYTE *>(data.data()),
                         &bytes) != ERROR_SUCCESS)
        return {};
    return QString::fromWCharArray(data.data());
}
Result startup() {
    Result result;
    for (const auto &pair : {std::pair{HKEY_CURRENT_USER, QString("Utilisateur")},
                             std::pair{HKEY_LOCAL_MACHINE, QString("Machine")}}) {
        for (bool disabled : {false, true}) {
            if (disabled && pair.first != HKEY_CURRENT_USER)
                continue;
            HKEY key = nullptr;
            const auto path = disabled ? L"Software\\Baawiil\\DisabledRun"
                                       : L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
            if (RegOpenKeyExW(pair.first, path, 0, KEY_READ, &key) != ERROR_SUCCESS)
                continue;
            for (DWORD i = 0;; ++i) {
                wchar_t name[16384];
                DWORD size = 16384;
                LONG status =
                    RegEnumValueW(key, i, name, &size, nullptr, nullptr, nullptr, nullptr);
                if (status == ERROR_NO_MORE_ITEMS)
                    break;
                if (status != ERROR_SUCCESS) {
                    result.error = "Inventaire démarrage partiel.";
                    break;
                }
                result.rows.append(QJsonObject{{"Nom", QString::fromWCharArray(name, int(size))},
                                               {"Commande", regString(key, name)},
                                               {"Portée", pair.second},
                                               {"Activé", !disabled},
                                               {"Origine", "Registre Run"}});
            }
            RegCloseKey(key);
        }
    }
    for (bool common : {false, true}) {
        wchar_t path[MAX_PATH]{};
        if (SUCCEEDED(SHGetFolderPathW(nullptr, common ? CSIDL_COMMON_STARTUP : CSIDL_STARTUP,
                                       nullptr, SHGFP_TYPE_CURRENT, path))) {
            for (const auto &file :
                 QDir(QString::fromWCharArray(path))
                     .entryInfoList(QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot))
                result.rows.append(
                    QJsonObject{{"Nom", file.fileName()},
                                {"Chemin", file.absoluteFilePath()},
                                {"Portée", common ? "Machine" : "Utilisateur"},
                                {"Origine", common ? "Dossier machine" : "Dossier utilisateur"},
                                {"Activé", true}});
        }
    }
    auto backup = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) +
                  "/disabled-startup";
    for (const auto &file : QDir(backup).entryInfoList({"*.json"}, QDir::Files)) {
        QFile metadata(file.absoluteFilePath());
        if (!metadata.open(QIODevice::ReadOnly) || metadata.size() > 4096)
            continue;
        auto obj = QJsonDocument::fromJson(metadata.readAll()).object();
        auto data = QDir(backup).filePath(file.completeBaseName() + ".bin");
        if (QFileInfo::exists(data))
            result.rows.append(QJsonObject{{"Nom", obj["name"].toString()},
                                           {"Chemin", data},
                                           {"Portée", "Utilisateur"},
                                           {"Origine", "Dossier utilisateur"},
                                           {"Activé", false}});
    }
    result.error = "Sources : clés Run et dossiers de démarrage utilisateur/machine. Les entrées "
                   "Run présentes peuvent être bloquées séparément par Windows. Tâches planifiées "
                   "et StartupTask des applications empaquetées non modifiables ici.";
    return result;
}
Result services() {
    Service manager{OpenSCManagerW(nullptr, nullptr, SC_MANAGER_ENUMERATE_SERVICE)};
    if (!manager.h)
        return {{}, winError()};
    DWORD bytes = 0, count = 0, resume = 0;
    EnumServicesStatusExW(manager.h, SC_ENUM_PROCESS_INFO, SERVICE_WIN32, SERVICE_STATE_ALL,
                          nullptr, 0, &bytes, &count, &resume, nullptr);
    std::vector<BYTE> buffer(bytes);
    resume = 0;
    if (!EnumServicesStatusExW(manager.h, SC_ENUM_PROCESS_INFO, SERVICE_WIN32, SERVICE_STATE_ALL,
                               buffer.data(), DWORD(buffer.size()), &bytes, &count, &resume,
                               nullptr))
        return {{}, winError()};
    Result result;
    auto *rows = reinterpret_cast<ENUM_SERVICE_STATUS_PROCESSW *>(buffer.data());
    for (DWORD i = 0; i < count; ++i) {
        const auto &row = rows[i];
        QString state;
        switch (row.ServiceStatusProcess.dwCurrentState) {
        case SERVICE_RUNNING:
            state = "En cours";
            break;
        case SERVICE_STOPPED:
            state = "Arrêté";
            break;
        default:
            state = "Transition / autre";
        }
        QJsonObject item{{"Nom", QString::fromWCharArray(row.lpServiceName)},
                         {"Description", QString::fromWCharArray(row.lpDisplayName)},
                         {"État", state},
                         {"PID", double(row.ServiceStatusProcess.dwProcessId)}};
        Service service{OpenServiceW(manager.h, row.lpServiceName, SERVICE_QUERY_CONFIG)};
        if (service.h) {
            DWORD needed = 0;
            QueryServiceConfigW(service.h, nullptr, 0, &needed);
            std::vector<BYTE> config(needed);
            if (needed && QueryServiceConfigW(
                              service.h, reinterpret_cast<QUERY_SERVICE_CONFIGW *>(config.data()),
                              needed, &needed)) {
                auto *value = reinterpret_cast<QUERY_SERVICE_CONFIGW *>(config.data());
                item["Démarrage"] = int(value->dwStartType);
                item["Exécutable"] = QString::fromWCharArray(value->lpBinaryPathName);
                QStringList deps;
                for (auto *p = value->lpDependencies; p && *p; p += wcslen(p) + 1)
                    deps.append(QString::fromWCharArray(p));
                item["Dépendances"] = deps.join(", ");
            }
        }
        result.rows.append(item);
    }
    return result;
}
QString ipv4(DWORD address) {
    IN_ADDR value{};
    value.S_un.S_addr = address;
    wchar_t text[64]{};
    return InetNtopW(AF_INET, &value, text, 64) ? QString::fromWCharArray(text) : QString();
}
Result connections() {
    Result result;
    DWORD bytes = 0;
    GetExtendedTcpTable(nullptr, &bytes, FALSE, AF_INET, TCP_TABLE_OWNER_PID_ALL, 0);
    std::vector<BYTE> buffer(bytes);
    if (GetExtendedTcpTable(buffer.data(), &bytes, FALSE, AF_INET, TCP_TABLE_OWNER_PID_ALL, 0) ==
        NO_ERROR) {
        auto *table = reinterpret_cast<MIB_TCPTABLE_OWNER_PID *>(buffer.data());
        for (DWORD i = 0; i < table->dwNumEntries; ++i) {
            auto &r = table->table[i];
            result.rows.append(QJsonObject{{"Protocole", "TCP IPv4"},
                                           {"Locale", ipv4(r.dwLocalAddr)},
                                           {"Port local", int(ntohs(USHORT(r.dwLocalPort)))},
                                           {"Distante", ipv4(r.dwRemoteAddr)},
                                           {"Port distant", int(ntohs(USHORT(r.dwRemotePort)))},
                                           {"État", int(r.dwState)},
                                           {"PID", double(r.dwOwningPid)}});
        }
    } else
        result.error = "Lecture TCP IPv4 impossible.";
    bytes = 0;
    GetExtendedUdpTable(nullptr, &bytes, FALSE, AF_INET, UDP_TABLE_OWNER_PID, 0);
    buffer.resize(bytes);
    if (GetExtendedUdpTable(buffer.data(), &bytes, FALSE, AF_INET, UDP_TABLE_OWNER_PID, 0) ==
        NO_ERROR) {
        auto *table = reinterpret_cast<MIB_UDPTABLE_OWNER_PID *>(buffer.data());
        for (DWORD i = 0; i < table->dwNumEntries; ++i) {
            auto &r = table->table[i];
            result.rows.append(QJsonObject{{"Protocole", "UDP IPv4"},
                                           {"Locale", ipv4(r.dwLocalAddr)},
                                           {"Port local", int(ntohs(USHORT(r.dwLocalPort)))},
                                           {"PID", double(r.dwOwningPid)}});
        }
    }
    auto ipv6 = [](const UCHAR *address) {
        wchar_t text[128]{};
        return InetNtopW(AF_INET6, const_cast<UCHAR *>(address), text, 128)
                   ? QString::fromWCharArray(text)
                   : QString();
    };
    bytes = 0;
    GetExtendedTcpTable(nullptr, &bytes, FALSE, AF_INET6, TCP_TABLE_OWNER_PID_ALL, 0);
    buffer.resize(bytes);
    if (GetExtendedTcpTable(buffer.data(), &bytes, FALSE, AF_INET6, TCP_TABLE_OWNER_PID_ALL, 0) ==
        NO_ERROR) {
        auto *table = reinterpret_cast<MIB_TCP6TABLE_OWNER_PID *>(buffer.data());
        for (DWORD i = 0; i < table->dwNumEntries; ++i) {
            auto &r = table->table[i];
            result.rows.append(QJsonObject{{"Protocole", "TCP IPv6"},
                                           {"Locale", ipv6(r.ucLocalAddr)},
                                           {"Port local", int(ntohs(USHORT(r.dwLocalPort)))},
                                           {"Distante", ipv6(r.ucRemoteAddr)},
                                           {"Port distant", int(ntohs(USHORT(r.dwRemotePort)))},
                                           {"État", int(r.dwState)},
                                           {"PID", double(r.dwOwningPid)}});
        }
    }
    bytes = 0;
    GetExtendedUdpTable(nullptr, &bytes, FALSE, AF_INET6, UDP_TABLE_OWNER_PID, 0);
    buffer.resize(bytes);
    if (GetExtendedUdpTable(buffer.data(), &bytes, FALSE, AF_INET6, UDP_TABLE_OWNER_PID, 0) ==
        NO_ERROR) {
        auto *table = reinterpret_cast<MIB_UDP6TABLE_OWNER_PID *>(buffer.data());
        for (DWORD i = 0; i < table->dwNumEntries; ++i) {
            auto &r = table->table[i];
            result.rows.append(QJsonObject{{"Protocole", "UDP IPv6"},
                                           {"Locale", ipv6(r.ucLocalAddr)},
                                           {"Port local", int(ntohs(USHORT(r.dwLocalPort)))},
                                           {"PID", double(r.dwOwningPid)}});
        }
    }
    QMap<DWORD, QString> names;
    Handle processes{CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0)};
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (processes.h != INVALID_HANDLE_VALUE && Process32FirstW(processes.h, &entry))
        do {
            names[entry.th32ProcessID] = QString::fromWCharArray(entry.szExeFile);
        } while (Process32NextW(processes.h, &entry));
    const QStringList states{"Inconnu",  "Fermé",      "Écoute",     "SYN envoyé", "SYN reçu",
                             "Établie",  "FIN_WAIT_1", "FIN_WAIT_2", "CLOSE_WAIT", "CLOSING",
                             "LAST_ACK", "TIME_WAIT",  "DELETE_TCB"};
    for (int i = 0; i < result.rows.size(); ++i) {
        auto row = result.rows[i].toObject();
        row["Processus"] = names.value(DWORD(row["PID"].toInteger()), "Disparu / inconnu");
        if (row["État"].isDouble()) {
            int state = row["État"].toInt();
            row["Code état TCP"] = state;
            row["État"] = state >= 0 && state < states.size() ? states[state] : "Inconnu";
        }
        result.rows[i] = row;
    }
    if (result.error.isEmpty())
        result.error =
            "TCP/UDP IPv4 et IPv6. L'association PID/nom est un instantané non atomique. "
            "L'attribution des débits par processus nécessite un collecteur ETW supplémentaire.";
    return result;
}
Result nvidiaSensors() {
    HMODULE library = LoadLibraryExW(L"nvml.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!library)
        return {{},
                "NVML NVIDIA absente du répertoire système ; aucun téléchargement automatique."};
    auto symbol = [library](const char *name) { return GetProcAddress(library, name); };
    auto init = reinterpret_cast<int (*)()>(symbol("nvmlInit_v2"));
    auto shutdown = reinterpret_cast<int (*)()>(symbol("nvmlShutdown"));
    auto countFn = reinterpret_cast<int (*)(unsigned *)>(symbol("nvmlDeviceGetCount_v2"));
    auto handleFn =
        reinterpret_cast<int (*)(unsigned, void **)>(symbol("nvmlDeviceGetHandleByIndex_v2"));
    if (!init || !shutdown || !countFn || !handleFn || init() != 0) {
        FreeLibrary(library);
        return {{}, "NVML non initialisable."};
    }
    Result result;
    unsigned count = 0;
    if (countFn(&count) == 0)
        for (unsigned i = 0; i < std::min(count, 64u); ++i) {
            void *device = nullptr;
            if (handleFn(i, &device) != 0)
                continue;
            QJsonObject row{{"Adaptateur", int(i)}, {"Source", "NVIDIA NVML"}};
            auto name =
                reinterpret_cast<int (*)(void *, char *, unsigned)>(symbol("nvmlDeviceGetName"));
            char buffer[256]{};
            if (name && name(device, buffer, 256) == 0)
                row["Nom"] = QString::fromUtf8(buffer);
            auto temperature = reinterpret_cast<int (*)(void *, unsigned, unsigned *)>(
                symbol("nvmlDeviceGetTemperature"));
            unsigned value = 0;
            if (temperature && temperature(device, 0, &value) == 0)
                row["Température °C"] = int(value);
            auto power =
                reinterpret_cast<int (*)(void *, unsigned *)>(symbol("nvmlDeviceGetPowerUsage"));
            if (power && power(device, &value) == 0)
                row["Puissance W"] = value / 1000.0;
            auto fan =
                reinterpret_cast<int (*)(void *, unsigned *)>(symbol("nvmlDeviceGetFanSpeed"));
            if (fan && fan(device, &value) == 0)
                row["Ventilateur %"] = int(value);
            auto throttle = reinterpret_cast<int (*)(void *, unsigned long long *)>(
                symbol("nvmlDeviceGetCurrentClocksThrottleReasons"));
            unsigned long long reasons = 0;
            if (throttle && throttle(device, &reasons) == 0) {
                row["Limitation thermique signalée"] = bool(reasons & 0x60);
                row["Raisons limitation NVML (hex)"] = QString::number(reasons, 16);
            }
            auto clock = reinterpret_cast<int (*)(void *, unsigned, unsigned *)>(
                symbol("nvmlDeviceGetClockInfo"));
            if (clock && clock(device, 0, &value) == 0)
                row["Fréquence GPU MHz"] = int(value);
            if (clock && clock(device, 2, &value) == 0)
                row["Fréquence mémoire MHz"] = int(value);
            struct Memory {
                unsigned long long total, free, used;
            };
            Memory memory{};
            auto memoryFn =
                reinterpret_cast<int (*)(void *, Memory *)>(symbol("nvmlDeviceGetMemoryInfo"));
            if (memoryFn && memoryFn(device, &memory) == 0) {
                row["VRAM totale octets"] = double(memory.total);
                row["VRAM utilisée octets"] = double(memory.used);
            }
            struct Utilization {
                unsigned gpu, memory;
            };
            Utilization utilization{};
            auto utilizationFn = reinterpret_cast<int (*)(void *, Utilization *)>(
                symbol("nvmlDeviceGetUtilizationRates"));
            if (utilizationFn && utilizationFn(device, &utilization) == 0)
                row["GPU %"] = int(utilization.gpu);
            result.rows.append(row);
        }
    shutdown();
    FreeLibrary(library);
    result.error =
        "Seuls les champs retournés par le pilote sont affichés ; les champs absents ne sont pas "
        "disponibles. GPU AMD/Intel : compteurs Windows ou fournisseur de capteurs externe.";
    return result;
}
} // namespace
Result inventory(const QString &category) {
    if (category == "alertMetrics") {
        static std::mutex mutex;
        std::lock_guard guard(mutex);
        QJsonObject row;
        auto gpu = nvidiaSensors();
        double usage = -1, temperature = -1;
        for (auto value : gpu.rows) {
            auto obj = value.toObject();
            if (obj["GPU %"].isDouble())
                usage = std::max(usage, obj["GPU %"].toDouble());
            if (obj["Température °C"].isDouble())
                temperature = std::max(temperature, obj["Température °C"].toDouble());
        }
        if (usage >= 0)
            row["gpu-percent"] = usage;
        if (temperature >= 0)
            row["gpu-temperature"] = temperature;
        wchar_t windows[MAX_PATH]{};
        GetWindowsDirectoryW(windows, MAX_PATH);
        QStorageInfo volume(QString::fromWCharArray(windows));
        if (volume.isReady() && volume.bytesTotal() > 0)
            row["disk-used-percent"] =
                100.0 * (1 - double(volume.bytesAvailable()) / volume.bytesTotal());
        struct NetworkCounter {
            quint64 received, sent;
            ULONGLONG time;
        };
        static std::map<quint64, NetworkCounter> previous;
        std::map<quint64, NetworkCounter> next;
        MIB_IF_TABLE2 *interfaces = nullptr;
        if (GetIfTable2(&interfaces) == NO_ERROR) {
            bool found = false, up = false, rate = false;
            double received = 0, sent = 0;
            auto now = GetTickCount64();
            for (ULONG i = 0; i < interfaces->NumEntries; ++i) {
                auto &iface = interfaces->Table[i];
                if (iface.Type == IF_TYPE_ETHERNET_CSMACD || iface.Type == IF_TYPE_IEEE80211) {
                    found = true;
                    if (iface.OperStatus == IfOperStatusUp)
                        up = true;
                    auto old = previous.find(iface.InterfaceLuid.Value);
                    if (old != previous.end() && now > old->second.time &&
                        iface.InOctets >= old->second.received &&
                        iface.OutOctets >= old->second.sent) {
                        double seconds = (now - old->second.time) / 1000.0;
                        received += (iface.InOctets - old->second.received) / seconds;
                        sent += (iface.OutOctets - old->second.sent) / seconds;
                        rate = true;
                    }
                    next[iface.InterfaceLuid.Value] = {iface.InOctets, iface.OutOctets, now};
                }
            }
            if (found)
                row["network-loss"] = up ? 0 : 100;
            if (rate) {
                row["network-receive-bytes-per-sec"] = received;
                row["network-send-bytes-per-sec"] = sent;
            }
            FreeMibTable(interfaces);
        }
        previous = std::move(next);
        struct DiskCounters {
            PDH_HQUERY query = nullptr;
            PDH_HCOUNTER read = nullptr, write = nullptr;
            DiskCounters() {
                if (PdhOpenQueryW(nullptr, 0, &query) == ERROR_SUCCESS) {
                    PdhAddEnglishCounterW(query, L"\\PhysicalDisk(_Total)\\Disk Read Bytes/sec", 0,
                                          &read);
                    PdhAddEnglishCounterW(query, L"\\PhysicalDisk(_Total)\\Disk Write Bytes/sec", 0,
                                          &write);
                }
            }
            ~DiskCounters() {
                if (query)
                    PdhCloseQuery(query);
            }
        };
        static DiskCounters disk;
        if (disk.query && PdhCollectQueryData(disk.query) == ERROR_SUCCESS) {
            for (const auto &pair : {std::pair{disk.read, QString("disk-read-bytes-per-sec")},
                                     std::pair{disk.write, QString("disk-write-bytes-per-sec")}}) {
                PDH_FMT_COUNTERVALUE value{};
                if (pair.first &&
                    PdhGetFormattedCounterValue(pair.first, PDH_FMT_DOUBLE, nullptr, &value) ==
                        ERROR_SUCCESS &&
                    (value.CStatus == PDH_CSTATUS_VALID_DATA ||
                     value.CStatus == PDH_CSTATUS_NEW_DATA) &&
                    std::isfinite(value.doubleValue) && value.doubleValue >= 0)
                    row[pair.second] = value.doubleValue;
            }
        }
        return {{row}, {}};
    }
    if (category == "nvidia")
        return nvidiaSensors();
    if (category == "services")
        return services();
    if (category == "startup")
        return startup();
    if (category == "connections")
        return connections();
    if (category == "volumes") {
        Result result;
        for (const auto &disk : QStorageInfo::mountedVolumes())
            if (disk.isValid() && disk.isReady())
                result.rows.append(
                    QJsonObject{{"Volume", disk.rootPath()},
                                {"Nom", disk.displayName()},
                                {"Système de fichiers", QString::fromUtf8(disk.fileSystemType())},
                                {"Capacité octets", double(disk.bytesTotal())},
                                {"Disponible octets", double(disk.bytesAvailable())}});
        return result;
    }
    const QMap<QString, QString> scripts{
        {"gpu", "Get-CimInstance Win32_VideoController | Select-Object "
                "Name,AdapterCompatibility,DriverVersion,VideoProcessor,Status"},
        {"disks", "Get-CimInstance Win32_DiskDrive | Select-Object "
                  "Model,InterfaceType,MediaType,Size,Status,SerialNumber"},
        {"memory", "Get-CimInstance Win32_PhysicalMemory | Select-Object "
                   "BankLabel,DeviceLocator,Capacity,Speed,ConfiguredClockSpeed,Manufacturer,"
                   "PartNumber,SMBIOSMemoryType"},
        {"cpu", "Get-CimInstance Win32_Processor | Select-Object "
                "Name,Manufacturer,NumberOfCores,NumberOfLogicalProcessors,MaxClockSpeed,"
                "CurrentClockSpeed,L2CacheSize,L3CacheSize"},
        {"system",
         "Get-CimInstance Win32_OperatingSystem | Select-Object "
         "Caption,Version,BuildNumber,OSArchitecture,LastBootUpTime,TotalVisibleMemorySize"},
        {"network",
         "Get-CimInstance Win32_NetworkAdapterConfiguration -Filter 'IPEnabled=True' | "
         "Select-Object "
         "Description,MACAddress,IPAddress,DefaultIPGateway,DNSServerSearchOrder,DHCPEnabled"},
        {"diskRates",
         "Get-CimInstance Win32_PerfFormattedData_PerfDisk_PhysicalDisk | Select-Object "
         "Name,DiskReadBytesPersec,DiskWriteBytesPersec,PercentDiskTime,CurrentDiskQueueLength"},
        {"networkRates", "Get-CimInstance Win32_PerfFormattedData_Tcpip_NetworkInterface | "
                         "Select-Object Name,BytesReceivedPersec,BytesSentPersec,CurrentBandwidth"},
        {"gpuRates", "Get-CimInstance Win32_PerfFormattedData_GPUPerformanceCounters_GPUEngine | "
                     "Select-Object Name,UtilizationPercentage"},
        {"vram", "Get-CimInstance Win32_PerfFormattedData_GPUPerformanceCounters_GPUAdapterMemory "
                 "| Select-Object Name,DedicatedUsage,SharedUsage,TotalCommitted"},
        {"sensors", "& { try { Get-CimInstance -Namespace root/LibreHardwareMonitor -ClassName "
                    "Sensor -ErrorAction Stop } catch { Get-CimInstance -Namespace "
                    "root/OpenHardwareMonitor -ClassName Sensor -ErrorAction Stop } } | "
                    "Select-Object Name,SensorType,Value,Min,Max,Identifier,Parent"},
        {"health", "Get-PhysicalDisk | Get-StorageReliabilityCounter | Select-Object "
                   "DeviceId,Temperature,Wear,PowerOnHours,ReadErrorsTotal,WriteErrorsTotal"}};
    auto script = scripts.constFind(category);
    if (script == scripts.cend())
        return {{}, "Catégorie inconnue."};
    return cim(*script);
}
bool autoStartEnabled() {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0,
                      KEY_READ, &key) != ERROR_SUCCESS)
        return false;
    auto command = regString(key, L"Baawiil Monitor");
    RegCloseKey(key);
    return command ==
           "\"" + QDir::toNativeSeparators(QCoreApplication::applicationFilePath()) + "\"";
}
QString setAutoStart(bool enabled) {
    HKEY key = nullptr;
    LONG result =
        RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0,
                        nullptr, 0, KEY_READ | KEY_WRITE, nullptr, &key, nullptr);
    if (result != ERROR_SUCCESS)
        return winError(DWORD(result));
    const QString expected = "\"" +
                             QDir::toNativeSeparators(QCoreApplication::applicationFilePath()) +
                             "\"",
                  existing = regString(key, L"Baawiil Monitor");
    if (!existing.isEmpty() && existing != expected) {
        RegCloseKey(key);
        return "Une entrée homonyme différente existe : aucune modification.";
    }
    if (enabled)
        result = RegSetValueExW(key, L"Baawiil Monitor", 0, REG_SZ,
                                reinterpret_cast<const BYTE *>(expected.utf16()),
                                DWORD((expected.size() + 1) * 2));
    else
        result = RegDeleteValueW(key, L"Baawiil Monitor");
    RegCloseKey(key);
    return result == ERROR_SUCCESS || (!enabled && result == ERROR_FILE_NOT_FOUND)
               ? QString()
               : winError(DWORD(result));
}
QString processAction(quint32 pid, quint64 creation, const QString &action, quint64 value) {
    if (!pid || !creation)
        return "Identité du processus non vérifiable : action refusée.";
    DWORD rights = PROCESS_QUERY_LIMITED_INFORMATION;
    if (action == "terminate")
        rights |= PROCESS_TERMINATE;
    else if (action == "priority" || action == "affinity")
        rights |= PROCESS_SET_INFORMATION;
    else
        return "Action inconnue.";
    Handle process{OpenProcess(rights, FALSE, pid)};
    if (!process.h)
        return winError();
    FILETIME c{}, e{}, k{}, u{};
    if (!GetProcessTimes(process.h, &c, &e, &k, &u) || ft(c) != creation)
        return "Le processus a changé ou n'est plus accessible : action annulée.";
    BOOL done = FALSE;
    if (action == "terminate") {
        BOOL critical = FALSE;
        if (!IsProcessCritical(process.h, &critical))
            return winError();
        if (critical || pid == GetCurrentProcessId())
            return "Processus critique ou application elle-même : terminaison refusée.";
        done = TerminateProcess(process.h, 1);
    } else if (action == "priority") {
        if (value != IDLE_PRIORITY_CLASS && value != BELOW_NORMAL_PRIORITY_CLASS &&
            value != NORMAL_PRIORITY_CLASS && value != ABOVE_NORMAL_PRIORITY_CLASS &&
            value != HIGH_PRIORITY_CLASS)
            return "Priorité non autorisée.";
        done = SetPriorityClass(process.h, DWORD(value));
    } else {
        if (GetActiveProcessorGroupCount() > 1)
            return "Affinité non disponible sur plusieurs groupes de processeurs.";
        DWORD_PTR current = 0, system = 0;
        if (!GetProcessAffinityMask(process.h, &current, &system))
            return winError();
        if (!value || (value & ~quint64(system)))
            return "Masque d'affinité invalide.";
        done = SetProcessAffinityMask(process.h, DWORD_PTR(value));
    }
    return done ? QString() : winError();
}
QString serviceAction(const QString &name, const QString &action) {
    if (action == "restart") {
        auto error = serviceAction(name, "stop");
        if (!error.isEmpty())
            return error;
        return serviceAction(name, "start");
    }
    if (name.isEmpty() || name.size() > 256)
        return "Nom de service invalide.";
    Service manager{OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT)};
    if (!manager.h)
        return winError();
    Service service{
        OpenServiceW(manager.h, reinterpret_cast<const wchar_t *>(name.utf16()),
                     (action == "start" ? SERVICE_START : SERVICE_STOP) | SERVICE_QUERY_STATUS)};
    if (!service.h)
        return winError();
    SERVICE_STATUS status{};
    if (action == "start") {
        if (!StartServiceW(service.h, 0, nullptr) &&
            GetLastError() != ERROR_SERVICE_ALREADY_RUNNING)
            return winError();
    } else if (action == "stop") {
        if (!ControlService(service.h, SERVICE_CONTROL_STOP, &status) &&
            GetLastError() != ERROR_SERVICE_NOT_ACTIVE)
            return winError();
    } else
        return "Action inconnue.";
    for (int i = 0; i < 100; ++i) {
        if (!QueryServiceStatus(service.h, &status))
            return winError();
        if (status.dwCurrentState == (action == "start" ? SERVICE_RUNNING : SERVICE_STOPPED))
            return {};
        Sleep(100);
    }
    return "Commande envoyée, état cible non atteint après 10 secondes : vérifier le service.";
}
Result processDetails(quint32 pid, quint64 creation, bool modules) {
    Handle process{OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid)};
    if (!process.h)
        return {{}, winError()};
    FILETIME c{}, e{}, k{}, u{};
    if (!creation || !GetProcessTimes(process.h, &c, &e, &k, &u) || ft(c) != creation)
        return {{}, "Identité de processus non vérifiable."};
    Result result;
    if (modules) {
        Handle snapshot{CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid)};
        if (snapshot.h == INVALID_HANDLE_VALUE)
            return {{}, winError()};
        MODULEENTRY32W entry{};
        entry.dwSize = sizeof(entry);
        if (Module32FirstW(snapshot.h, &entry))
            do {
                result.rows.append(QJsonObject{{"Module", QString::fromWCharArray(entry.szModule)},
                                               {"Chemin", QString::fromWCharArray(entry.szExePath)},
                                               {"Taille octets", double(entry.modBaseSize)}});
            } while (Module32NextW(snapshot.h, &entry));
        else
            result.error = winError();
        return result;
    }
    wchar_t path[32768]{};
    DWORD size = 32768;
    QString executable;
    if (QueryFullProcessImageNameW(process.h, 0, path, &size))
        executable = QString::fromWCharArray(path, int(size));
    DWORD count = 0;
    GetProcessHandleCount(process.h, &count);
    DWORD priority = GetPriorityClass(process.h);
    QJsonObject row{{"PID", double(pid)},
                    {"Création FILETIME", QString::number(creation)},
                    {"Chemin", executable},
                    {"Handles (nombre)", double(count)},
                    {"Priorité Win32", double(priority)}};
    HANDLE token = nullptr;
    if (OpenProcessToken(process.h, TOKEN_QUERY, &token)) {
        Handle closeToken{token};
        DWORD bytes = 0;
        GetTokenInformation(token, TokenUser, nullptr, 0, &bytes);
        std::vector<BYTE> data(bytes);
        if (GetTokenInformation(token, TokenUser, data.data(), bytes, &bytes)) {
            wchar_t user[1024]{}, domain[1024]{};
            DWORD userSize = 1024, domainSize = 1024;
            SID_NAME_USE use{};
            if (LookupAccountSidW(nullptr, reinterpret_cast<TOKEN_USER *>(data.data())->User.Sid,
                                  user, &userSize, domain, &domainSize, &use))
                row["Utilisateur"] =
                    QString::fromWCharArray(domain) + "\\" + QString::fromWCharArray(user);
        }
    }
    if (!executable.isEmpty()) {
        WINTRUST_FILE_INFO file{};
        file.cbStruct = sizeof(file);
        file.pcwszFilePath = reinterpret_cast<const wchar_t *>(executable.utf16());
        WINTRUST_DATA trust{};
        trust.cbStruct = sizeof(trust);
        trust.dwUIChoice = WTD_UI_NONE;
        trust.fdwRevocationChecks = WTD_REVOKE_NONE;
        trust.dwUnionChoice = WTD_CHOICE_FILE;
        trust.pFile = &file;
        trust.dwStateAction = WTD_STATEACTION_VERIFY;
        trust.dwProvFlags = WTD_CACHE_ONLY_URL_RETRIEVAL;
        GUID policy = WINTRUST_ACTION_GENERIC_VERIFY_V2;
        LONG status = WinVerifyTrust(nullptr, &policy, &trust);
        trust.dwStateAction = WTD_STATEACTION_CLOSE;
        WinVerifyTrust(nullptr, &policy, &trust);
        row["Signature locale"] =
            status == ERROR_SUCCESS
                ? "Valide selon la chaîne locale ; révocation en ligne non vérifiée"
            : status == TRUST_E_NOSIGNATURE ? "Non signé"
                                            : "Invalide ou non vérifiable hors ligne";
        DWORD ignored = 0, bytes = GetFileVersionInfoSizeW(
                               reinterpret_cast<const wchar_t *>(executable.utf16()), &ignored);
        std::vector<BYTE> version(bytes);
        if (bytes && GetFileVersionInfoW(reinterpret_cast<const wchar_t *>(executable.utf16()), 0,
                                         bytes, version.data())) {
            struct Translation {
                WORD language, codepage;
            };
            Translation *translations = nullptr;
            UINT translationBytes = 0;
            if (VerQueryValueW(version.data(), L"\\VarFileInfo\\Translation",
                               reinterpret_cast<void **>(&translations), &translationBytes) &&
                translationBytes >= sizeof(Translation)) {
                auto key = QString("\\StringFileInfo\\%1%2\\CompanyName")
                               .arg(translations[0].language, 4, 16, QChar('0'))
                               .arg(translations[0].codepage, 4, 16, QChar('0'));
                wchar_t *company = nullptr;
                UINT chars = 0;
                if (VerQueryValueW(version.data(), reinterpret_cast<const wchar_t *>(key.utf16()),
                                   reinterpret_cast<void **>(&company), &chars) &&
                    chars)
                    row["Éditeur déclaré"] = QString::fromWCharArray(company);
            }
        }
    }
    result.rows.append(row);
    result.error = "L'éditeur déclaré n'est pas une preuve de confiance. Les handles individuels "
                   "ne sont pas inspectés.";
    return result;
}
Result processTree(quint32 pid, quint64 creation) {
    if (!pid || !creation)
        return {{}, "Identité racine non vérifiable."};
    struct Node {
        DWORD pid, parent;
        quint64 created;
        QString name;
    };
    std::vector<Node> nodes;
    Handle snapshot{CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0)};
    if (snapshot.h == INVALID_HANDLE_VALUE)
        return {{}, winError()};
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (Process32FirstW(snapshot.h, &entry))
        do {
            Handle process{
                OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ProcessID)};
            FILETIME c{}, e{}, k{}, u{};
            if (process.h && GetProcessTimes(process.h, &c, &e, &k, &u))
                nodes.push_back({entry.th32ProcessID, entry.th32ParentProcessID, ft(c),
                                 QString::fromWCharArray(entry.szExeFile)});
        } while (Process32NextW(snapshot.h, &entry));
    auto root = std::find_if(nodes.begin(), nodes.end(), [&](const Node &node) {
        return node.pid == pid && node.created == creation;
    });
    if (root == nodes.end())
        return {{}, "Le processus racine a changé ou disparu."};
    std::vector<Node> selected{*root};
    for (std::size_t i = 0; i < selected.size() && selected.size() < 4096; ++i)
        for (const auto &node : nodes)
            if (node.parent == selected[i].pid && node.created >= selected[i].created &&
                std::none_of(selected.begin(), selected.end(),
                             [&](const Node &value) { return value.pid == node.pid; }))
                selected.push_back(node);
    Result result;
    for (auto it = selected.rbegin(); it != selected.rend(); ++it)
        result.rows.append(QJsonObject{{"Nom", it->name},
                                       {"PID", double(it->pid)},
                                       {"Création FILETIME", QString::number(it->created)}});
    result.error =
        "Instantané : seuls les processus dont l'identité est lisible sont inclus ; de nouveaux "
        "enfants peuvent apparaître ensuite. Aucune action n'a encore été effectuée.";
    return result;
}
Result processHandles(quint32 pid, quint64 creation) {
    Handle process{OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid)};
    FILETIME c{}, e{}, k{}, u{};
    if (!process.h || !creation || !GetProcessTimes(process.h, &c, &e, &k, &u) || ft(c) != creation)
        return {{}, "Identité du processus inaccessible ou modifiée."};
    using Query = LONG(NTAPI *)(ULONG, void *, ULONG, ULONG *);
    auto query = reinterpret_cast<Query>(
        GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQuerySystemInformation"));
    if (!query)
        return {{}, "API NT d'inventaire des handles indisponible."};
    struct Entry {
        void *object;
        ULONG_PTR pid, handle;
        ULONG access;
        USHORT trace, type;
        ULONG attributes, reserved;
    };
    struct Table {
        ULONG_PTR count, reserved;
        Entry entries[1];
    };
    std::vector<BYTE> buffer(1024 * 1024);
    ULONG needed = 0;
    LONG status = 0;
    for (int attempt = 0; attempt < 8; ++attempt) {
        status = query(64, buffer.data(), ULONG(buffer.size()), &needed);
        if (ULONG(status) != 0xC0000004u)
            break;
        auto size = std::max<std::size_t>(buffer.size() * 2, std::size_t(needed) + 65536);
        if (size > 128ull * 1024 * 1024)
            return {{}, "Inventaire de handles refusé : limite mémoire de 128 Mio atteinte."};
        buffer.resize(size);
    }
    if (status < 0)
        return {{},
                QString("Inventaire de handles indisponible, statut NT 0x%1. Aucun privilège de "
                        "débogage activé.")
                    .arg(ULONG(status), 8, 16, QChar('0'))};
    auto *table = reinterpret_cast<const Table *>(buffer.data());
    auto maximum = (buffer.size() - offsetof(Table, entries)) / sizeof(Entry);
    if (table->count > maximum)
        return {{}, "Format de table de handles non reconnu sur cette version de Windows."};
    Result result;
    for (ULONG_PTR i = 0; i < table->count; ++i) {
        const auto &entry = table->entries[i];
        if (entry.pid == pid)
            result.rows.append(QJsonObject{{"Handle hex", QString::number(entry.handle, 16)},
                                           {"Type (index NT)", int(entry.type)},
                                           {"Droits hex", QString::number(entry.access, 16)},
                                           {"Attributs", double(entry.attributes)}});
    }
    result.error = "Instantané via API NT non contractuelle : compatibilité à vérifier selon "
                   "Windows. Aucune adresse noyau exposée, aucun handle fermé ou dupliqué. Les "
                   "noms d'objets ne sont pas interrogés.";
    return result;
}
QString startupToggle(const QString &name, bool enable) {
    if (name.isEmpty() || name.size() > 16383)
        return "Nom invalide.";
    HKEY run = nullptr, backup = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run", 0,
                        nullptr, 0, KEY_READ | KEY_WRITE, nullptr, &run, nullptr) != ERROR_SUCCESS)
        return "Clé Run inaccessible.";
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Baawiil\\DisabledRun", 0, nullptr, 0,
                        KEY_READ | KEY_WRITE, nullptr, &backup, nullptr) != ERROR_SUCCESS) {
        RegCloseKey(run);
        return "Sauvegarde inaccessible.";
    }
    HKEY source = enable ? backup : run, target = enable ? run : backup;
    const auto *key = reinterpret_cast<const wchar_t *>(name.utf16());
    DWORD type = 0, bytes = 0;
    LONG error = RegQueryValueExW(source, key, nullptr, &type, nullptr, &bytes);
    std::vector<BYTE> data(bytes);
    if (error == ERROR_SUCCESS && bytes <= 1048576 && (type == REG_SZ || type == REG_EXPAND_SZ)) {
        error = RegQueryValueExW(source, key, nullptr, &type, data.data(), &bytes);
        DWORD existing = 0;
        if (error == ERROR_SUCCESS && RegQueryValueExW(target, key, nullptr, nullptr, nullptr,
                                                       &existing) != ERROR_FILE_NOT_FOUND)
            error = ERROR_ALREADY_EXISTS;
        if (error == ERROR_SUCCESS)
            error = RegSetValueExW(target, key, 0, type, data.data(), bytes);
        if (error == ERROR_SUCCESS)
            error = RegDeleteValueW(source, key);
    } else if (error == ERROR_SUCCESS)
        error = ERROR_INVALID_DATA;
    RegCloseKey(run);
    RegCloseKey(backup);
    return error == ERROR_SUCCESS ? QString() : winError(DWORD(error));
}
QString startupFileToggle(const QString &path, bool enable) {
    wchar_t folder[MAX_PATH]{};
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_STARTUP, nullptr, SHGFP_TYPE_CURRENT, folder)))
        return "Dossier de démarrage utilisateur inaccessible.";
    QDir startup(QString::fromWCharArray(folder));
    auto backupRoot = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) +
                      "/disabled-startup";
    QDir backup(backupRoot);
    if (!QDir().mkpath(backupRoot))
        return "Dossier de sauvegarde inaccessible.";
    QString original, stored, metadataPath;
    if (enable) {
        QFileInfo file(path);
        if (file.absolutePath().compare(backup.absolutePath(), Qt::CaseInsensitive) != 0 ||
            !QRegularExpression("^[a-f0-9]{64}\\.bin$").match(file.fileName()).hasMatch())
            return "Sauvegarde hors du périmètre autorisé.";
        stored = file.absoluteFilePath();
        metadataPath = backup.filePath(file.completeBaseName() + ".json");
        QFile metadata(metadataPath);
        if (!metadata.open(QIODevice::ReadOnly) || metadata.size() > 4096)
            return "Métadonnées de restauration invalides.";
        auto name = QJsonDocument::fromJson(metadata.readAll()).object()["name"].toString();
        if (name.isEmpty() || name == "." || name == ".." || name.contains('/') ||
            name.contains('\\') || name.contains(':') || name.contains(QChar(0)))
            return "Nom de restauration invalide.";
        original = startup.filePath(name);
        auto hash =
            QCryptographicHash::hash(original.toLower().toUtf8(), QCryptographicHash::Sha256)
                .toHex();
        if (file.completeBaseName() != QString::fromLatin1(hash))
            return "L'identité de la sauvegarde ne correspond pas au fichier d'origine.";
        if (!MoveFileExW(reinterpret_cast<const wchar_t *>(stored.utf16()),
                         reinterpret_cast<const wchar_t *>(original.utf16()), 0))
            return winError();
        return QFile::remove(metadataPath)
                   ? QString()
                   : "Fichier restauré ; métadonnées de sauvegarde non supprimées.";
    }
    QFileInfo file(path);
    if (!file.isFile() ||
        file.absolutePath().compare(startup.absolutePath(), Qt::CaseInsensitive) != 0)
        return "Seuls les fichiers directement dans le dossier de démarrage de l'utilisateur sont "
               "modifiables.";
    original = file.absoluteFilePath();
    auto hash = QString::fromLatin1(
        QCryptographicHash::hash(original.toLower().toUtf8(), QCryptographicHash::Sha256).toHex());
    stored = backup.filePath(hash + ".bin");
    metadataPath = backup.filePath(hash + ".json");
    if (QFileInfo::exists(stored))
        return "Une sauvegarde existe déjà : aucune modification.";
    QFile metadata(metadataPath);
    if (!metadata.open(QIODevice::WriteOnly | QIODevice::NewOnly))
        return "Impossible de créer une nouvelle sauvegarde sans écrasement.";
    auto bytes = QJsonDocument(QJsonObject{{"name", file.fileName()}}).toJson();
    if (metadata.write(bytes) != bytes.size() || !metadata.flush()) {
        metadata.close();
        QFile::remove(metadataPath);
        return "Métadonnées non enregistrées ; fichier non modifié.";
    }
    metadata.close();
    if (!MoveFileExW(reinterpret_cast<const wchar_t *>(original.utf16()),
                     reinterpret_cast<const wchar_t *>(stored.utf16()), 0)) {
        auto error = winError();
        QFile::remove(metadataPath);
        return error;
    }
    return {};
}
Result networkTest(const QString &host, bool dns) {
    if (!QRegularExpression("^[A-Za-z0-9][A-Za-z0-9.:-]{0,252}$").match(host).hasMatch())
        return {{}, "Destination invalide."};
    QProcess process;
    wchar_t system[MAX_PATH]{};
    GetSystemDirectoryW(system, MAX_PATH);
    process.start(QString::fromWCharArray(system) + (dns ? "/nslookup.exe" : "/PING.EXE"),
                  dns ? QStringList{host} : QStringList{"-n", "4", "-w", "1000", host});
    if (!process.waitForStarted(5000))
        return {{}, process.errorString()};
    if (!process.waitForFinished(15000)) {
        process.kill();
        process.waitForFinished();
        return {{}, "Test interrompu après 15 secondes."};
    }
    QString output = QString::fromLocal8Bit(process.readAllStandardOutput()) +
                     QString::fromLocal8Bit(process.readAllStandardError());
    return {{QJsonObject{{"Destination", host},
                         {"Test", dns ? "DNS" : "ICMP • 4 requêtes"},
                         {"Code retour", process.exitCode()},
                         {"Résultat", output}}},
            process.exitCode() ? "Le test a échoué ; consulter le résultat. Une absence de réponse "
                                 "ICMP ne prouve pas une panne Internet."
                               : QString()};
}
Result directoryUsage(const QString &root, const std::shared_ptr<std::atomic_bool> &cancel) {
    if (!QFileInfo(root).isDir())
        return {{}, "Dossier invalide."};
    Result result;
    std::vector<std::pair<qint64, QString>> largest;
    quint64 bytes = 0, count = 0, skipped = 0;
    QElapsedTimer timer;
    timer.start();
    QStringList pending{root};
    QMap<QString, quint64> folders;
    while (!pending.isEmpty() && !cancel->load()) {
        auto directory = pending.takeLast();
        if (!QFileInfo(directory).isReadable()) {
            ++skipped;
            continue;
        }
        QDirIterator iterator(directory, QDir::Files | QDir::Dirs | QDir::Hidden | QDir::System |
                                             QDir::NoDotAndDotDot);
        while (iterator.hasNext() && !cancel->load()) {
            iterator.next();
            const auto info = iterator.fileInfo();
            const DWORD attrs = GetFileAttributesW(
                reinterpret_cast<const wchar_t *>(info.absoluteFilePath().utf16()));
            if (attrs == INVALID_FILE_ATTRIBUTES || (attrs & FILE_ATTRIBUTE_REPARSE_POINT)) {
                ++skipped;
                continue;
            }
            if (info.isDir()) {
                pending.append(info.absoluteFilePath());
                continue;
            }
            if (!info.isFile())
                continue;
            if (!info.isReadable()) {
                ++skipped;
                continue;
            }
            const auto size = info.size();
            bytes += quint64(std::max<qint64>(0, size));
            ++count;
            const auto relative = QDir(root).relativeFilePath(info.absoluteFilePath());
            if (relative.contains('/'))
                folders[relative.section('/', 0, 0)] += quint64(std::max<qint64>(0, size));
            largest.emplace_back(size, info.absoluteFilePath());
            if (largest.size() > 2000) {
                std::sort(largest.begin(), largest.end(), std::greater<>());
                largest.resize(1000);
            }
        }
    }
    std::sort(largest.begin(), largest.end(), std::greater<>());
    if (largest.size() > 1000)
        largest.resize(1000);
    for (auto it = folders.begin(); it != folders.end(); ++it)
        result.rows.append(QJsonObject{
            {"Type", "Dossier de premier niveau"},
            {"Chemin", QDir(root).filePath(it.key())},
            {"Taille logique octets", double(it.value())},
            {"Part du périmètre %", bytes ? 100.0 * double(it.value()) / double(bytes) : 0.0}});
    for (const auto &[size, path] : largest)
        result.rows.append(QJsonObject{
            {"Type", "Fichier"},
            {"Chemin", path},
            {"Taille logique octets", double(size)},
            {"Part du périmètre %", bytes ? 100.0 * double(size) / double(bytes) : 0.0}});
    result.error = QString("%1 fichiers • %2 octets logiques • %3 entrées ignorées • %4 ms. %5 "
                           "Liens physiques non dédupliqués, occupation allouée non mesurée ; "
                           "résultats partiels possibles selon les permissions.")
                       .arg(count)
                       .arg(bytes)
                       .arg(skipped)
                       .arg(timer.elapsed())
                       .arg(cancel->load() ? "Analyse annulée." : "Analyse terminée.");
    return result;
}
Result benchmark(const QString &kind, int seconds, const QString &directory,
                 const std::shared_ptr<std::atomic_bool> &cancel) {
    if (seconds < 1 || seconds > 60)
        return {{}, "Durée invalide."};
    QElapsedTimer timer;
    timer.start();
    std::atomic<quint64> operations{0};
    const int threads = kind == "cpu-multi"
                            ? int(std::max(1u, std::min(32u, std::thread::hardware_concurrency())))
                            : 1;
    if (kind == "cpu-single" || kind == "cpu-multi") {
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
        std::vector<std::thread> workers;
        for (int i = 0; i < threads; ++i)
            workers.emplace_back([&, i] {
                quint64 x = quint64(i) + 123456789, blocks = 0;
                while (!cancel->load() && std::chrono::steady_clock::now() < deadline) {
                    for (int n = 0; n < 100000; ++n) {
                        x ^= x << 13;
                        x ^= x >> 7;
                        x ^= x << 17;
                    }
                    ++blocks;
                    // Publish a checksum to prevent the optimizer eliminating the workload.
                    static std::atomic<quint64> checksum{0};
                    checksum.fetch_xor(x, std::memory_order_relaxed);
                }
                operations.fetch_add(blocks * 100000, std::memory_order_relaxed);
            });
        for (auto &worker : workers)
            worker.join();
    } else if (kind == "memory") {
        std::vector<quint64> data(8 * 1024 * 1024, 1);
        quint64 pass = 0;
        while (!cancel->load() && timer.elapsed() < seconds * 1000) {
            for (auto &value : data)
                value = value * 1664525 + 1013904223;
            ++pass;
        }
        volatile auto checksum = std::accumulate(data.begin(), data.end(), quint64(0));
        (void)checksum;
        operations = pass * data.size() * sizeof(quint64) * 2;
    } else if (QStringList{"disk-sequential", "disk-random", "disk-read-sequential",
                           "disk-read-random"}
                   .contains(kind)) {
        QStorageInfo storage(directory);
        if (!storage.isReady() || storage.bytesAvailable() < 512ll * 1024 * 1024)
            return {{}, "Au moins 512 Mio libres sont requis."};
        QTemporaryFile file(QDir(directory).filePath("baawiil-benchmark-XXXXXX.tmp"));
        if (!file.open())
            return {{}, file.errorString()};
        QByteArray block(1024 * 1024, 'B');
        std::mt19937 generator(42);
        quint64 transferred = 0;
        const bool read = kind.contains("read");
        if (read) {
            for (int i = 0; i < 128 && !cancel->load(); ++i)
                if (file.write(block) != block.size())
                    return {{}, file.errorString()};
            if (!file.flush())
                return {{}, file.errorString()};
            timer.restart();
        }
        while (!cancel->load() && timer.elapsed() < seconds * 1000) {
            qint64 offset = kind.contains("random")
                                ? qint64(generator() % 128) * block.size()
                                : qint64((transferred / block.size()) % 128) * block.size();
            if (!file.seek(offset))
                return {{}, file.errorString()};
            if (read) {
                auto data = file.read(block.size());
                if (data.size() != block.size())
                    return {{}, "Lecture temporaire incomplète."};
            } else if (file.write(block) != block.size() || !file.flush())
                return {{}, file.errorString()};
            transferred += quint64(block.size());
        }
        operations = transferred;
    } else
        return {{}, "Benchmark inconnu."};
    const double elapsed = std::max(0.001, timer.elapsed() / 1000.0);
    const bool cpu = kind.startsWith("cpu");
    return {{QJsonObject{
                {"Test", kind},
                {"Protocole", "Baawiil 1"},
                {"Date UTC", QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
                {"Durée s", elapsed},
                {"Threads", threads},
                {"État", cancel->load() ? "Annulé" : "Terminé"},
                {"Résultat",
                 cancel->load() ? QJsonValue() : QJsonValue(double(operations.load()) / elapsed)},
                {"Unité", cpu ? "itérations/s" : "octets/s"},
                {"Conditions",
                 cpu ? "Xorshift entier, charge synthétique non comparable aux suites tierces"
                 : kind == "memory"
                     ? "64 Mio, lecture-modification-écriture ; débit effectif avec caches"
                     : "Fichier temporaire dédié avec cache OS, 128 Mio, blocs 1 Mio ; préparation "
                       "exclue du temps en lecture ; aucune mesure brute du matériel"}}},
            {}};
}
} // namespace features
