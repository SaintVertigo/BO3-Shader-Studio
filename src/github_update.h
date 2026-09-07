#pragma once

#include <QString>
#include <QUrl>

class QWidget;

namespace github_update
{

enum class Channel
{
    Stable,
    Tester
};

struct Config
{
    QString repository;      // owner/repo
    QString assetPrefix = "BO3_HLSL_Previewer_Update_";
};

struct ReleaseInfo
{
    bool valid = false;
    QString version;
    QString tag;
    QString title;
    QString notes;
    bool prerelease = false;
    QUrl releasePage;
    QString assetName;
    QUrl assetUrl;
    qint64 assetSize = 0;
    QString sha256;          // lowercase hex, when GitHub exposes an asset digest
};

QString channelName(Channel channel);
bool repositoryConfigured(const QString& repository);
int compareVersions(const QString& left, const QString& right);

bool fetchLatestRelease(const Config& config,
                        Channel channel,
                        ReleaseInfo& release,
                        QString& error);

bool downloadReleaseAsset(const ReleaseInfo& release,
                          QWidget* parent,
                          QString& localPath,
                          QString& error);

bool verifySha256(const QString& path,
                  const QString& expectedSha256,
                  QString& actualSha256,
                  QString& error);

} // namespace github_update
