#include "github_update.h"

#include <QCryptographicHash>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProgressDialog>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTimer>

#include <algorithm>
#include <climits>

namespace github_update
{
namespace
{
struct ParsedVersion
{
    bool valid = false;
    int major = 0;
    int minor = 0;
    int patch = 0;
    QString prerelease;
};

ParsedVersion parseVersion(QString value)
{
    value = value.trimmed();
    if(value.startsWith('v', Qt::CaseInsensitive)) value.remove(0, 1);
    const QRegularExpression re(R"(^(\d+)\.(\d+)\.(\d+)(?:-([^+]+))?(?:\+.*)?$)");
    const auto match = re.match(value);
    ParsedVersion out;
    if(!match.hasMatch()) return out;
    out.valid = true;
    out.major = match.captured(1).toInt();
    out.minor = match.captured(2).toInt();
    out.patch = match.captured(3).toInt();
    out.prerelease = match.captured(4);
    return out;
}

int comparePrerelease(const QString& a, const QString& b)
{
    if(a.isEmpty() && b.isEmpty()) return 0;
    if(a.isEmpty()) return 1;   // a stable release outranks a prerelease
    if(b.isEmpty()) return -1;

    const QStringList aa = a.split(QRegularExpression("[.-]"), Qt::SkipEmptyParts);
    const QStringList bb = b.split(QRegularExpression("[.-]"), Qt::SkipEmptyParts);
    const int count = std::max(aa.size(), bb.size());
    for(int i = 0; i < count; ++i)
    {
        if(i >= aa.size()) return -1;
        if(i >= bb.size()) return 1;
        bool aNum = false, bNum = false;
        const qlonglong an = aa[i].toLongLong(&aNum);
        const qlonglong bn = bb[i].toLongLong(&bNum);
        if(aNum && bNum)
        {
            if(an < bn) return -1;
            if(an > bn) return 1;
            continue;
        }
        if(aNum != bNum) return aNum ? -1 : 1; // SemVer: numeric identifiers sort before text
        const int cmp = QString::compare(aa[i], bb[i], Qt::CaseInsensitive);
        if(cmp < 0) return -1;
        if(cmp > 0) return 1;
    }
    return 0;
}

bool fetchJson(const QUrl& url, QJsonDocument& document, QString& error)
{
    QNetworkAccessManager manager;
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader, "BO3-Shader-Studio-Updater/2");
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setRawHeader("X-GitHub-Api-Version", "2026-03-10");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);

    QNetworkReply* reply = manager.get(request);
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    timeout.start(20000);
    QObject::connect(&timeout, &QTimer::timeout, reply, &QNetworkReply::abort);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    const QByteArray body = reply->readAll();
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const bool ok = reply->error() == QNetworkReply::NoError && status >= 200 && status < 300;
    const QString networkError = reply->errorString();
    reply->deleteLater();

    if(!ok)
    {
        error = QString("GitHub update check failed (HTTP %1): %2").arg(status).arg(networkError);
        if(status == 403)
            error += "\n\nGitHub may have rate-limited anonymous API requests. Try again later.";
        return false;
    }

    QJsonParseError parseError{};
    document = QJsonDocument::fromJson(body, &parseError);
    if(parseError.error != QJsonParseError::NoError)
    {
        error = QString("GitHub returned invalid JSON: %1").arg(parseError.errorString());
        return false;
    }
    return true;
}

bool releaseFromObject(const QJsonObject& object,
                       const Config& config,
                       ReleaseInfo& out)
{
    if(object.value("draft").toBool(false)) return false;

    QString version = object.value("tag_name").toString().trimmed();
    if(version.startsWith('v', Qt::CaseInsensitive)) version.remove(0, 1);
    if(!parseVersion(version).valid) return false;

    const QJsonArray assets = object.value("assets").toArray();
    QJsonObject selectedAsset;
    for(const QJsonValue& value : assets)
    {
        const QJsonObject asset = value.toObject();
        const QString name = asset.value("name").toString();
        if(name.startsWith(config.assetPrefix, Qt::CaseInsensitive) &&
           name.endsWith(".zip", Qt::CaseInsensitive))
        {
            selectedAsset = asset;
            break;
        }
    }
    if(selectedAsset.isEmpty()) return false;

    out = {};
    out.valid = true;
    out.version = version;
    out.tag = object.value("tag_name").toString();
    out.title = object.value("name").toString().trimmed();
    if(out.title.isEmpty()) out.title = out.tag;
    const QString studioPrefix = "BO3 Shader Studio";
    if(out.title.startsWith(studioPrefix, Qt::CaseInsensitive))
        out.displayVersion = out.title.mid(studioPrefix.size()).trimmed();
    if(out.displayVersion.isEmpty()) out.displayVersion = version;
    out.notes = object.value("body").toString();
    out.prerelease = object.value("prerelease").toBool(false);
    out.releasePage = QUrl(object.value("html_url").toString());
    out.assetName = selectedAsset.value("name").toString();
    out.assetUrl = QUrl(selectedAsset.value("browser_download_url").toString());
    out.assetSize = static_cast<qint64>(selectedAsset.value("size").toDouble(0.0));
    QString digest = selectedAsset.value("digest").toString().trimmed();
    if(digest.startsWith("sha256:", Qt::CaseInsensitive)) digest = digest.mid(7);
    if(QRegularExpression("^[0-9a-fA-F]{64}$").match(digest).hasMatch())
        out.sha256 = digest.toLower();
    return out.assetUrl.isValid();
}
} // namespace

QString channelName(Channel channel)
{
    return channel == Channel::Tester ? "Tester" : "Stable";
}

bool repositoryConfigured(const QString& repository)
{
    const QString value = repository.trimmed();
    if(value.isEmpty() || value.contains("__GITHUB_REPOSITORY__") ||
       value.compare("OWNER/REPO", Qt::CaseInsensitive) == 0)
        return false;
    return QRegularExpression(R"(^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$)").match(value).hasMatch();
}

int compareVersions(const QString& left, const QString& right)
{
    const ParsedVersion a = parseVersion(left);
    const ParsedVersion b = parseVersion(right);
    if(!a.valid || !b.valid)
        return QString::compare(left.trimmed(), right.trimmed(), Qt::CaseInsensitive);
    if(a.major != b.major) return a.major < b.major ? -1 : 1;
    if(a.minor != b.minor) return a.minor < b.minor ? -1 : 1;
    if(a.patch != b.patch) return a.patch < b.patch ? -1 : 1;
    return comparePrerelease(a.prerelease, b.prerelease);
}

bool fetchLatestRelease(const Config& config,
                        Channel channel,
                        ReleaseInfo& release,
                        QString& error)
{
    release = {};
    if(!repositoryConfigured(config.repository))
    {
        error = "GitHub updates are not configured for this build. The release workflow stamps githubRepository into version.json automatically.";
        return false;
    }

    if(channel == Channel::Stable)
    {
        QJsonDocument document;
        const QUrl url(QString("https://api.github.com/repos/%1/releases/latest").arg(config.repository));
        if(!fetchJson(url, document, error)) return false;
        if(!document.isObject() || !releaseFromObject(document.object(), config, release))
        {
            error = "The latest stable GitHub release does not contain a compatible BO3 Shader Studio update ZIP.";
            return false;
        }
        return true;
    }

    QJsonDocument document;
    const QUrl url(QString("https://api.github.com/repos/%1/releases?per_page=30").arg(config.repository));
    if(!fetchJson(url, document, error)) return false;
    if(!document.isArray())
    {
        error = "GitHub returned an unexpected release-list response.";
        return false;
    }

    bool found = false;
    ReleaseInfo best;
    for(const QJsonValue& value : document.array())
    {
        if(!value.isObject()) continue;
        ReleaseInfo candidate;
        if(!releaseFromObject(value.toObject(), config, candidate)) continue;
        if(!found || compareVersions(candidate.version, best.version) > 0)
        {
            best = candidate;
            found = true;
        }
    }
    if(!found)
    {
        error = "No compatible Stable or Tester GitHub release contains a BO3 Shader Studio update ZIP.";
        return false;
    }
    release = best;
    return true;
}

bool downloadReleaseAsset(const ReleaseInfo& release,
                          QWidget* parent,
                          QString& localPath,
                          QString& error)
{
    if(!release.valid || !release.assetUrl.isValid())
    {
        error = "The selected GitHub release does not contain a downloadable update asset.";
        return false;
    }

    QString tempDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    if(tempDir.isEmpty()) tempDir = QDir::tempPath();
    QDir().mkpath(tempDir);
    QString safeName = release.assetName;
    if(safeName.isEmpty()) safeName = QString("BO3_Shader_Studio_Update_AUTO_UPDATER_ONLY.zip");
    safeName.replace(QRegularExpression("[^A-Za-z0-9_.-]"), "_");
    localPath = QDir(tempDir).filePath(safeName);

    QFile output(localPath);
    if(!output.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        error = QString("Could not create the temporary update file:\n%1").arg(localPath);
        return false;
    }

    QNetworkAccessManager manager;
    QNetworkRequest request(release.assetUrl);
    request.setHeader(QNetworkRequest::UserAgentHeader, "BO3-Shader-Studio-Updater/2");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply* reply = manager.get(request);

    QProgressDialog progress("Downloading BO3 Shader Studio update...", "Cancel", 0,
                             release.assetSize > 0 ? static_cast<int>(std::min<qint64>(release.assetSize, INT_MAX)) : 0,
                             parent);
    progress.setWindowTitle("Downloading BO3 Shader Studio update");
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(0);
    progress.setAutoClose(false);

    bool writeFailed = false;
    QObject::connect(reply, &QNetworkReply::readyRead, [&]{
        const QByteArray bytes = reply->readAll();
        if(output.write(bytes) != bytes.size())
        {
            writeFailed = true;
            reply->abort();
        }
    });
    QObject::connect(reply, &QNetworkReply::downloadProgress, [&](qint64 received, qint64 total){
        if(total > 0 && total <= INT_MAX)
        {
            progress.setRange(0, static_cast<int>(total));
            progress.setValue(static_cast<int>(std::min(received, total)));
        }
        else
        {
            progress.setRange(0, 0);
        }
        progress.setLabelText(QString("Downloading %1\n%2 / %3 MB")
            .arg(release.assetName)
            .arg(received / (1024.0 * 1024.0), 0, 'f', 1)
            .arg(total > 0 ? QString::number(total / (1024.0 * 1024.0), 'f', 1) : QString("?")));
    });
    QObject::connect(&progress, &QProgressDialog::canceled, reply, &QNetworkReply::abort);

    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    timeout.start(5 * 60 * 1000);
    QObject::connect(&timeout, &QTimer::timeout, reply, &QNetworkReply::abort);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();

    // Consume any bytes delivered with finished() but not readyRead().
    const QByteArray tail = reply->readAll();
    if(!tail.isEmpty() && output.write(tail) != tail.size()) writeFailed = true;
    output.flush();
    output.close();

    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const bool canceled = progress.wasCanceled();
    const bool ok = !writeFailed && !canceled && reply->error() == QNetworkReply::NoError && status >= 200 && status < 300;
    const QString networkError = reply->errorString();
    reply->deleteLater();
    progress.close();

    if(!ok)
    {
        QFile::remove(localPath);
        error = canceled ? "Update download canceled."
                         : (writeFailed ? "Could not write the downloaded update to disk."
                                        : QString("Update download failed (HTTP %1): %2").arg(status).arg(networkError));
        return false;
    }
    return true;
}

bool verifySha256(const QString& path,
                  const QString& expectedSha256,
                  QString& actualSha256,
                  QString& error)
{
    QFile file(path);
    if(!file.open(QIODevice::ReadOnly))
    {
        error = QString("Could not open the downloaded update for verification:\n%1").arg(path);
        return false;
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if(!hash.addData(&file))
    {
        error = "Could not calculate the update SHA-256 digest.";
        return false;
    }
    actualSha256 = QString::fromLatin1(hash.result().toHex()).toLower();
    const QString expected = expectedSha256.trimmed().toLower();
    if(expected.isEmpty())
    {
        error = "GitHub did not provide a SHA-256 digest for this release asset. BO3 Shader Studio refuses automatic installation without a digest; use manual Install Update File only if you trust the package.";
        return false;
    }
    if(actualSha256 != expected)
    {
        error = QString("Downloaded update failed SHA-256 verification.\n\nExpected: %1\nActual:   %2\n\nThe file has been rejected.")
            .arg(expected, actualSha256);
        return false;
    }
    return true;
}

} // namespace github_update
