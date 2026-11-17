#include "thetaexplorer/ConfirmDialog.h"
#include "thetaexplorer/ColorUtils.h"
#include "thetaexplorer/UiIcons.h"
#include <QHBoxLayout>
#include <QLabel>
#include <QKeySequence>
#include <QPushButton>
#include <QTextBrowser>
#include <QPlainTextEdit>
#include <QVBoxLayout>

namespace {
constexpr int kDialogWidth    = 460;
constexpr int kHeaderIconSize = 44;
constexpr int kHintIconSize   = 26;
constexpr auto kHeaderIconColor = "#9a9a9a";
constexpr auto kHintIconColor   = "#6f6f6f";
}

ConfirmDialog::ConfirmDialog(const Spec& spec, QWidget* parent)
    : QDialog(parent)
    , m_result(spec.escapeIndex)
{
    setObjectName("confirmDialog");
    setWindowTitle(spec.title);
    setModal(true);
    setFixedWidth(qMax(kDialogWidth, spec.buttons.size() * 140 + 44));

    const qreal dpr = devicePixelRatioF();

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(22, 22, 22, 20);
    root->setSpacing(0);

    // ---- Encabezado: icono + titulo + subtitulo ----
    auto* header = new QHBoxLayout();
    header->setSpacing(14);

    auto* icon = new QLabel(this);
    icon->setFixedSize(kHeaderIconSize, kHeaderIconSize);
    icon->setAlignment(Qt::AlignCenter);
    icon->setPixmap(UiIcons::pixmap(spec.iconName, 36, QColor(kHeaderIconColor), dpr));
    header->addWidget(icon, 0, Qt::AlignVCenter);

    auto* texts = new QVBoxLayout();
    texts->setSpacing(3);
    auto* title = new QLabel(spec.title, this);
    title->setObjectName("confirmTitle");
    title->setWordWrap(true);
    auto* subtitle = new QLabel(spec.subtitle, this);
    subtitle->setObjectName("confirmSubtitle");
    subtitle->setWordWrap(true);
    subtitle->setTextInteractionFlags(Qt::TextSelectableByMouse);
    texts->addWidget(title);
    texts->addWidget(subtitle);
    header->addLayout(texts, 1);
    root->addLayout(header);

    if (!spec.contentHtml.isEmpty()) {
        root->addSpacing(14);
        auto* notes = new QTextBrowser(this);
        notes->setObjectName("confirmNotes");
        notes->setOpenExternalLinks(false);
        notes->setOpenLinks(false);
        notes->setHtml(spec.contentHtml);
        notes->setFixedHeight(200);
        root->addWidget(notes);
    }
    if (!spec.details.isEmpty()) {
        root->addSpacing(10);
        auto* toggle = new QPushButton("Details", this);
        toggle->setObjectName("confirmDetailsButton");
        auto* details = new QPlainTextEdit(spec.details, this);
        details->setObjectName("confirmDetails");
        details->setReadOnly(true);
        details->setFixedHeight(140);
        details->hide();
        connect(toggle, &QPushButton::clicked, this, [this, toggle, details] {
            const bool expanded = !details->isVisible();
            details->setVisible(expanded);
            toggle->setText(expanded ? "Hide details" : "Details");
            adjustSize();
        });
        root->addWidget(toggle, 0, Qt::AlignLeft);
        root->addWidget(details);
    }

    // ---- Fila de atajos ----
    if (!spec.hintHtml.isEmpty()) {
        root->addSpacing(18);
        auto* hintRow = new QHBoxLayout();
        hintRow->setSpacing(8);
        auto* kb = new QLabel(this);
        kb->setFixedSize(kHintIconSize, kHintIconSize);
        kb->setPixmap(UiIcons::pixmap("keyboard", kHintIconSize, QColor(kHintIconColor), dpr));
        hintRow->addWidget(kb, 0, Qt::AlignVCenter);

        // Las teclas van en violeta, como en MediaTools.
        QString html = spec.hintHtml;
        html.replace("<b>", QStringLiteral("<span style='color:%1'><b>").arg(ColorUtils::VIOLETA_CLARO));
        html.replace("</b>", "</b></span>");
        auto* hint = new QLabel(html, this);
        hint->setObjectName("confirmHint");
        hint->setTextFormat(Qt::RichText);
        hint->setWordWrap(true);
        // Sin alineacion: un QLabel con wordWrap alineado pierde su heightForWidth.
        hintRow->addWidget(hint, 1);
        root->addLayout(hintRow);
    }

    // ---- Botones: mismo ancho, sin borde ----
    root->addSpacing(14);
    auto* buttons = new QHBoxLayout();
    buttons->setSpacing(10);
    QPushButton* defaultButton = nullptr;
    for (int i = 0; i < spec.buttons.size(); ++i) {
        const Button& b = spec.buttons.at(i);
        auto* btn = new QPushButton(b.text, this);
        btn->setObjectName("confirmButton");
        if (b.shortcut != Qt::Key_unknown) btn->setShortcut(QKeySequence(b.shortcut));
        switch (b.style) {
            case ButtonStyle::Action: btn->setProperty("variant", "action"); break;
            case ButtonStyle::Danger: btn->setProperty("variant", "danger"); break;
            case ButtonStyle::Neutral: btn->setProperty("variant", "neutral"); break;
        }
        // En macOS los botones no toman foco con Tab salvo que se pida explicito. Los
        // destructivos quedan fuera del Tab: sin contorno de foco, Tab + Espacio borraria
        // sin que se vea a donde fue el foco.
        btn->setFocusPolicy(spec.noFocusIndexes.contains(i) ? Qt::NoFocus : Qt::StrongFocus);
        const bool isDefault = (i == spec.defaultIndex);
        btn->setAutoDefault(isDefault);
        btn->setDefault(isDefault);
        if (isDefault) {
            defaultButton = btn;
        }
        connect(btn, &QPushButton::clicked, this, [this, i]() {
            m_result = i;
            accept();
        });
        buttons->addWidget(btn, 1);
    }
    root->addLayout(buttons);

    if (defaultButton) {
        defaultButton->setFocus();
    }
}

int ConfirmDialog::ask(QWidget* parent, const Spec& spec)
{
    ConfirmDialog dlg(spec, parent);
    dlg.exec();
    return dlg.m_result;
}
