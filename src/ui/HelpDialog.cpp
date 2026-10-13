#include "thetaexplorer/HelpDialog.h"
#include "thetaexplorer/AppPaths.h"
#include "thetaexplorer/Logger.h"
#include "lgaupdate/LgaUpdateService.h"
#include <QApplication>
#include <QCheckBox>
#include <QPushButton>
#include <QSettings>
#include <QDesktopServices>
#include <QDir>
#include <QEnterEvent>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QUrl>
#include <QVBoxLayout>

namespace {

// Colores del Help (convencion LGA: constantes k* al principio del .cpp).
constexpr auto kHelpAppNameColor     = "rgb(127, 98, 170)";   // violeta de marca
constexpr auto kHelpTitleColor       = "#cccccc";
constexpr auto kHelpVersionColor     = "#8f8f8f";
constexpr auto kHelpAuthorColor      = "#9d9d9d";
constexpr auto kHelpDescriptionColor = "#888888";
constexpr auto kHelpLinkColor        = "#8676a3";   // violeta apagado, menos que el nombre
constexpr auto kHelpLinkHoverColor   = "#a796c6";
constexpr auto kHelpSeparatorColor   = "#2f2f2f";

constexpr auto kWebsiteText = "lega.com.ar";
constexpr auto kWebsiteUrl  = "https://lega.com.ar";

// Texto clickeable: abre una URL, se aclara y se subraya con el mouse encima.
class LinkLabel : public QLabel
{
public:
    LinkLabel(const QString& text, const QUrl& url, int fontPx, QWidget* parent)
        : QLabel(text, parent), m_url(url), m_fontPx(fontPx)
    {
        setCursor(Qt::PointingHandCursor);
        applyStyle(false);
    }

protected:
    void enterEvent(QEnterEvent* e) override { applyStyle(true); QLabel::enterEvent(e); }
    void leaveEvent(QEvent* e) override { applyStyle(false); QLabel::leaveEvent(e); }
    void mouseReleaseEvent(QMouseEvent* e) override
    {
        if (e->button() == Qt::LeftButton && rect().contains(e->position().toPoint())) {
            if (!QDesktopServices::openUrl(m_url)) {
                LOGW("ui") << "No se pudo abrir " << m_url.toString();
            }
        }
        QLabel::mouseReleaseEvent(e);
    }

private:
    void applyStyle(bool hover)
    {
        setStyleSheet(QStringLiteral("QLabel { color: %1; font-size: %2px; text-decoration: %3; }")
                          .arg(hover ? kHelpLinkHoverColor : kHelpLinkColor)
                          .arg(m_fontPx)
                          .arg(hover ? "underline" : "none"));
    }

    QUrl m_url;
    int  m_fontPx;
};

QString withTilde(const QString& path)
{
    const QString home = QDir::homePath();
    return path.startsWith(home) ? QStringLiteral("~") + path.mid(home.size()) : path;
}

QFrame* separator(QWidget* parent)
{
    auto* line = new QFrame(parent);
    line->setFixedHeight(2);
    line->setStyleSheet(QStringLiteral("background-color: %1; border: none;").arg(kHelpSeparatorColor));
    return line;
}

QLabel* sectionTitle(const QString& text, QWidget* parent)
{
    auto* l = new QLabel(text, parent);
    l->setStyleSheet(QStringLiteral("color: %1; font-size: 15px; font-weight: 500;").arg(kHelpTitleColor));
    return l;
}

QLabel* description(const QString& text, QWidget* parent)
{
    auto* l = new QLabel(text, parent);
    l->setStyleSheet(QStringLiteral("color: %1; font-size: 13px;").arg(kHelpDescriptionColor));
    return l;
}

// Una tecla dibujada como tecla.
QLabel* keyCap(const QString& text, QWidget* parent)
{
    auto* l = new QLabel(text, parent);
    l->setObjectName("helpKey");
    l->setAlignment(Qt::AlignCenter);
    l->setMinimumWidth(24);
    return l;
}

QWidget* keyCombo(const QStringList& keys, QWidget* parent)
{
    auto* w = new QWidget(parent);
    auto* row = new QHBoxLayout(w);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(4);
    for (const QString& k : keys) {
        row->addWidget(keyCap(k, w));
    }
    row->addStretch();
    return w;
}

} // namespace

HelpDialog::HelpDialog(const QString& downloadFolder, QWidget* parent, LgaUpdateService* updates)
    : QDialog(parent)
{
    setObjectName("helpDialog");
    setWindowTitle("Help");
    setModal(true);
    setFixedWidth(560);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(26, 22, 26, 24);
    root->setSpacing(0);

    // ---- Encabezado ----
    auto* title = new QLabel(this);
    title->setTextFormat(Qt::RichText);
    title->setText(QStringLiteral("<span style='color:%1; font-weight:500;'>%2</span>"
                                  "&nbsp;&nbsp;<span style='color:%3;'>v%4</span>")
                       .arg(kHelpAppNameColor, QApplication::applicationDisplayName(),
                            kHelpVersionColor, QApplication::applicationVersion()));
    title->setStyleSheet(QStringLiteral("font-size: 20px;"));
    root->addWidget(title);

    root->addSpacing(6);
    auto* author = new QLabel("Developed by Lega Pugliese", this);
    author->setStyleSheet(QStringLiteral("color: %1; font-size: 14px;").arg(kHelpAuthorColor));
    root->addWidget(author);

    root->addSpacing(2);
    auto* linkRow = new QHBoxLayout();
    linkRow->setContentsMargins(0, 0, 0, 0);
    linkRow->addWidget(new LinkLabel(kWebsiteText, QUrl(kWebsiteUrl), 14, this));
    linkRow->addStretch();
    root->addLayout(linkRow);

    if (updates) {
        root->addSpacing(18);
        root->addWidget(separator(this));
        root->addSpacing(16);
        root->addWidget(sectionTitle("Updates", this));
        root->addSpacing(10);
        auto* updateRow = new QHBoxLayout();
        auto* automatic = new QCheckBox("Check for updates automatically", this);
        automatic->setObjectName("automaticUpdatesCheck");
        QSettings settings(AppPaths::settingsFile(), QSettings::IniFormat);
        automatic->setChecked(settings.value("Updates/AutomaticChecks", true).toBool());
        connect(automatic, &QCheckBox::toggled, this, [](bool enabled) {
            QSettings settings(AppPaths::settingsFile(), QSettings::IniFormat);
            settings.setValue("Updates/AutomaticChecks", enabled);
        });
        auto* check = new QPushButton(this);
        check->setObjectName("checkUpdatesBtn");
        check->setAutoDefault(false);
        check->setAttribute(Qt::WA_MacShowFocusRect, false);
        const auto refreshCheck = [check, updates] {
            check->setText(updates->isBusy() ? "Checking..." : "Check now");
            check->setEnabled(!updates->isBusy());
        };
        connect(updates, &LgaUpdateService::checkStarted, this, refreshCheck);
        connect(updates, &LgaUpdateService::checkFinished, this, [check] {
            check->setText("Check now");
            check->setEnabled(true);
        });
        connect(check, &QPushButton::clicked, this, [updates] { updates->checkForUpdates(true); });
        refreshCheck();
        updateRow->addWidget(automatic);
        updateRow->addStretch();
        updateRow->addWidget(check);
        root->addLayout(updateRow);
        root->addSpacing(8);
        auto* updateKeys = description("Update dialog: U — update, R — remind later, S — skip, C / Esc — cancel.", this);
        updateKeys->setWordWrap(true);
        root->addWidget(updateKeys);
    }

    // ---- Atajos ----
    root->addSpacing(18);
    root->addWidget(separator(this));
    root->addSpacing(16);
    root->addWidget(sectionTitle("Shortcuts", this));
    root->addSpacing(10);

    auto* keys = new QGridLayout();
    keys->setContentsMargins(0, 0, 0, 0);
    keys->setHorizontalSpacing(14);
    keys->setVerticalSpacing(7);
    keys->setColumnMinimumWidth(0, 150);
    int row = 0;
    keys->addWidget(keyCombo({"Arrows"}, this), row, 0);
    keys->addWidget(description("Move between items", this), row++, 1);
    keys->addWidget(keyCombo({"Shift", "Arrows"}, this), row, 0);
    keys->addWidget(description("Extend the selection", this), row++, 1);
    keys->addWidget(keyCombo({QString::fromUtf8("⌘"), "A"}, this), row, 0);
    keys->addWidget(description("Select all items", this), row++, 1);
    keys->addWidget(keyCombo({"Esc"}, this), row, 0);
    keys->addWidget(description("Clear the selection", this), row++, 1);
    keys->addWidget(keyCombo({QString::fromUtf8("⌘"), "?"}, this), row, 0);
    keys->addWidget(description("Open this Help", this), row++, 1);
    keys->addWidget(description("Click empty space", this), row, 0);
    keys->addWidget(description("Clear the selection", this), row++, 1);
    keys->setColumnStretch(1, 1);
    root->addLayout(keys);

    // ---- Rutas ----
    root->addSpacing(18);
    root->addWidget(separator(this));
    root->addSpacing(16);
    root->addWidget(sectionTitle("Paths", this));
    root->addSpacing(10);

    auto* paths = new QGridLayout();
    paths->setContentsMargins(0, 0, 0, 0);
    paths->setHorizontalSpacing(14);
    paths->setVerticalSpacing(7);
    paths->setColumnMinimumWidth(0, 90);
    const QList<QPair<QString, QString>> entries = {
        {"Downloads", downloadFolder},
        {"Logs",      QFileInfo(thetaexplorer::Logger::instance().logFilePath()).absolutePath()},
        {"Settings",  AppPaths::settingsDirectory()},
    };
    row = 0;
    for (const auto& entry : entries) {
        paths->addWidget(description(entry.first, this), row, 0, Qt::AlignTop);
        auto* link = new LinkLabel(withTilde(entry.second), QUrl::fromLocalFile(entry.second), 13, this);
        link->setWordWrap(true);
        paths->addWidget(link, row++, 1);
    }
    paths->setColumnStretch(1, 1);
    root->addLayout(paths);

    // Respetar el alto del contenido al ancho fijo: no comprimir atajos ni rutas envueltas.
    ensurePolished();
    const int contentHeight = root->totalHeightForWidth(width());
    if (contentHeight > 0) setMinimumHeight(contentHeight);
}
