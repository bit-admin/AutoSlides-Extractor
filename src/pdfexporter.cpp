#include "pdfexporter.h"

#include <QImage>
#include <QPdfWriter>
#include <QPainter>
#include <QBuffer>
#include <QPageSize>
#include <QMarginsF>
#include <QRect>
#include <QSize>
#include <QDebug>

namespace {

// Centre-crop to the target aspect if the source is wider than it.
void coverCropIfWider(QImage& img, double targetAspect)
{
    if (img.isNull() || img.height() <= 0 || img.width() <= 0) return;
    const double srcAspect = static_cast<double>(img.width()) / img.height();
    if (srcAspect > targetAspect + 0.001) {
        int newWidth = qRound(img.height() * targetAspect);
        int x = (img.width() - newWidth) / 2;
        img = img.copy(x, 0, newWidth, img.height());
    }
}

} // namespace

int PdfExporter::exportToPdf(const QStringList& imagePaths,
                             const QString& outputPath,
                             const PdfExportOptions& options,
                             const std::function<void()>& onImageProcessed)
{
    // Locate the first loadable image to size the document.
    QImage firstImage;
    for (const QString& path : imagePaths) {
        firstImage = QImage(path);
        if (!firstImage.isNull()) break;
    }
    if (firstImage.isNull()) {
        return 0;
    }

    QSize fixedPageSize;
    if (options.reduceSize) {
        int pageH = options.targetHeight > 0 ? options.targetHeight : firstImage.height();
        int pageW = qRound(pageH * options.targetAspect);
        fixedPageSize = QSize(pageW, pageH);
    }

    QPdfWriter writer(outputPath);
    writer.setResolution(96);
    writer.setPageMargins(QMarginsF(0, 0, 0, 0));
    if (options.reduceSize) {
        writer.setPageSize(QPageSize(fixedPageSize, QPageSize::Point));
    } else {
        writer.setPageSize(QPageSize(firstImage.size(), QPageSize::Point));
    }

    QPainter painter(&writer);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);

    bool isFirstPage = true;
    int pages = 0;

    for (const QString& imagePath : imagePaths) {
        QImage image(imagePath);
        if (image.isNull()) {
            qWarning() << "PdfExporter: Failed to load image:" << imagePath;
            continue;
        }

        if (!isFirstPage) {
            writer.newPage();
        }

        if (options.reduceSize) {
            coverCropIfWider(image, options.targetAspect);

            if (options.targetHeight > 0 && image.height() > options.targetHeight) {
                image = image.scaledToHeight(options.targetHeight, Qt::SmoothTransformation);
            }

            QBuffer buffer;
            buffer.open(QIODevice::WriteOnly);
            image.save(&buffer, "JPEG", options.jpegQuality);
            buffer.close();
            buffer.open(QIODevice::ReadOnly);
            image.loadFromData(buffer.data());

            QRect pageRect(0, 0, writer.width(), writer.height());
            painter.fillRect(pageRect, Qt::white);

            QSize fitted = image.size().scaled(pageRect.size(), Qt::KeepAspectRatio);
            QRect dst((pageRect.width()  - fitted.width())  / 2,
                      (pageRect.height() - fitted.height()) / 2,
                      fitted.width(), fitted.height());
            painter.drawImage(dst, image);
        } else {
            writer.setPageSize(QPageSize(image.size(), QPageSize::Point));
            QRect pageRect(0, 0, writer.width(), writer.height());
            painter.drawImage(pageRect, image);
        }

        isFirstPage = false;
        pages++;
        if (onImageProcessed) {
            onImageProcessed();
        }
    }

    painter.end();
    return pages;
}
