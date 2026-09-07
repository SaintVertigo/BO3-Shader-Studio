#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

namespace bo3history
{

struct InstalledFile
{
    QString relativePath;
    QString sha256;
};

struct InstallRecord
{
    QString id;
    QString installedAtUtc;
    QString removedAtUtc;
    QString removedReason;
    QString supersededAtUtc;
    QString supersededReason;
    QString target;
    QString detail;
    QString assetName;
    QString materialName;
    QString techsetName;
    QString nameSpace;
    QString bo3Root;
    QString sourceShader;
    bool removed = false;
    bool superseded = false;
    QVector<InstalledFile> files;
};

struct RecordStatus
{
    QString label;
    int total = 0;
    int present = 0;
    int matching = 0;
    int changed = 0;
    int missing = 0;
};

struct RemoveResult
{
    int deleted = 0;
    int missing = 0;
    int changedKept = 0;
    int sharedKept = 0;
    int failed = 0;
    QStringList changedPaths;
    QStringList sharedPaths;
    QStringList failedPaths;
};

QString historyFilePath();
QString fileSha256(const QString& path);
QVector<InstallRecord> loadHistory(QString* error = nullptr);
bool saveHistory(const QVector<InstallRecord>& records, QString* error = nullptr);

InstallRecord makeInstallRecord(const QString& target,
                                const QString& detail,
                                const QString& assetName,
                                const QString& materialName,
                                const QString& techsetName,
                                const QString& nameSpace,
                                const QString& bo3Root,
                                const QString& sourceShader,
                                const QStringList& installedAbsoluteFiles);

RecordStatus statusForRecord(const InstallRecord& record);

bool removeInstalledFiles(QVector<InstallRecord>& records,
                          int recordIndex,
                          bool deleteChangedFiles,
                          RemoveResult& result,
                          QString* error = nullptr);

bool forgetRecord(QVector<InstallRecord>& records, int recordIndex, QString* error = nullptr);

} // namespace bo3history
