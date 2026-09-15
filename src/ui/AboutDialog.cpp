#include "AboutDialog.h"

#include <QDialogButtonBox>
#include <QFont>
#include <QHBoxLayout>
#include <QIcon>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPixmap>
#include <QPushButton>
#include <QVBoxLayout>

#include "BuildInfo.h" // generated at build time; see cmake/GenerateBuildInfo.cmake
#include "core/VersionCompare.h"

AboutDialog::AboutDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("About SMB Manager"));

    auto *iconLabel = new QLabel(this);
    iconLabel->setPixmap(QIcon(QStringLiteral(":/icons/app/app.svg")).pixmap(QSize(256, 256)));

    auto *nameLabel = new QLabel(tr("SMB Manager"), this);
    QFont nameFont = nameLabel->font();
    nameFont.setBold(true);
    nameFont.setPointSize(nameFont.pointSize() + 4);
    nameLabel->setFont(nameFont);

    auto *versionLabel = new QLabel(tr("v%1").arg(QStringLiteral(APP_VERSION)), this);
    versionLabel->setObjectName(QStringLiteral("ad.versionLabel"));

    auto *buildDateLabel = new QLabel(tr("Built %1").arg(QStringLiteral(APP_BUILD_DATE)), this);
    buildDateLabel->setObjectName(QStringLiteral("ad.buildDateLabel"));

    auto *copyrightLabel = new QLabel(tr("© Copyright 2026 Dan Brakeley"), this);

    auto *githubLabel = new QLabel(
        tr("<a href=\"https://github.com/danbrakeley/smbmgr\">github.com/danbrakeley/smbmgr</a>"),
        this);
    githubLabel->setObjectName(QStringLiteral("ad.githubLink"));
    githubLabel->setTextFormat(Qt::RichText);
    githubLabel->setTextInteractionFlags(Qt::TextBrowserInteraction);
    githubLabel->setOpenExternalLinks(true);

    auto *checkUpdatesButton = new QPushButton(tr("Check for Updates"), this);
    checkUpdatesButton->setObjectName(QStringLiteral("ad.checkUpdatesButton"));

    auto *updateStatusLabel = new QLabel(this);
    updateStatusLabel->setObjectName(QStringLiteral("ad.updateStatusLabel"));
    updateStatusLabel->setTextFormat(Qt::RichText);
    updateStatusLabel->setTextInteractionFlags(Qt::TextBrowserInteraction);
    updateStatusLabel->setOpenExternalLinks(true);

    connect(checkUpdatesButton, &QPushButton::clicked, this,
            [this, checkUpdatesButton, updateStatusLabel]() {
        checkUpdatesButton->setEnabled(false);
        updateStatusLabel->setText(tr("Looking for updates..."));

        auto *manager = new QNetworkAccessManager(this);

        // Not /releases/latest: that endpoint skips releases marked
        // "pre-release" (and 404s if every release is one). The plain list is
        // sorted newest-first and includes them.
        QNetworkRequest request(
            QUrl(QStringLiteral("https://api.github.com/repos/danbrakeley/smbmgr/releases")));
        // GitHub's API rejects requests with no User-Agent header (403).
        request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("smbmgr"));
        request.setTransferTimeout(10000);

        QNetworkReply *reply = manager->get(request);
        connect(reply, &QNetworkReply::finished, this,
                [reply, manager, checkUpdatesButton, updateStatusLabel]() {
            reply->deleteLater();
            manager->deleteLater();
            checkUpdatesButton->setEnabled(true);

            if (reply->error() != QNetworkReply::NoError) {
                updateStatusLabel->setText(tr("Couldn't check for updates"));
                return;
            }

            const QJsonArray releases = QJsonDocument::fromJson(reply->readAll()).array();
            const QString tag = releases.isEmpty()
                                     ? QString()
                                     : releases.first().toObject().value(QStringLiteral("tag_name")).toString();
            if (tag.isEmpty()) {
                updateStatusLabel->setText(tr("Couldn't check for updates"));
                return;
            }

            const QString tagLink =
                tr("<a href=\"https://github.com/danbrakeley/smbmgr/releases/tag/%1\">%1</a>").arg(tag.toHtmlEscaped());

            if (versioncompare::isNewer(tag, QStringLiteral(APP_VERSION))) {
                updateStatusLabel->setText(tr("Update available: %1").arg(tagLink));
            } else {
                updateStatusLabel->setText(tr("Latest version: %1").arg(tagLink));
            }
        });
    });

    auto *updateRowLayout = new QVBoxLayout;
    updateRowLayout->addWidget(checkUpdatesButton);
    updateRowLayout->addWidget(updateStatusLabel);

    auto *textLayout = new QVBoxLayout;
    textLayout->addWidget(nameLabel);
    textLayout->addWidget(versionLabel);
    textLayout->addWidget(buildDateLabel);
    textLayout->addSpacing(12);
    textLayout->addWidget(copyrightLabel);
    textLayout->addWidget(githubLabel);
    textLayout->addSpacing(12);
    textLayout->addLayout(updateRowLayout);
    textLayout->addStretch(1);

    auto *topLayout = new QHBoxLayout;
    topLayout->addWidget(iconLabel);
    topLayout->addLayout(textLayout, /*stretch*/ 1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok, this);
    buttons->setObjectName(QStringLiteral("ad.okButton"));
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(topLayout);
    layout->addWidget(buttons);
}
