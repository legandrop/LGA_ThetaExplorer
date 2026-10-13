#include "thetaexplorer/FileMetadataPanel.h"
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>

static QString formatSize(qint64 bytes)
{
    if (bytes < 1024)
        return QString("%1 B").arg(bytes);
    else if (bytes < 1024 * 1024)
        return QString("%1 KB").arg(bytes / 1024.0, 0, 'f', 1);
    else if (bytes < 1024 * 1024 * 1024)
        return QString("%1 MB").arg(bytes / (1024.0 * 1024.0), 0, 'f', 1);
    else
        return QString("%1 GB").arg(bytes / (1024.0 * 1024.0 * 1024.0), 0, 'f', 2);
}

FileMetadataPanel::FileMetadataPanel(QWidget* parent)
    : QWidget(parent)
{
    setObjectName("metadataPanel");
    setAttribute(Qt::WA_StyledBackground);
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(10, 8, 10, 8);
    outer->setSpacing(3);

    auto* title = new QLabel("FILE INFO", this);
    title->setObjectName("metaTitle");
    outer->addWidget(title);

    m_nameVal = makeValue("name");
    m_typeVal = makeValue("type");
    m_sizeVal = makeValue("size");
    m_dateVal = makeValue("date");
    m_dimVal = makeValue("dimensions");
    m_pathVal = makeValue("path");
    m_nameVal->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_typeVal->setWordWrap(true);
    m_pathVal->setWordWrap(true);
    m_typeVal->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    m_pathVal->setAlignment(Qt::AlignRight | Qt::AlignTop);
    m_sizeVal->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_dimVal->setAlignment(Qt::AlignRight | Qt::AlignTop);

    auto addRow = [&](QLabel* left, QLabel* right) {
        auto* row = new QHBoxLayout;
        row->setSpacing(8);
        row->addWidget(left, 1);
        row->addWidget(right);
        outer->addLayout(row);
    };
    addRow(m_nameVal, m_sizeVal);
    addRow(m_typeVal, m_dimVal);
    addRow(m_dateVal, m_pathVal);

    clearMetadata();
}

QLabel* FileMetadataPanel::makeValue(const QString& field)
{
    auto* l = new QLabel(this);
    l->setObjectName("metaValue");
    l->setProperty("field", field);
    l->setTextFormat(Qt::PlainText);
    l->setAccessibleName(field);
    l->setIndent(0);
    l->setWordWrap(false);
    l->setTextInteractionFlags(Qt::TextSelectableByMouse);
    return l;
}

void FileMetadataPanel::setDimensions(int width, int height)
{
    // Las dimensiones llegan del catalogo o del JPEG de preview; sin dato se ocultan.
    const bool known = width > 0 && height > 0;
    m_dimVal->setText(known ? QString("%1 × %2").arg(width).arg(height) : QString());
    m_dimVal->setVisible(known);
}

void FileMetadataPanel::setRow(QLabel* val, const QString& text)
{
    if (val) val->setText(text);
}

void FileMetadataPanel::showMetadata(const MediaAssetGroup& group)
{
    if (group.files.size() == 1) {
        const CameraFileInfo& file = group.files.first();
        setRow(m_nameVal, file.name);

        QString type;
        if (file.isVideo) type = "Video (MP4/MOV)";
        else if (file.isRaw) type = "RAW Image (DNG)";
        else {
            int dot = file.name.lastIndexOf('.');
            type = dot >= 0 ? file.name.mid(dot + 1).toUpper() + " Image" : "Image";
        }
        setRow(m_typeVal, type);
        setRow(m_sizeVal, formatSize(file.sizeBytes));
        setRow(m_dateVal, file.creationDate.isValid()
               ? file.creationDate.toString("ddd d MMM · HH:mm")
               : "—");
        setDimensions(file.width, file.height);
        setRow(m_pathVal, file.devicePath.isEmpty() ? "—" : file.devicePath);
        return;
    }

    qint64 totalBytes = 0;
    for (const CameraFileInfo& file : group.files) {
        totalBytes += file.sizeBytes;
    }

    setRow(m_nameVal, group.displayTitle);
    setRow(m_typeVal, group.subtitle);
    setRow(m_sizeVal, formatSize(totalBytes));
    setRow(m_dateVal, group.captureTime.isValid()
           ? group.captureTime.toString("ddd d MMM · HH:mm")
           : "—");
    // La cantidad de brackets ya esta en Type; aca va la resolucion, como en
    // un archivo suelto (antes decia "5 images" debajo de "Size (px)").
    setDimensions(group.representative.width, group.representative.height);
    setRow(m_pathVal, group.representative.devicePath.isEmpty() ? "—" : group.representative.devicePath);
}

void FileMetadataPanel::showMultipleSelection(int count)
{
    setRow(m_nameVal, QString("%1 items selected").arg(count));
    setRow(m_typeVal, "—");
    setRow(m_sizeVal, "—");
    setRow(m_dateVal, "—");
    setDimensions(0, 0);
    setRow(m_pathVal, "—");
}

void FileMetadataPanel::clearMetadata()
{
    setRow(m_nameVal, "—");
    setRow(m_typeVal, "—");
    setRow(m_sizeVal, "—");
    setRow(m_dateVal, "—");
    setDimensions(0, 0);
    setRow(m_pathVal, "—");
}
