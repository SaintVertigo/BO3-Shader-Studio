#include "bo3_install_history.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>

namespace bo3history
{
namespace
{
constexpr int kHistoryVersion = 1;
constexpr int kMaxHistoryEntries = 250;

Qt::CaseSensitivity pathCaseSensitivity()
{
#ifdef Q_OS_WIN
    return Qt::CaseInsensitive;
#else
    return Qt::CaseSensitive;
#endif
}

QString normalizedRoot(const QString& path)
{
    if(path.trimmed().isEmpty()) return {};
    const QFileInfo info(QDir::cleanPath(path));
    return QDir::cleanPath(info.absoluteFilePath()).replace('\\', '/');
}

bool isSafeRelativePath(const QString& relative)
{
    const QString clean = QDir::cleanPath(relative).replace('\\', '/');
    return !clean.isEmpty() && clean != "." && clean != ".." &&
           !clean.startsWith("../") && !QFileInfo(clean).isAbsolute();
}

bool pathIsInsideRoot(const QString& absolutePath, const QString& root)
{
    const QString cleanPath = QDir::cleanPath(QFileInfo(absolutePath).absoluteFilePath()).replace('\\', '/');
    const QString cleanRoot = normalizedRoot(root);
    if(cleanPath.isEmpty() || cleanRoot.isEmpty()) return false;
    if(cleanPath.compare(cleanRoot, pathCaseSensitivity()) == 0) return false;
    const QString prefix = cleanRoot.endsWith('/') ? cleanRoot : cleanRoot + '/';
    return cleanPath.startsWith(prefix, pathCaseSensitivity());
}

QJsonObject fileToJson(const InstalledFile& file)
{
    QJsonObject object;
    object.insert("path", file.relativePath);
    object.insert("sha256", file.sha256);
    return object;
}

InstalledFile fileFromJson(const QJsonObject& object)
{
    InstalledFile file;
    file.relativePath = QDir::cleanPath(object.value("path").toString()).replace('\\', '/');
    file.sha256 = object.value("sha256").toString().trimmed().toLower();
    return file;
}

QJsonObject recordToJson(const InstallRecord& record)
{
    QJsonObject object;
    object.insert("id", record.id);
    object.insert("installedAtUtc", record.installedAtUtc);
    object.insert("removedAtUtc", record.removedAtUtc);
    object.insert("removedReason", record.removedReason);
    object.insert("supersededAtUtc", record.supersededAtUtc);
    object.insert("supersededReason", record.supersededReason);
    object.insert("target", record.target);
    object.insert("detail", record.detail);
    object.insert("assetName", record.assetName);
    object.insert("materialName", record.materialName);
    object.insert("techsetName", record.techsetName);
    object.insert("namespace", record.nameSpace);
    object.insert("bo3Root", record.bo3Root);
    object.insert("sourceShader", record.sourceShader);
    object.insert("removed", record.removed);
    object.insert("superseded", record.superseded);

    QJsonArray files;
    for(const InstalledFile& file : record.files)
        files.append(fileToJson(file));
    object.insert("files", files);
    return object;
}

InstallRecord recordFromJson(const QJsonObject& object)
{
    InstallRecord record;
    record.id = object.value("id").toString();
    record.installedAtUtc = object.value("installedAtUtc").toString();
    record.removedAtUtc = object.value("removedAtUtc").toString();
    record.removedReason = object.value("removedReason").toString();
    record.supersededAtUtc = object.value("supersededAtUtc").toString();
    record.supersededReason = object.value("supersededReason").toString();
    record.target = object.value("target").toString();
    record.detail = object.value("detail").toString();
    record.assetName = object.value("assetName").toString();
    record.materialName = object.value("materialName").toString();
    record.techsetName = object.value("techsetName").toString();
    record.nameSpace = object.value("namespace").toString();
    record.bo3Root = normalizedRoot(object.value("bo3Root").toString());
    record.sourceShader = object.value("sourceShader").toString();
    record.removed = object.value("removed").toBool(false);
    record.superseded = object.value("superseded").toBool(false);

    const QJsonArray files = object.value("files").toArray();
    for(const QJsonValue& value : files)
    {
        if(!value.isObject()) continue;
        InstalledFile file = fileFromJson(value.toObject());
        if(isSafeRelativePath(file.relativePath)) record.files.append(file);
    }
    return record;
}

QString normalizedOwnershipKey(const QString& root, const QString& relative)
{
    QString absolute = QDir(normalizedRoot(root)).filePath(QDir::cleanPath(relative));
    absolute = QDir::cleanPath(QFileInfo(absolute).absoluteFilePath()).replace('\\', '/');
#ifdef Q_OS_WIN
    return absolute.toLower();
#else
    return absolute;
#endif
}

void pruneEmptyParents(const QString& filePath, const QString& root)
{
    const QString cleanRoot = normalizedRoot(root);
    if(cleanRoot.isEmpty()) return;
    const QString rootPrefix = cleanRoot.endsWith('/') ? cleanRoot : cleanRoot + '/';
    const QSet<QString> protectedRelativeDirs = {
        "share", "share/raw",
        "share/raw/shaders_stable", "share/raw/shaders_stable/geometry",
        "share/raw/shaders_stable_toolsgfx", "share/raw/shaders_stable_toolsgfx/postfx",
        "share/raw/techsetdefs_stable", "share/raw/techsetdefs_stable/postfx",
        "share/raw/techsetdefs_stable/geometry", "share/raw/techsetdefs_stable/geometry_custom",
        "share/raw/techsetdefs_stable/decal",
        "share/raw/techsetdefs_stable_toolsgfx", "share/raw/techsetdefs_stable_toolsgfx/postfx",
        "share/raw/techsetdefs_stable_toolsgfx/geometry", "share/raw/techsetdefs_stable_toolsgfx/geometry_custom",
        "share/raw/techsetdefs_stable_toolsgfx/decal",
        "share/raw/scripts", "share/raw/scripts/postfx",
        "source_data", "texture_assets", "zone_source"
    };
    QString current = QDir::cleanPath(QFileInfo(filePath).absolutePath()).replace('\\', '/');
    while(!current.isEmpty() && current.compare(cleanRoot, pathCaseSensitivity()) != 0 &&
          current.startsWith(rootPrefix, pathCaseSensitivity()))
    {
        QString relative = QDir(cleanRoot).relativeFilePath(current).replace('\\', '/').toLower();
        if(protectedRelativeDirs.contains(relative)) break;
        QDir dir(current);
        if(!dir.entryList(QDir::NoDotAndDotDot | QDir::AllEntries).isEmpty()) break;
        const QFileInfo currentInfo(current);
        const QString parentPath = QDir::cleanPath(currentInfo.absolutePath()).replace('\\', '/');
        QDir parent(parentPath);
        if(!parent.rmdir(currentInfo.fileName())) break;
        current = parentPath;
    }
}

QString makeRecordId(const QString& root, const QString& target, const QString& asset, const QString& timestamp)
{
    const QByteArray seed = (root + "\n" + target + "\n" + asset + "\n" + timestamp + "\n" +
                             QString::number(QDateTime::currentMSecsSinceEpoch())).toUtf8();
    return QString::fromLatin1(QCryptographicHash::hash(seed, QCryptographicHash::Sha256).toHex().left(20));
}

} // namespace

QString historyFilePath()
{
    QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if(base.isEmpty()) base = QDir::home().filePath(".bo3_hlsl_previewer");
    QDir().mkpath(base);
    return QDir(base).filePath("bo3_install_history.json");
}

QString fileSha256(const QString& path)
{
    QFile file(path);
    if(!file.open(QIODevice::ReadOnly)) return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if(!hash.addData(&file)) return {};
    return QString::fromLatin1(hash.result().toHex());
}

QVector<InstallRecord> loadHistory(QString* error)
{
    if(error) error->clear();
    QVector<InstallRecord> records;
    QFile file(historyFilePath());
    if(!file.exists()) return records;
    if(!file.open(QIODevice::ReadOnly))
    {
        if(error) *error = QString("Could not open BO3 install history:\n%1").arg(file.errorString());
        return records;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if(parseError.error != QJsonParseError::NoError || !document.isObject())
    {
        if(error) *error = QString("BO3 install history is not valid JSON:\n%1").arg(parseError.errorString());
        return records;
    }

    const QJsonArray array = document.object().value("records").toArray();
    for(const QJsonValue& value : array)
    {
        if(!value.isObject()) continue;
        InstallRecord record = recordFromJson(value.toObject());
        if(record.id.isEmpty() || record.bo3Root.isEmpty()) continue;
        records.append(record);
    }
    return records;
}

bool saveHistory(const QVector<InstallRecord>& inputRecords, QString* error)
{
    if(error) error->clear();
    QVector<InstallRecord> records = inputRecords;
    if(records.size() > kMaxHistoryEntries)
        records = records.mid(records.size() - kMaxHistoryEntries);

    QJsonArray array;
    for(const InstallRecord& record : records)
        array.append(recordToJson(record));

    QJsonObject root;
    root.insert("version", kHistoryVersion);
    root.insert("records", array);

    const QString path = historyFilePath();
    if(!QDir().mkpath(QFileInfo(path).absolutePath()))
    {
        if(error) *error = "Could not create the BO3 install history folder.";
        return false;
    }

    QSaveFile file(path);
    if(!file.open(QIODevice::WriteOnly))
    {
        if(error) *error = QString("Could not write BO3 install history:\n%1").arg(file.errorString());
        return false;
    }
    if(file.write(QJsonDocument(root).toJson(QJsonDocument::Indented)) < 0 || !file.commit())
    {
        if(error) *error = QString("Could not save BO3 install history:\n%1").arg(file.errorString());
        return false;
    }
    return true;
}

InstallRecord makeInstallRecord(const QString& target,
                                const QString& detail,
                                const QString& assetName,
                                const QString& materialName,
                                const QString& techsetName,
                                const QString& nameSpace,
                                const QString& bo3Root,
                                const QString& sourceShader,
                                const QStringList& installedAbsoluteFiles)
{
    InstallRecord record;
    record.installedAtUtc = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    record.target = target;
    record.detail = detail;
    record.assetName = assetName;
    record.materialName = materialName;
    record.techsetName = techsetName;
    record.nameSpace = nameSpace;
    record.bo3Root = normalizedRoot(bo3Root);
    record.sourceShader = sourceShader.isEmpty() ? QString() : QFileInfo(sourceShader).absoluteFilePath();
    record.id = makeRecordId(record.bo3Root, target, assetName, record.installedAtUtc);

    QSet<QString> seen;
    const QDir rootDir(record.bo3Root);
    for(const QString& rawPath : installedAbsoluteFiles)
    {
        const QFileInfo info(rawPath);
        if(!info.exists() || !info.isFile()) continue;
        const QString absolute = info.absoluteFilePath();
        if(!pathIsInsideRoot(absolute, record.bo3Root)) continue;
        const QString relative = QDir::cleanPath(rootDir.relativeFilePath(absolute)).replace('\\', '/');
        if(!isSafeRelativePath(relative)) continue;
#ifdef Q_OS_WIN
        const QString key = relative.toLower();
#else
        const QString key = relative;
#endif
        if(seen.contains(key)) continue;
        seen.insert(key);
        InstalledFile file;
        file.relativePath = relative;
        file.sha256 = fileSha256(absolute);
        record.files.append(file);
    }
    return record;
}

RecordStatus statusForRecord(const InstallRecord& record)
{
    RecordStatus status;
    status.total = record.files.size();
    if(record.removed)
    {
        status.label = "Removed";
        return status;
    }
    if(record.superseded)
        status.label = "Replaced";
    if(!QFileInfo::exists(record.bo3Root) || !QFileInfo(record.bo3Root).isDir())
    {
        status.label = "BO3 root missing";
        status.missing = status.total;
        return status;
    }

    for(const InstalledFile& file : record.files)
    {
        const QString absolute = QDir(record.bo3Root).filePath(file.relativePath);
        if(!QFileInfo::exists(absolute) || !QFileInfo(absolute).isFile())
        {
            ++status.missing;
            continue;
        }
        ++status.present;
        const QString currentHash = fileSha256(absolute);
        if(!file.sha256.isEmpty() && currentHash.compare(file.sha256, Qt::CaseInsensitive) == 0)
            ++status.matching;
        else
            ++status.changed;
    }

    if(!record.superseded)
    {
        if(status.total == 0) status.label = "No tracked files";
        else if(status.changed > 0) status.label = "Modified";
        else if(status.missing == 0) status.label = "Installed";
        else if(status.present == 0) status.label = "Missing";
        else status.label = "Partially missing";
    }
    return status;
}

bool removeInstalledFiles(QVector<InstallRecord>& records,
                          int recordIndex,
                          bool deleteChangedFiles,
                          RemoveResult& result,
                          QString* error)
{
    result = {};
    if(error) error->clear();
    if(recordIndex < 0 || recordIndex >= records.size())
    {
        if(error) *error = "The selected BO3 install history entry no longer exists.";
        return false;
    }

    InstallRecord& record = records[recordIndex];
    if(record.removed) return true;

    QSet<QString> ownedByOtherActiveRecords;
    for(int i = 0; i < records.size(); ++i)
    {
        if(i == recordIndex || records[i].removed || records[i].superseded) continue;
        for(const InstalledFile& file : records[i].files)
            ownedByOtherActiveRecords.insert(normalizedOwnershipKey(records[i].bo3Root, file.relativePath));
    }

    for(const InstalledFile& file : record.files)
    {
        if(!isSafeRelativePath(file.relativePath)) continue;
        const QString absolute = QDir(record.bo3Root).filePath(file.relativePath);
        if(!pathIsInsideRoot(absolute, record.bo3Root)) continue;

        const QString ownershipKey = normalizedOwnershipKey(record.bo3Root, file.relativePath);
        if(ownedByOtherActiveRecords.contains(ownershipKey))
        {
            ++result.sharedKept;
            result.sharedPaths.append(file.relativePath);
            continue;
        }

        const QFileInfo info(absolute);
        if(!info.exists() || !info.isFile())
        {
            ++result.missing;
            continue;
        }

        const QString currentHash = fileSha256(absolute);
        const bool changed = file.sha256.isEmpty() || currentHash.compare(file.sha256, Qt::CaseInsensitive) != 0;
        if(changed && !deleteChangedFiles)
        {
            ++result.changedKept;
            result.changedPaths.append(file.relativePath);
            continue;
        }

        if(QFile::remove(absolute))
        {
            ++result.deleted;
            pruneEmptyParents(absolute, record.bo3Root);
        }
        else
        {
            ++result.failed;
            result.failedPaths.append(file.relativePath);
        }
    }

    if(result.failed == 0 && result.changedKept == 0)
    {
        record.removed = true;
        record.removedAtUtc = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
        record.removedReason = "Deleted from BO3 by Install History";
    }
    if(!saveHistory(records, error)) return false;
    return true;
}

bool forgetRecord(QVector<InstallRecord>& records, int recordIndex, QString* error)
{
    if(error) error->clear();
    if(recordIndex < 0 || recordIndex >= records.size())
    {
        if(error) *error = "The selected BO3 install history entry no longer exists.";
        return false;
    }
    records.removeAt(recordIndex);
    return saveHistory(records, error);
}

} // namespace bo3history
