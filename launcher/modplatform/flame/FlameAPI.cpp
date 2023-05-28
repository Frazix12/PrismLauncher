// SPDX-FileCopyrightText: 2023 flowln <flowlnlnln@gmail.com>
//
// SPDX-License-Identifier: GPL-3.0-only

#include "FlameAPI.h"
#include "FlameModIndex.h"

#include "Application.h"
#include "BuildConfig.h"
#include "Json.h"
#include "net/NetJob.h"
#include "net/Upload.h"

Task::Ptr FlameAPI::matchFingerprints(const QList<uint>& fingerprints, QByteArray* response)
{
    auto netJob = makeShared<NetJob>(QString("Flame::MatchFingerprints"), APPLICATION->network());

    QJsonObject body_obj;
    QJsonArray fingerprints_arr;
    for (auto& fp : fingerprints) {
        fingerprints_arr.append(QString("%1").arg(fp));
    }

    body_obj["fingerprints"] = fingerprints_arr;

    QJsonDocument body(body_obj);
    auto body_raw = body.toJson();

    netJob->addNetAction(Net::Upload::makeByteArray(QString("https://api.curseforge.com/v1/fingerprints"), response, body_raw));

    QObject::connect(netJob.get(), &NetJob::finished, [response] { delete response; });

    return netJob;
}

auto FlameAPI::getModFileChangelog(int modId, int fileId) -> QString
{
    QEventLoop lock;
    QString changelog;

    auto netJob = makeShared<NetJob>(QString("Flame::FileChangelog"), APPLICATION->network());
    auto response = std::make_shared<QByteArray>();
    netJob->addNetAction(Net::Download::makeByteArray(
        QString("https://api.curseforge.com/v1/mods/%1/files/%2/changelog")
            .arg(QString::fromStdString(std::to_string(modId)), QString::fromStdString(std::to_string(fileId))),
        response.get()));

    QObject::connect(netJob.get(), &NetJob::succeeded, [&netJob, response, &changelog] {
        QJsonParseError parse_error{};
        QJsonDocument doc = QJsonDocument::fromJson(*response, &parse_error);
        if (parse_error.error != QJsonParseError::NoError) {
            qWarning() << "Error while parsing JSON response from Flame::FileChangelog at " << parse_error.offset
                       << " reason: " << parse_error.errorString();
            qWarning() << *response;

            netJob->failed(parse_error.errorString());
            return;
        }

        changelog = Json::ensureString(doc.object(), "data");
    });

    QObject::connect(netJob.get(), &NetJob::finished, [&lock] { lock.quit(); });

    netJob->start();
    lock.exec();

    return changelog;
}

auto FlameAPI::getModDescription(int modId) -> QString
{
    QEventLoop lock;
    QString description;

    auto netJob = makeShared<NetJob>(QString("Flame::ModDescription"), APPLICATION->network());
    auto response = std::make_shared<QByteArray>();
    netJob->addNetAction(Net::Download::makeByteArray(
        QString("https://api.curseforge.com/v1/mods/%1/description").arg(QString::number(modId)), response.get()));

    QObject::connect(netJob.get(), &NetJob::succeeded, [&netJob, response, &description] {
        QJsonParseError parse_error{};
        QJsonDocument doc = QJsonDocument::fromJson(*response, &parse_error);
        if (parse_error.error != QJsonParseError::NoError) {
            qWarning() << "Error while parsing JSON response from Flame::ModDescription at " << parse_error.offset
                       << " reason: " << parse_error.errorString();
            qWarning() << *response;

            netJob->failed(parse_error.errorString());
            return;
        }

        description = Json::ensureString(doc.object(), "data");
    });

    QObject::connect(netJob.get(), &NetJob::finished, [&lock] { lock.quit(); });

    netJob->start();
    lock.exec();

    return description;
}

auto FlameAPI::getLatestVersion(VersionSearchArgs&& args) -> ModPlatform::IndexedVersion
{
    auto versions_url_optional = getVersionsURL(args);
    if (!versions_url_optional.has_value())
        return {};

    auto versions_url = versions_url_optional.value();

    QEventLoop loop;

    auto netJob = makeShared<NetJob>(QString("Flame::GetLatestVersion(%1)").arg(args.pack.name), APPLICATION->network());
    auto response = std::make_shared<QByteArray>();
    ModPlatform::IndexedVersion ver;

    netJob->addNetAction(Net::Download::makeByteArray(versions_url, response.get()));

    QObject::connect(netJob.get(), &NetJob::succeeded, [response, args, &ver] {
        QJsonParseError parse_error{};
        QJsonDocument doc = QJsonDocument::fromJson(*response, &parse_error);
        if (parse_error.error != QJsonParseError::NoError) {
            qWarning() << "Error while parsing JSON response from latest mod version at " << parse_error.offset
                       << " reason: " << parse_error.errorString();
            qWarning() << *response;
            return;
        }

        try {
            auto obj = Json::requireObject(doc);
            auto arr = Json::requireArray(obj, "data");

            QJsonObject latest_file_obj;
            ModPlatform::IndexedVersion ver_tmp;

            for (auto file : arr) {
                auto file_obj = Json::requireObject(file);
                auto file_tmp = FlameMod::loadIndexedPackVersion(file_obj);
                if(file_tmp.date > ver_tmp.date) {
                    ver_tmp = file_tmp;
                    latest_file_obj = file_obj;
                }
            }

            ver = FlameMod::loadIndexedPackVersion(latest_file_obj);
        } catch (Json::JsonException& e) {
            qCritical() << "Failed to parse response from a version request.";
            qCritical() << e.what();
            qDebug() << doc;
        }
    });

    QObject::connect(netJob.get(), &NetJob::finished, [&loop] { loop.quit(); });

    netJob->start();

    loop.exec();

    return ver;
}

Task::Ptr FlameAPI::getProjects(QStringList addonIds, QByteArray* response) const
{
    auto netJob = makeShared<NetJob>(QString("Flame::GetProjects"), APPLICATION->network());

    QJsonObject body_obj;
    QJsonArray addons_arr;
    for (auto& addonId : addonIds) {
        addons_arr.append(addonId);
    }

    body_obj["modIds"] = addons_arr;

    QJsonDocument body(body_obj);
    auto body_raw = body.toJson();

    netJob->addNetAction(Net::Upload::makeByteArray(QString("https://api.curseforge.com/v1/mods"), response, body_raw));

    QObject::connect(netJob.get(), &NetJob::finished, [response] { delete response; });
    QObject::connect(netJob.get(), &NetJob::failed, [body_raw] { qDebug() << body_raw; });

    return netJob;
}

Task::Ptr FlameAPI::getFiles(const QStringList& fileIds, QByteArray* response) const
{
    auto netJob = makeShared<NetJob>(QString("Flame::GetFiles"), APPLICATION->network());

    QJsonObject body_obj;
    QJsonArray files_arr;
    for (auto& fileId : fileIds) {
        files_arr.append(fileId);
    }

    body_obj["fileIds"] = files_arr;

    QJsonDocument body(body_obj);
    auto body_raw = body.toJson();

    netJob->addNetAction(Net::Upload::makeByteArray(QString("https://api.curseforge.com/v1/mods/files"), response, body_raw));

    QObject::connect(netJob.get(), &NetJob::finished, [response] { delete response; });
    QObject::connect(netJob.get(), &NetJob::failed, [body_raw] { qDebug() << body_raw; });

    return netJob;
}

// https://docs.curseforge.com/?python#tocS_ModsSearchSortField
static QList<ResourceAPI::SortingMethod> s_sorts = { { 1, "Featured", QObject::tr("Sort by Featured") },
                                                     { 2, "Popularity", QObject::tr("Sort by Popularity") },
                                                     { 3, "LastUpdated", QObject::tr("Sort by Last Updated") },
                                                     { 4, "Name", QObject::tr("Sort by Name") },
                                                     { 5, "Author", QObject::tr("Sort by Author") },
                                                     { 6, "TotalDownloads", QObject::tr("Sort by Downloads") },
                                                     { 7, "Category", QObject::tr("Sort by Category") },
                                                     { 8, "GameVersion", QObject::tr("Sort by Game Version") } };

QList<ResourceAPI::SortingMethod> FlameAPI::getSortingMethods() const
{
    return s_sorts;
}
