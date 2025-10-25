<div align="center">
  <img src="docs/icon.png" alt="AutoSlides Extractor 标志" width="128" />

  # AutoSlides Extractor

  <p><strong>从课程、讲座和演示视频中自动提取清晰幻灯片，支持计算机视觉检测、pHash 清理、<br>机器学习过滤、人工复核、无损裁剪和 PDF 导出。</strong></p>

  <p>
    <a href="https://github.com/bit-admin/AutoSlides-Extractor/releases">
      <img src="https://img.shields.io/github/v/release/bit-admin/AutoSlides-Extractor?color=blue" alt="最新版本" />
    </a>
    <img src="https://img.shields.io/badge/platform-macOS%20%7C%20Windows%20%7C%20Linux-green" alt="平台" />
    <img src="https://github.com/bit-admin/AutoSlides-Extractor/actions/workflows/build-windows.yml/badge.svg" alt="Windows 构建状态" />
  </p>
  <p>
    <img src="https://img.shields.io/badge/C%2B%2B-17-00599C?style=flat-square&logo=c%2B%2B" alt="C++17" />
    <img src="https://img.shields.io/badge/Qt-6-41CD52?style=flat-square&logo=qt" alt="Qt 6" />
    <img src="https://img.shields.io/badge/OpenCV-4.x-5C3EE8?style=flat-square&logo=opencv" alt="OpenCV 4.x" />
    <img src="https://img.shields.io/badge/FFmpeg-enabled-007808?style=flat-square&logo=ffmpeg" alt="FFmpeg" />
  </p>
  <p>
    <a href="README.md">English</a> | 简体中文
  </p>
</div>

## 目录

- [概览](#概览)
- [2.0.0 新增内容](#200-新增内容)
- [功能特性](#功能特性)
- [下载与安装](#下载与安装)
- [快速开始](#快速开始)
- [用户指南](#用户指南)
- [处理设置](#处理设置)
- [pHash 后处理](#phash-后处理)
- [ML 幻灯片分类](#ml-幻灯片分类)
- [Auto Crop 自动裁剪](#auto-crop-自动裁剪)
- [Slides Review 幻灯片复核](#slides-review-幻灯片复核)
- [Slides Export PDF 导出](#slides-export-pdf-导出)
- [命令行指南](#命令行指南)
- [输出文件与元数据](#输出文件与元数据)
- [工作原理](#工作原理)
- [从源码构建](#从源码构建)
- [故障排查](#故障排查)
- [实用建议](#实用建议)
- [相关项目](#相关项目)
- [许可证](#许可证)

## 概览

AutoSlides Extractor 是一款原生桌面应用，用于把录播课程、在线课堂、会议演讲、屏幕录制和演示视频转换成独立的幻灯片图片。

它不是按固定时间间隔粗暴截帧，而是分析视频帧之间的结构相似度，只保存真正发生内容变化且稳定的幻灯片。提取完成后，应用还能用 pHash 删除重复页和排除页，用 ONNX MobileNetV4 模型识别非幻灯片内容，提供完整的复核、恢复、删除、裁剪和 PDF 导出流程。

所有处理都在本机完成。视频解码、图像比较、机器学习推理、幻灯片复核、裁剪和 PDF 生成都运行在你的电脑上。

## 2.0.0 新增内容

2.0.0 版本引入了媒体时间线元数据追踪、智能后处理自动裁剪以及优化的 Windows 发布包：

- **时间线 (`timeline.json`)**：在提取过程中同步生成视频媒体时间到幻灯片图片的映射文件，记录 I-frame PTS 时间戳，并维护可变的决议状态（`canonical`、`duplicate`、`gap`）。
- **后处理自动裁剪**：在删除较模糊的 `may_be_slide` 疑似幻灯片前，自动识别幻灯片区域并进行无损裁剪保留，同时支持候选帧的二次 pHash 去重。
- **新增 CLI 参数**：新增命令行标志 `--write-timeline`、`--ml-autocrop-maybe`、`--ml-postcrop-dedup` 和 `--compatible`。
- **优化的 Windows 预构建包**：Windows 预构建版切换为 DirectML/CPU ONNX Runtime (`onnxruntime-win-x64`)，不再捆绑庞大的 NVIDIA CUDA 运行时 DLL，实现开箱即用的轻量化部署与 DirectML 硬件加速。

完整版本记录见 [CHANGELOG.md](CHANGELOG.md)。

## 功能特性

### 幻灯片提取

- 使用结构相似性指数 SSIM 检测不同幻灯片。
- 两阶段检测：先寻找候选切换点，再做稳定性验证。
- 根据视频特征和 I-frame 采样减少不必要的帧处理。
- 使用可配置 JPEG 质量保存幻灯片。
- 通过 FFmpeg 支持常见视频格式，包括 MP4、MKV、AVI、MOV 和 WMV。
- 支持把多个视频加入队列并依次处理。

### 智能清理

- 使用感知哈希 pHash 删除重复或近似重复幻灯片。
- 将提取结果与可配置的 pHash 排除列表进行比较。
- 为被移除图片写入稳定分类，例如 `phash_duplicate`、`phash_excluded`、`ml_not_slide`、`ml_maybe_slide` 和 `manual`。
- 被移除图片会进入应用自己的 `.extractorTrash/`，而不是直接静默删除。

### ML 幻灯片分类

- 构建时启用 ONNX Runtime 后，可使用内置 MobileNetV4 ONNX 分类模型。
- 分类为 `slide` 的图片始终保留。
- 根据阈值移除 `not_slide` 内容，例如桌面、黑屏、无信号画面和明显不是幻灯片的帧。
- 可选择是否移除 `may_be_slide` 内容，例如演示文稿编辑界面、侧屏画面或较模糊的疑似幻灯片。
- 可使用 macOS Core ML、Windows CUDA/DirectML、Linux CUDA 或 CPU 回退。

### Slides Review 与无损裁剪

- 按输出文件夹复核已提取和已移除的幻灯片。
- 可按保留/移除状态和移除原因过滤。
- 可恢复被移除幻灯片，也可把已提取幻灯片手动移入应用垃圾箱。
- 每张缩略图都可进入全窗口查看器。
- 对已提取幻灯片进行无损裁剪，并保存原图备份和裁剪元数据。
- 支持恢复裁剪、从原图重新裁剪，以及批量应用基准裁剪。
- 支持对已提取图片和可恢复的 `ml_maybe_slide` 移除项运行批量 Auto Crop。

### Slides Export

- 浏览输出目录下的幻灯片文件夹。
- 选择一个或多个文件夹生成 PDF。
- 支持自然排序和自定义顺序。
- 支持一个合并 PDF 或每个文件夹一个 PDF。
- 减小文件大小时可控制宽高比、缩放尺寸和 JPEG 质量。

### 性能

- 使用多线程处理和分块策略处理大型视频。
- 在支持的平台启用 SIMD 优化：NEON、SSE4.2、AVX2、AVX512。
- 自动检测 VideoToolbox、CUDA、OpenCL、Metal、DirectX 和 Vulkan 等硬件加速能力。

## 下载与安装

### 预构建版本

从 [GitHub Releases](https://github.com/bit-admin/AutoSlides-Extractor/releases) 下载发布包。

不同版本的发布资产可能不同，通常包括：

- **macOS**：优先使用 `.dmg` 安装包。
- **Windows**：使用安装器或便携压缩包。预构建 Windows 发布包采用 DirectML 和 CPU ONNX Runtime 提供 GPU 硬件加速，无需额外安装或捆绑 CUDA 驱动与 DLL。
- **Linux**：如果没有对应发行版包，请从源码构建。

### macOS Gatekeeper 提示

如果 macOS 提示应用来自身份不明开发者，或提示应用已损坏，请安装后在终端运行：

```bash
sudo xattr -d com.apple.quarantine /Applications/AutoSlides\ Extractor.app
```

### 系统要求

| 项目 | 建议 |
| --- | --- |
| 操作系统 | macOS 11+、Windows 10+ x64，或现代 Linux 发行版 |
| CPU | 四核或更高 |
| 内存 | 最低 8 GB，长视频建议 16 GB 或更多 |
| 磁盘 | 需要为 JPEG、应用垃圾箱、裁剪备份和 PDF 留出空间 |
| GPU | 可选，但有助于视频解码和 ML 推理加速 |

## 快速开始

1. 启动 **AutoSlides Extractor**。
2. 点击 **Add Videos**，选择一个或多个视频文件。
3. 选择输出目录，默认是 `~/Downloads/SlidesExtractor`。
4. 第一次使用建议保留默认设置。
5. 点击 **Start**。
6. 查看队列表格、进度条和状态日志。
7. 处理完成后，在输出目录中检查生成的 `slides_<video-name>/` 文件夹。
8. 点击 **Slides Review** 复核已提取和已移除幻灯片，恢复误删项，进行裁剪或重新去重。
9. 点击 **Slides Export** 将选中的幻灯片文件夹导出成 PDF。

## 用户指南

### 主窗口

<p align="center">
  <img src="docs/main-window.png" alt="带有视频队列和处理控制的主窗口" width="90%" />
</p>

主窗口围绕视频队列、处理控制、后处理选项、进度条和状态日志组织。

### 主流程

1. 点击 **Add Videos**。
2. 选择一个或多个视频文件。
3. 选择输出目录。
4. 在右侧确认后处理选项。
5. 点击 **Start**。
6. 等待队列项进入 **Completed** 状态。
7. 使用 **Slides Review** 复核结果。
8. 使用 **Slides Export** 生成 PDF。

### 队列表格

| 列 | 含义 |
| --- | --- |
| Filename | 输入视频文件名。 |
| Status | 当前队列状态。 |
| Time (s) | 该视频处理耗时。 |
| Extracted | 清理前提取出的原始幻灯片数量。 |
| - pHash | 被 pHash 重复检测或排除列表移除的数量。 |
| - ML | 被 ML 分类移除的数量。 |
| Saved | 清理后保留的幻灯片数量。 |

### 主窗口按钮

| 按钮 | 用途 |
| --- | --- |
| **Start** | 开始处理队列中的视频。 |
| **Pause** | 尽可能暂停当前处理。 |
| **Reset** | 重置进度和队列状态。 |
| **Settings** | 打开处理、pHash、ML、Auto Crop 和 CLI 设置。 |
| **Slides Export** | 打开 PDF 导出流程。 |
| **Slides Review** | 打开复核、恢复、删除、裁剪和去重流程。 |
| **Manual Post-Processing** | 对已有图片文件夹运行当前 pHash 和 ML 后处理设置。 |

## 处理设置

<p align="center">
  <img src="docs/settings-processing.png" alt="处理设置" width="55%" />
</p>

打开 **Settings**，在 **Processing** 标签页调整提取灵敏度和输出质量。

### SSIM 预设

SSIM 是结构相似度分数。阈值越高，对画面变化越敏感。

| 预设 | 阈值 | 适用场景 |
| --- | ---: | --- |
| Strict | `0.9990` | 小变化容易漏检的视频。 |
| Normal | `0.9985` | 常见课程录播和屏幕录制。 |
| Loose | `0.9980` | 动画、鼠标移动或压缩噪声明显的视频。 |
| Custom | 自定义 | 对困难视频精细调参。 |

调参建议：

- 如果漏掉幻灯片，尝试 **Strict** 或更高的自定义阈值。
- 如果保存了太多动画中间帧，尝试 **Loose** 或更低的自定义阈值。
- 不确定时先用 **Normal**。

### Downsampling 降采样

降采样会在计算 SSIM 前缩小帧尺寸。它可以提升速度，也能减少细小压缩噪声对相似度的影响。

默认值：

- 开启
- 宽度：`480`
- 高度：`270`

只有在需要捕捉非常细小的视觉变化时，才建议关闭降采样测试。

### Chunk Size 分块大小

分块处理用于限制长视频的内存占用。默认 chunk size 为 `100` 帧。只有在内存充足且希望减少分块边界时才需要增大。

### JPEG Quality

提取和裁剪流程都会保存 JPEG。质量越高，文字和线条越清晰，文件也越大。

默认值：`95`

建议：

- `95`：最高归档质量。
- `85`：适合分享和生成 PDF 的平衡值。
- `70-80`：文件更小，但需要确认文字仍然清晰。

## pHash 后处理

<p align="center">
  <img src="docs/settings-post-processing.png" alt="pHash 后处理设置" width="55%" />
</p>

pHash 可以识别视觉上接近的图片，即使它们的像素并不完全相同。

### 主要选项

| 选项 | 作用 |
| --- | --- |
| **Enable Post-Processing** | 提取后启用清理流程。 |
| **Delete Redundant Slides with pHash** | 删除重复或近似重复幻灯片。 |
| **Compare with pHash Excluded List** | 删除与排除列表匹配的幻灯片。 |
| **Hamming Distance Threshold** | 控制两个 pHash 需要多相似才算匹配。 |

### Hamming 阈值

值越低，匹配越严格。值越高，能抓到更多近似项，但也更容易误删不同幻灯片。

默认值：`10`

建议：

- 误删太多：降低阈值。
- 重复残留太多：逐步提高阈值。
- 常用测试值：`8`、`10`、`12`。

### 排除列表

排除列表适合处理总是需要移除的画面，例如片头、片尾、黑屏、固定校徽页或重复标题页。

添加方式：

- **Add from Image**：选择示例图片，由应用计算 pHash。
- **Manual Input**：手动输入 64 位十六进制 pHash 字符串。

匹配排除列表的图片会移动到 `.extractorTrash/`，并标记为 `phash_excluded`。

## ML 幻灯片分类

<p align="center">
  <img src="docs/settings-ml.png" alt="ML 分类设置" width="55%" />
</p>

构建时启用 ONNX Runtime 后，可以使用内置 MobileNetV4 分类模型。模型分为三类：

| 类别 | 默认行为 |
| --- | --- |
| `slide` | 始终保留。 |
| `not_slide` | 置信度满足阈值时移除。 |
| `may_be_slide` | 开启对应选项且置信度满足阈值时移除。 |

常见 `not_slide`：桌面、黑屏、无信号画面、明显不是演示文稿的帧。

常见 `may_be_slide`：PPT 编辑界面、侧屏画面、包含幻灯片但不够干净的模糊场景。

### 两阶段阈值逻辑

对 `not_slide` 和 `may_be_slide`，应用使用保守的两阶段删除规则：

1. 预测类别置信度大于等于 high threshold 时删除。
2. 置信度位于 low 和 high 之间时，只有当 `slide` 概率低于 slide-max threshold 才删除。
3. 其他情况保留。

默认值：

| 设置 | 默认值 |
| --- | ---: |
| `not_slide` high threshold | `0.90` |
| `not_slide` low threshold | `0.75` |
| `may_be_slide` high threshold | `0.90` |
| `may_be_slide` low threshold | `0.75` |
| 中等置信度删除时允许的最大 `slide` 概率 | `0.25` |

### 后处理 `may_be_slide` 自动裁剪

当启用 **Delete 'may_be_slide' Images** 时，应用可在将疑似帧移入垃圾箱前运行自动裁剪：

1. **就地裁剪**：使用 `AutoCropDetector`（Canny/YOLO）检测 `may_be_slide` 帧上的幻灯片区域。若检测成功，则直接就地无损裁剪并保留（标记 `autoCropped=true`）。
2. **裁剪后 pHash 二次去重**：自动裁剪保留的图片会进行候选帧 pHash 检测，防止与已有幻灯片重复。
3. **设置项**：可在 Settings -> ML Classification 中配置（`mlAutoCropMaybeSlides` 和 `mlPostCropDedup`，GUI 默认开启）。

### Execution Provider

不调试平台问题时，建议使用 **Auto**。

可选项：

- **Auto**
- **CoreML**
- **CUDA**
- **DirectML**
- **CPU**

平台行为：

- macOS 可使用 Core ML。
- Windows 预构建版本使用 DirectML（加速支持 DirectX 12 的 GPU）或 CPU；若从源码构建并配置 GPU ONNX 包，亦可使用 CUDA。
- Linux 可使用 CUDA。
- 所有平台都可以回退到 CPU，但速度较慢。

### 测试单张图片

在 ML 设置页点击 **Select Image to Test** 可以对单张代表性图片进行分类测试。

重点查看：

- 预测类别
- 置信度
- 所有类别概率
- 当前使用的 execution provider

## Auto Crop 自动裁剪

<p align="center">
  <img src="docs/settings-auto-crop.png" alt="Auto Crop 设置和测试预览" width="55%" />
</p>

Auto Crop 用于检测截图或录屏画面中的实际幻灯片区域。它在查看器里不会直接提交裁剪，而是预先填充裁剪框；你确认正确后再点击 **Apply Crop**。

### 模式

| 模式 | 行为 |
| --- | --- |
| **Canny then YOLO** | 先尝试传统边缘检测，失败后使用 YOLO。 |
| **Canny only** | 只使用 OpenCV 边缘检测。 |
| **YOLO only** | 只使用 ONNX YOLO 检测器。 |

默认：**Canny then YOLO**

### Canny Aspect Tolerance

Canny 检测器会寻找接近 16:9 或 4:3 的强矩形区域。

默认值：`0.05`

如果幻灯片不是标准比例，可适当增大；如果误识别太多非幻灯片矩形，可适当减小。

### YOLO Confidence Threshold

默认值：`0.25`

降低阈值会检测更多候选框，也会增加误检；提高阈值会更严格。

### 测试 Auto Crop

使用 **Test Canny...** 或 **Test YOLO...** 选择一张图片并预览检测框。这是批量裁剪前调参最快的方式。

## Slides Review 幻灯片复核

Slides Review 是应用中最重要的安全复核流程。每次提取后都建议用它检查保留和移除的幻灯片。

### 文件夹页

<p align="center">
  <img src="docs/slides-review-folders.png" alt="Slides Review 文件夹页" width="90%" />
</p>

文件夹页列出输出目录下的幻灯片文件夹。

| 列 | 含义 |
| --- | --- |
| Folder | `slides_<video-name>` 文件夹。 |
| Extracted | 当前保留的幻灯片数量。 |
| Removed | `.extractorTrash/` 中属于该文件夹的移除项数量。 |
| Review | 打开该文件夹的图片网格。 |

可用操作：

- **Select All / Deselect All**：切换当前文件夹勾选状态。
- **Empty Trash**：清空勾选文件夹对应的应用垃圾箱项目。
- **Delete Folder**：将勾选的幻灯片文件夹及对应 trash/crop 元数据移动到系统废纸篓。
- **Refresh**：重新扫描文件夹和元数据。

### 图片网格页

<p align="center">
  <img src="docs/slides-review-grid.png" alt="Slides Review 图片网格" width="90%" />
</p>

图片网格页会按 slide index 交错显示已提取和已移除的图片。

视觉提示：

- 已提取幻灯片是 `slides_<video-name>/` 中的实时文件。
- 已移除幻灯片有红色视觉样式。
- 已移除幻灯片会显示原因标签，例如 **pHash - Duplicate**、**ML - Not Slide** 或 **Manual**。
- 已裁剪的已提取幻灯片会显示 **Cropped** 标签。

### 过滤器

**Show** 过滤器控制显示哪些状态：

- Show Both
- Show Extracted Only
- Show Removed Only

**Reason** 过滤器作用于已移除项目：

- All Reasons
- pHash - Duplicate
- pHash - Excluded
- ML - Not Slide
- ML - May Be Slide
- Manual

Select All / Deselect All 只作用于当前过滤后可见的项目。

### 图片操作

| 操作 | 作用对象 | 效果 |
| --- | --- | --- |
| **View** | 任意项目 | 打开全窗口查看器。 |
| **Restore Selected** | 已移除项目 | 恢复到原始文件夹。 |
| **Delete Selected** | 已提取项目 | 作为 `manual` 移动到应用垃圾箱。 |
| **Restore Crop** | 已裁剪的已提取项目 | 从 `.extractorCrop/` 恢复原图。 |
| **Remove Duplicate** | 当前文件夹 | 使用当前 Hamming 阈值重新运行 pHash 去重。 |
| **Auto Crop** | 已提取项目和 `ml_maybe_slide` 移除项 | 检测幻灯片区域并执行裁剪流程。 |
| **Set as Baseline** | 已裁剪的已提取项目 | 把该裁剪框设为批量裁剪基准。 |
| **Apply Baseline** | 选中的未裁剪已提取项目 | 按比例应用基准裁剪。 |

### 查看器与手动裁剪

<p align="center">
  <img src="docs/slides-review-viewer-crop.png" alt="Slides Review 查看器裁剪框" width="90%" />
</p>

查看器一次打开一张图片。

对已提取幻灯片：

1. 点击 **Crop**。
2. 拖拽选择幻灯片区域。
3. 点击 **Apply Crop**。
4. 应用会把原图复制到 `.extractorCrop/`。
5. 当前 `slide_*.jpg` 会被裁剪后的图片替换。
6. 元数据会记录原图像素坐标中的裁剪矩形。

对已移除幻灯片：

- 查看器为只读。
- 点击 **Back** 返回网格。
- 需要编辑时，先在网格中恢复该项目。

### Restore Crop

当裁剪过紧、过松或不再需要时，使用 **Restore Crop**。应用会从 `.extractorCrop/` 恢复原图，并删除对应裁剪元数据。

### Recrop

当已裁剪幻灯片需要重新选择裁剪框时，使用 **Recrop**。它从原始备份开始，而不是从当前裁剪图继续裁剪，因此多次裁剪不会逐步损失画面。

### Baseline Crop

Baseline Crop 适合整节课都带有相同窗口边框或黑边的情况。

1. 裁剪一张代表性幻灯片。
2. 回到图片网格。
3. 在该已裁剪幻灯片上点击 **Set as Baseline**。
4. 选择其他未裁剪的已提取幻灯片。
5. 点击 **Apply Baseline**。

应用会根据每张目标图片尺寸按比例缩放基准裁剪框，并限制在图像边界内。

### 批量 Auto Crop

图片网格中的 **Auto Crop** 按钮只在选择内容兼容时启用：

- 已提取幻灯片
- category 为 `ml_maybe_slide` 的已移除幻灯片

如果选择中包含 `phash_duplicate`、`phash_excluded`、`ml_not_slide` 或 `manual` 移除项，按钮会禁用。

对已提取幻灯片，Auto Crop 会检测区域并通过正常裁剪流程应用。

对 `ml_maybe_slide` 移除项，Auto Crop 会先在垃圾箱文件上检测有效幻灯片区域；检测成功则恢复并裁剪，检测失败则跳过。

## Slides Export PDF 导出

<p align="center">
  <img src="docs/slides-export.png" alt="Slides Export PDF 工作流" width="90%" />
</p>

Slides Export 用于从幻灯片文件夹生成 PDF。

### 工作流

1. 在主窗口点击 **Slides Export**。
2. 检查 `slides_*` 文件夹列表。
3. 选择一个或多个文件夹。
4. 选择输出模式。
5. 选择顺序。
6. 选择文件大小和质量设置。
7. 点击 **Make PDF**。
8. 导出完成后点击 **Open PDF**。

### 输出模式

| 模式 | 结果 |
| --- | --- |
| One Combined PDF | 所选文件夹按顺序合并成一个 PDF。 |
| One PDF per Folder | 每个所选文件夹各导出一个 PDF。 |

### 排序

默认使用自然 A-Z 排序。自然排序比普通字符串排序更适合处理数字、日期、英文星期/月名和中文星期。

需要自定义顺序时：

1. 切换到 custom order。
2. 选择文件夹行。
3. 使用 **Move Up**、**Move Down** 或拖拽调整。
4. 顺序确认后导出。

### 文件大小控制

开启 **Reduce File Size** 后可以：

- 选择 16:9 或 4:3 等宽高比
- 选择缩放预设
- 选择 JPEG 质量

建议：

| 目标 | 建议设置 |
| --- | --- |
| 最佳质量 | 原始尺寸或 1080p，JPEG 85-95。 |
| 课堂讲义 | 720p，JPEG 70-85。 |
| 极小文件 | 480p，JPEG 50-70，并确认文字可读。 |

## 命令行指南

<p align="center">
  <img src="docs/settings-cli.png" alt="CLI 安装设置" width="55%" />
</p>

CLI 适合自动化、批处理和外部程序调用。

### 安装 wrapper

1. 打开 **Settings**。
2. 打开 **CLI** 标签页。
3. 点击 **Install CLI**。
4. 如果设置页显示 PATH 提示，把提示目录加入 shell 配置。
5. 移动 app bundle 或可执行文件后，请重新安装 wrapper。

wrapper 命令为：

```bash
SlidesExtractor
```

传入任何命令行参数时，应用会直接进入 CLI 模式；无用户参数启动时打开 GUI。

### 基本用法

```bash
SlidesExtractor --video lecture.mp4 --output ~/Slides
```

只有 `--video` 和 `--output` 是必填项。

其他值默认来自 GUI 中保存的设置，除非命令行显式覆盖。

### 启用完整清理流程

```bash
SlidesExtractor --video lecture.mp4 --output ~/Slides \
    --phash-redundant \
    --phash-exclusion \
    --ml-classify
```

### 临时排除哈希

```bash
SlidesExtractor --video lecture.mp4 --output ~/Slides \
    --phash-exclusion-hashes <64-char-hex>,<64-char-hex>
```

该选项会隐式启用 `--phash-exclusion`，但不会把这些哈希写回 GUI 设置。

### 单次覆盖检测参数

```bash
SlidesExtractor --video lecture.mp4 --output ~/Slides \
    --ssim-threshold 0.999 \
    --jpeg-quality 80
```

### JSON Lines 输出

```bash
SlidesExtractor --video lecture.mp4 --output ~/Slides --json
```

JSON 模式每行输出一个紧凑 JSON 对象，适合 `child_process.spawn`、shell pipeline 或其他进程管理器。

常见事件：

| Event | 含义 |
| --- | --- |
| `start` | CLI 运行开始，并包含解析后的选项。 |
| `info` | 视频元数据或警告。 |
| `frame_progress` | 帧提取进度。 |
| `slides_extracted` | 提取阶段完成。 |
| `post_progress` | pHash 或 ML 后处理进度。 |
| `trash` | 某个文件被移动到应用垃圾箱。 |
| `ml_started` | ML 分类开始，并包含 execution provider。 |
| `ml_failed` | ML 分类器初始化或运行失败。 |
| `post_complete` | 后处理汇总。 |
| `done` | 全流程成功完成。 |
| `cancelled` | SIGINT 或 SIGTERM 取消完成。 |
| `error` | 结构化错误，通常写入 stderr。 |

### CLI 参数

| 参数 | 值 | 说明 |
| --- | --- | --- |
| `--video` | path | 输入视频文件，必填。 |
| `--output` | dir | 输出目录，必填。 |
| `--ssim-threshold` | float | 自定义 SSIM 阈值，例如 `0.9985`。 |
| `--enable-downsampling` | bool | `true` 或 `false`。 |
| `--downsample-width` | int | 降采样宽度。 |
| `--downsample-height` | int | 降采样高度。 |
| `--chunk-size` | int | 帧分块大小。 |
| `--jpeg-quality` | int | JPEG 质量，范围 `1` 到 `100`。 |
| `--phash-redundant` | flag | 使用 pHash 移除重复幻灯片。 |
| `--phash-exclusion` | flag | 与 GUI 中保存的 pHash 排除列表比较。 |
| `--phash-exclusion-hashes` | CSV | 逗号分隔的 64 字符 pHash 十六进制字符串。 |
| `--hamming-threshold` | int | pHash 重复检测和排除匹配共用的 Hamming 阈值。 |
| `--ml-classify` | flag | 运行 ML 分类。 |
| `--ml-model` | path | 自定义 ONNX 分类模型路径；省略或传空则使用内置模型。 |
| `--ml-execution-provider` | name | `Auto`、`CoreML`、`CUDA`、`DirectML` 或 `CPU`。 |
| `--ml-not-slide-high` | float | `not_slide` high threshold。 |
| `--ml-not-slide-low` | float | `not_slide` low threshold。 |
| `--ml-maybe-slide-high` | float | `may_be_slide` high threshold。 |
| `--ml-maybe-slide-low` | float | `may_be_slide` low threshold。 |
| `--ml-slide-max` | float | 中等置信度删除时允许的最大 `slide` 概率。 |
| `--ml-delete-maybe-slides` | bool | 是否删除 `may_be_slide` 图片。 |
| `--ml-autocrop-maybe` | flag | 在删除 `may_be_slide` 图片前自动裁剪，成功则保留。 |
| `--ml-postcrop-dedup` | flag | 后处理自动裁剪后重新检测 pHash 重复项。 |
| `--write-timeline` | flag | 在输出幻灯片文件夹中写入 `timeline.json` 媒体时间映射文件。 |
| `--compatible` | flag | Electron 兼容模式（输出 PNG 幻灯片，去除 `screen_` 前缀，跳过后处理）。 |
| `--json` | flag | 输出 JSON Lines 并关闭文本进度条。 |
| `--help` | flag | 打印帮助。 |
| `--version` | flag | 打印版本。 |

布尔值支持 `true`、`false`、`1`、`0`、`yes`、`no`、`on` 和 `off`。

### 退出码

| 退出码 | 含义 |
| ---: | --- |
| `0` | 成功。 |
| `2` | 命令行参数无效。 |
| `3` | 输入或输出路径错误。 |
| `4` | 视频处理失败。 |
| `5` | 后处理失败。 |
| `130` | 被 SIGINT 取消，通常是 Ctrl-C。 |
| `143` | 被 SIGTERM 取消。 |

## 输出文件与元数据

典型输出目录：

```text
SlidesExtractor/
  slides_Lecture01/
    slide_Lecture01_001.jpg
    slide_Lecture01_002.jpg
    timeline.json
  .extractorTrash/
    metadata.json
    slideRemoved_phash_Lecture01_003.jpg
  .extractorCrop/
    metadata.json
    slideOriginal_Lecture01_002.jpg
```

### 幻灯片文件夹

每个视频会创建一个文件夹：

```text
slides_<video-base-name>
```

每张幻灯片命名为：

```text
slide_<video-base-name>_<three-digit-index>.jpg
```

示例：

```text
slides_Lecture01/slide_Lecture01_001.jpg
```

### 时间线映射文件 (timeline.json)

当开启 **Write timeline.json** 时（GUI 默认开启，CLI 需传入 `--write-timeline`），每个 `slides_<video-name>/` 文件夹中都会包含 `timeline.json`，用于将视频媒体时间映射到提取的幻灯片图片：

- **事件 (Events)**：捕获事件数组，记录 I-frame PTS 媒体时间（`changeAt`、`confirmedAt`）及 `initialFile` 基本文件名。
- **决议 (Resolutions)**：可变的决议映射表，追踪幻灯片状态（`canonical`、`duplicate`、`gap`）。后处理与人工复核会更新决议状态（如 `duplicateOf`、`exclusion`、`ai_filtered`、`manual_trash`），而不会删除捕获事件，从而为播放器和下游应用提供稳定可追溯的时间线。

### 应用垃圾箱

应用垃圾箱位置：

```text
<output-root>/.extractorTrash/
```

其中包含被移除图片和 `metadata.json`。元数据记录：

- 原始文件夹
- 视频名称
- slide index
- 移除方法
- 分类 category
- 人类可读的原因
- 时间戳

常见 category：

| Category | 显示名称 |
| --- | --- |
| `phash_duplicate` | pHash - Duplicate |
| `phash_excluded` | pHash - Excluded |
| `ml_not_slide` | ML - Not Slide |
| `ml_maybe_slide` | ML - May Be Slide |
| `manual` | Manual |

### 裁剪备份

裁剪备份位置：

```text
<output-root>/.extractorCrop/
```

其中包含原图备份和 `metadata.json`。元数据记录：

- 备份文件名
- 原始文件夹
- 视频名称
- slide index
- 原图像素坐标中的裁剪矩形
- 时间戳

这使得 Restore Crop 和 Recrop 都是无损的。

## 工作原理

1. **视频解码**：通过 FFmpeg 和平台解码器读取输入视频帧。
2. **智能采样**：根据视频特征和 I-frame 采样减少无意义处理。
3. **帧间比较**：使用 SSIM 计算相邻采样帧的结构相似度。
4. **切换检测**：当相似度下降时，SlideDetector 识别候选幻灯片变化。
5. **稳定性验证**：第二阶段过滤动画、鼠标移动和短暂转场。
6. **保存幻灯片**：把选中的帧写成按序编号的 JPEG。
7. **pHash 后处理**：把重复页和排除页移动到应用垃圾箱。
8. **ML 分类**：可选 ONNX 推理根据阈值移除非幻灯片内容。
9. **复核与导出**：Slides Review 提供恢复、删除、裁剪和清理；Slides Export 生成 PDF。

## 从源码构建

### 依赖

必需：

- CMake 3.16+
- C++17 编译器
- Qt 6 Core、Widgets、Gui
- OpenCV 4.x core、imgproc、imgcodecs
- FFmpeg 开发库：libavcodec、libavformat、libavutil、libswscale

可选：

- ONNX Runtime，用于 ML 分类和 YOLO Auto Crop
- CUDA、OpenCL、Vulkan、DirectX、Metal 或 Core ML 加速

### 标准构建

```bash
git clone https://github.com/bit-admin/AutoSlides-Extractor.git
cd AutoSlides-Extractor
mkdir build
cd build
cmake ..
cmake --build . --config Release
```

### Debug 构建

```bash
mkdir build
cd build
cmake -DCMAKE_BUILD_TYPE=Debug ..
cmake --build . --config Debug
```

### 清理后重建

```bash
rm -rf build
mkdir build
cd build
cmake ..
cmake --build . --config Release
```

### 依赖搜索路径

如果 CMake 找不到依赖，可设置：

```bash
export CMAKE_PREFIX_PATH="/path/to/Qt:/path/to/OpenCV:$CMAKE_PREFIX_PATH"
export PKG_CONFIG_PATH="/path/to/ffmpeg/pkgconfig:$PKG_CONFIG_PATH"
```

Windows 可用 vcpkg 提供 OpenCV 和 FFmpeg。仓库中的 `vcpkg.json` 覆盖核心依赖。

### ONNX Runtime

CMake 会先检查 `vendor/` 下的平台专用 ONNX Runtime 目录，再尝试 `find_package(onnxruntime)` 和常见 include/lib 路径。

示例：

```text
vendor/onnxruntime-osx-arm64-1.23.2/
vendor/onnxruntime-osx-x86_64-1.23.2/
vendor/onnxruntime-win-x64-1.23.2/
vendor/onnxruntime-win-x64-gpu-1.23.2/
vendor/onnxruntime-linux-x64-gpu-1.23.2/
vendor/onnxruntime-linux-x64-1.23.2/
```

*（注：Windows 预构建发布包使用 `onnxruntime-win-x64`（包含 DirectML 和 CPU 支持）以消除 CUDA 运行时依赖。如果从源码构建且需要 Windows 上的 CUDA 支持，请下载 GPU 版本 ONNX 包）。*

如果找不到 ONNX Runtime，应用仍可构建，但 ML 分类和 YOLO Auto Crop 会禁用。

## 故障排查

### 漏检幻灯片

按顺序尝试：

1. 使用 **Strict** SSIM 预设。
2. 略微提高自定义 SSIM 阈值。
3. 关闭 downsampling 测试一次。
4. 检查视频是否包含长动画或复杂转场。

### 保存了太多幻灯片

按顺序尝试：

1. 使用 **Loose** SSIM 预设。
2. 略微降低自定义 SSIM 阈值。
3. 保持 pHash 去重开启。
4. 修改 Hamming 阈值后，在 Slides Review 中使用 **Remove Duplicate**。

### pHash 误删了真实幻灯片

1. 打开 **Slides Review**。
2. Reason 过滤为 **pHash - Duplicate** 或 **pHash - Excluded**。
3. 选择误删幻灯片。
4. 点击 **Restore Selected**。
5. 如果经常发生，下一次运行前降低 Hamming 阈值。

### ML 误删了真实幻灯片

1. 打开 **Slides Review**。
2. Reason 过滤为 **ML - Not Slide** 或 **ML - May Be Slide**。
3. 恢复对应幻灯片。
4. 提高 ML 阈值，或关闭 `may_be_slide` 删除。
5. 在 ML 设置页测试代表性图片。

### Auto Crop 检测框错误

可尝试：

- Canny only
- YOLO only
- 调低或调高 YOLO confidence threshold
- 调整 Canny aspect tolerance
- 在查看器中手动裁剪

点击 **Apply Crop** 前一定先检查检测框。

### PDF 文件太大

开启 **Reduce File Size**，选择更小的 resize preset，并降低 JPEG quality。分享或归档前确认文字仍然可读。

### CLI wrapper 指向错误应用

移动 app bundle 或可执行文件后，在 **Settings > CLI** 中重新安装 wrapper。

### macOS 提示应用已损坏

运行：

```bash
sudo xattr -d com.apple.quarantine /Applications/AutoSlides\ Extractor.app
```

### macOS Qt Creator 或开发环境启动崩溃

如果开发环境中出现 `IIOReadPlugin::callInitialize()`、光标或图标加载相关崩溃，检查 Qt Creator 或 shell 是否暴露了过宽的 Homebrew 库路径，例如：

```bash
DYLD_LIBRARY_PATH=/opt/homebrew/lib
```

这可能让 Apple ImageIO 路径加载 Homebrew `libpng`。如果直接启动应用正常，通常不需要因为这个开发环境问题修改应用代码。

检查已构建 app 的 runtime path：

```bash
otool -l AutoSlidesExtractor.app/Contents/MacOS/AutoSlidesExtractor | grep -A3 LC_RPATH
otool -L AutoSlidesExtractor.app/Contents/MacOS/AutoSlidesExtractor | grep homebrew
```

## 实用建议

典型课程录播：

1. 使用 **Normal** SSIM。
2. 保持 downsampling 开启。
3. 保持 pHash 去重开启。
4. 如果 ONNX Runtime 可用，保持 ML 分类开启。
5. 清空垃圾箱前先复核被移除幻灯片。
6. 提取和清理完成后再裁剪。
7. 复核、裁剪和去重完成后再导出 PDF。

动画很多的视频：

1. 先尝试 **Loose** SSIM。
2. 保持 pHash 去重开启。
3. 仔细复核 pHash 移除项。

包含细小增量变化的视频：

1. 尝试 **Strict** SSIM。
2. 可考虑关闭 downsampling。
3. 之后用 Slides Review 删除多余近重复项。

经常出现桌面切换的录屏：

1. 保持 ML 分类开启。
2. 只有在愿意复核潜在误删时，才开启 `may_be_slide` 删除。
3. 对包含可恢复幻灯片内容的 `ml_maybe_slide` 项使用 Auto Crop。

## 相关项目

- [bit-admin/Yanhekt-AutoSlides](https://github.com/bit-admin/Yanhekt-AutoSlides)：延河课堂第三方客户端与幻灯片提取相关工具。
- [bit-admin/slide-classifier](https://github.com/bit-admin/slide-classifier)：MobileNetV4 幻灯片分类模型项目。
- [bit-admin/slide-crop](https://github.com/bit-admin/slide-crop)：YOLO 幻灯片区域检测模型项目。

## 许可证

本项目使用 [MIT License](LICENSE)。

---

<p align="center">
  <em>Built with Qt 6, OpenCV, FFmpeg, and ONNX Runtime support.</em>
</p>
