#ifndef PDFEXPORTER_H
#define PDFEXPORTER_H

#include <QString>
#include <QStringList>
#include <functional>

/**
 * @brief Options controlling PDF rendering.
 *
 * When reduceSize is false the PDF uses each image's native pixel size as the
 * page size (lossless passthrough). When true, pages are a fixed size derived
 * from targetHeight/targetAspect, images are cover-cropped to the aspect ratio,
 * downscaled to targetHeight, and re-encoded as JPEG at jpegQuality.
 */
struct PdfExportOptions {
    bool reduceSize = false;
    int targetHeight = 0;            // 0 = keep first image's native height
    int jpegQuality = 85;            // 1-100, used only when reduceSize
    double targetAspect = 16.0 / 9.0; // page aspect when reduceSize
};

/**
 * @brief Pure (UI-free) renderer of an ordered image list into a single PDF.
 *
 * Extracted from PdfMakerDialog::onMakePdf so the dialog only handles folder
 * selection, file dialogs and progress display. No Qt widgets or QApplication
 * dependency — progress is reported via the onImageProcessed callback, which
 * the caller uses to advance its progress bar (and pump the event loop).
 */
class PdfExporter
{
public:
    /**
     * @param imagePaths Ordered full paths of images, one per PDF page.
     * @param outputPath Destination .pdf path.
     * @param options Rendering options.
     * @param onImageProcessed Invoked once per successfully rendered page.
     * @return Number of pages written (0 if no valid images were found).
     */
    static int exportToPdf(const QStringList& imagePaths,
                           const QString& outputPath,
                           const PdfExportOptions& options,
                           const std::function<void()>& onImageProcessed = {});
};

#endif // PDFEXPORTER_H
