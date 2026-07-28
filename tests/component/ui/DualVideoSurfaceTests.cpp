#include "dvs/platform/D3d11DualVideoRenderer.h"
#include "dvs/platform/FrameBudget.h"
#include "dvs/platform/FrameMailbox.h"
#include "dvs/platform/FrameResourceFactory.h"
#include "dvs/platform/GpuTransferActor.h"
#include "dvs/platform/GraphicsDeviceBroker.h"
#include "dvs/platform/PresentationAckMailbox.h"
#include "dvs/platform/RenderActivitySink.h"
#include "dvs/ui/DualVideoSurface.h"

#include <QColor>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QGuiApplication>
#include <QImage>
#include <QQuickGraphicsConfiguration>
#include <QQuickWindow>
#include <QScreen>
#include <QThread>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <gtest/gtest.h>
#include <memory>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace dvs::ui {
namespace {

using namespace std::chrono_literals;

[[nodiscard]] std::array<float, 3U> applyColorTransform(
    const platform::Nv12ColorTransform& transform, const float y, const float u, const float v) {
    const std::array<float, 4U> sample{y, u, v, 1.0F};
    std::array<float, 3U> rgb{};
    for (std::size_t row = 0U; row < rgb.size(); ++row) {
        for (std::size_t column = 0U; column < sample.size(); ++column) {
            rgb[row] += transform.yuvToRgb[(row * sample.size()) + column] * sample[column];
        }
    }
    return rgb;
}

[[nodiscard]] QColor expectedNv12Color(const domain::ColorMetadata& metadata,
                                       const std::uint8_t y,
                                       const std::uint8_t u,
                                       const std::uint8_t v) {
    const std::array<float, 3U> rgb = applyColorTransform(platform::nv12ColorTransform(metadata),
                                                          static_cast<float>(y) / 255.0F,
                                                          static_cast<float>(u) / 255.0F,
                                                          static_cast<float>(v) / 255.0F);
    const auto channel = [](const float value) {
        return static_cast<int>(std::lround(std::clamp(value, 0.0F, 1.0F) * 255.0F));
    };
    return QColor{channel(rgb[0U]), channel(rgb[1U]), channel(rgb[2U])};
}

[[nodiscard]] float differenceGainValue(const DualVideoSurface::DifferenceGain gain) {
    switch (gain) {
    case DualVideoSurface::Gain1x:
        return 1.0F;
    case DualVideoSurface::Gain2x:
        return 2.0F;
    case DualVideoSurface::Gain4x:
        return 4.0F;
    case DualVideoSurface::Gain8x:
        return 8.0F;
    case DualVideoSurface::Gain16x:
        return 16.0F;
    }
    return 1.0F;
}

[[nodiscard]] QColor expectedDifferenceColor(const domain::ColorMetadata& metadataA,
                                             const std::array<std::uint8_t, 3U>& sampleA,
                                             const domain::ColorMetadata& metadataB,
                                             const std::array<std::uint8_t, 3U>& sampleB,
                                             const DualVideoSurface::DifferenceMetric metric,
                                             const DualVideoSurface::DifferenceGain gain) {
    const auto rgb = [](const domain::ColorMetadata& metadata,
                        const std::array<std::uint8_t, 3U>& sample) {
        return applyColorTransform(platform::nv12ColorTransform(metadata),
                                   static_cast<float>(sample[0U]) / 255.0F,
                                   static_cast<float>(sample[1U]) / 255.0F,
                                   static_cast<float>(sample[2U]) / 255.0F);
    };
    const auto luma = [](const std::array<float, 3U>& value) {
        return (0.2126F * value[0U]) + (0.7152F * value[1U]) + (0.0722F * value[2U]);
    };
    const std::array<float, 3U> rgbA = rgb(metadataA, sampleA);
    const std::array<float, 3U> rgbB = rgb(metadataB, sampleB);
    const float multiplier = differenceGainValue(gain);
    std::array<float, 3U> result{};
    if (metric == DualVideoSurface::Luma) {
        const float difference = multiplier * std::abs(luma(rgbA) - luma(rgbB));
        result = {difference, difference, difference};
    } else if (metric == DualVideoSurface::Chroma) {
        const float lumaA = luma(rgbA);
        const float lumaB = luma(rgbB);
        const float cbA = (rgbA[2U] - lumaA) / 1.8556F;
        const float cbB = (rgbB[2U] - lumaB) / 1.8556F;
        const float crA = (rgbA[0U] - lumaA) / 1.5748F;
        const float crB = (rgbB[0U] - lumaB) / 1.5748F;
        result = {multiplier * std::abs(crA - crB), 0.0F, multiplier * std::abs(cbA - cbB)};
    } else {
        for (std::size_t index = 0U; index < result.size(); ++index) {
            result[index] = multiplier * std::abs(rgbA[index] - rgbB[index]);
        }
        if (metric == DualVideoSurface::Heatmap) {
            const float value =
                std::clamp(std::max({result[0U], result[1U], result[2U]}), 0.0F, 1.0F);
            result = {std::clamp(value * 3.0F, 0.0F, 1.0F),
                      std::clamp((value * 3.0F) - 1.0F, 0.0F, 1.0F),
                      std::clamp((value * 3.0F) - 2.0F, 0.0F, 1.0F)};
        }
    }
    const auto channel = [](const float value) {
        return static_cast<int>(
            std::lround(std::clamp(value, 0.0F, 1.0F) * static_cast<float>(255)));
    };
    return QColor{channel(result[0U]), channel(result[1U]), channel(result[2U])};
}

void expectColorNear(const QColor& actual, const QColor& expected, const int tolerance = 3) {
    EXPECT_NEAR(actual.red(), expected.red(), tolerance);
    EXPECT_NEAR(actual.green(), expected.green(), tolerance);
    EXPECT_NEAR(actual.blue(), expected.blue(), tolerance);
}

[[nodiscard]] int maximumRgbDifference(const QImage& left, const QImage& right) {
    if (left.size() != right.size() || left.isNull() || right.isNull()) {
        return 255;
    }
    int maximum = 0;
    for (int y = 0; y < left.height(); ++y) {
        for (int x = 0; x < left.width(); ++x) {
            const QColor leftPixel = left.pixelColor(x, y);
            const QColor rightPixel = right.pixelColor(x, y);
            maximum = std::max({maximum,
                                std::abs(leftPixel.red() - rightPixel.red()),
                                std::abs(leftPixel.green() - rightPixel.green()),
                                std::abs(leftPixel.blue() - rightPixel.blue())});
        }
    }
    return maximum;
}

TEST(DualVideoSurfaceGeometryTests, SplitsEveryOddPhysicalPixelWithoutAGap) {
    const platform::SurfaceSplitLayout split =
        platform::computeSurfaceSplit(101.0F, 60.0F, 101U, 60U);

    EXPECT_EQ(split.leftPixelWidth, 50U);
    EXPECT_EQ(split.rightPixelWidth, 51U);
    EXPECT_FLOAT_EQ(split.left.x + split.left.width, split.right.x);
    EXPECT_FLOAT_EQ(split.right.x + split.right.width, 101.0F);
    EXPECT_FLOAT_EQ(split.left.height, 60.0F);
    EXPECT_FLOAT_EQ(split.right.height, 60.0F);
}

TEST(DualVideoSurfaceGeometryTests, AspectFitsUnequalSourcesWithinTheirOwnHalves) {
    const platform::SurfaceRect bounds{
        .x = 50.0F,
        .y = 0.0F,
        .width = 50.0F,
        .height = 60.0F,
    };
    const platform::SurfaceRect landscape = platform::aspectFitRect(bounds, 16U, 9U);
    const platform::SurfaceRect portrait = platform::aspectFitRect(bounds, 9U, 16U);

    EXPECT_FLOAT_EQ(landscape.x, 50.0F);
    EXPECT_NEAR(landscape.height, 28.125F, 0.001F);
    EXPECT_NEAR(landscape.y, 15.9375F, 0.001F);
    EXPECT_FLOAT_EQ(portrait.height, 60.0F);
    EXPECT_NEAR(portrait.width, 33.75F, 0.001F);
    EXPECT_NEAR(portrait.x, 58.125F, 0.001F);
}

TEST(DualVideoSurfaceGeometryTests, ConvertsAndClampsBottomLeftScissorToD3dViewport) {
    const std::optional<platform::D3dScissorRect> converted = platform::d3dScissorFromBottomLeft(
        platform::SurfaceScissorRect{
            .left = -5,
            .bottom = 20,
            .right = 120,
            .top = 50,
        },
        platform::SurfaceViewport{
            .topLeftX = 10.0F,
            .topLeftY = 10.0F,
            .width = 100.0F,
            .height = 100.0F,
        },
        120U);

    ASSERT_TRUE(converted.has_value());
    EXPECT_EQ(*converted,
              (platform::D3dScissorRect{.left = 10, .top = 70, .right = 110, .bottom = 100}));
    EXPECT_FALSE(platform::d3dScissorFromBottomLeft(
                     platform::SurfaceScissorRect{
                         .left = 0,
                         .bottom = 20,
                         .right = 5,
                         .top = 50,
                     },
                     platform::SurfaceViewport{
                         .topLeftX = 10.0F,
                         .topLeftY = 10.0F,
                         .width = 100.0F,
                         .height = 100.0F,
                     },
                     120U)
                     .has_value());
}

TEST(DualVideoSurfaceGeometryTests, RejectsUnknownPresentationOptions) {
    platform::SurfaceRenderState state{
        .logicalWidth = 100.0F,
        .logicalHeight = 60.0F,
        .pixelWidth = 100U,
        .pixelHeight = 60U,
    };
    EXPECT_TRUE(state.isValid());

    state.viewMode = static_cast<platform::SurfaceViewMode>(255U);
    EXPECT_FALSE(state.isValid());
    state.viewMode = platform::SurfaceViewMode::SideBySide;
    state.differenceMetric = static_cast<platform::SurfaceDifferenceMetric>(255U);
    EXPECT_FALSE(state.isValid());
    state.differenceMetric = platform::SurfaceDifferenceMetric::RgbAbsolute;
    state.differenceGain = static_cast<platform::SurfaceDifferenceGain>(255U);
    EXPECT_FALSE(state.isValid());
    state.differenceGain = platform::SurfaceDifferenceGain::Gain1x;
    state.differenceReference = static_cast<platform::SurfaceDifferenceReference>(255U);
    EXPECT_FALSE(state.isValid());
    state.differenceReference = platform::SurfaceDifferenceReference::SourceA;
    state.differenceFilter = static_cast<platform::SurfaceDifferenceFilter>(255U);
    EXPECT_FALSE(state.isValid());
}

TEST(DualVideoSurfaceColorTests, ConvertsFullRangeBt601AndBt709WithDifferentMatrices) {
    const domain::ColorMetadata bt601{
        .matrix = domain::ColorMatrix::kBt601,
        .range = domain::ColorRange::kFull,
        .matrixInferred = false,
    };
    const domain::ColorMetadata bt709{
        .matrix = domain::ColorMatrix::kBt709,
        .range = domain::ColorRange::kFull,
        .matrixInferred = false,
    };

    const std::array<float, 3U> rgb601 =
        applyColorTransform(platform::nv12ColorTransform(bt601), 0.5F, 0.25F, 0.75F);
    const std::array<float, 3U> rgb709 =
        applyColorTransform(platform::nv12ColorTransform(bt709), 0.5F, 0.25F, 0.75F);

    EXPECT_NEAR(rgb601[0U], 0.8505F, 0.0002F);
    EXPECT_NEAR(rgb601[1U], 0.4075F, 0.0003F);
    EXPECT_NEAR(rgb601[2U], 0.0570F, 0.0002F);
    EXPECT_NEAR(rgb709[0U], 0.8937F, 0.0002F);
    EXPECT_NEAR(rgb709[1U], 0.4298F, 0.0003F);
    EXPECT_NEAR(rgb709[2U], 0.0361F, 0.0002F);
}

TEST(DualVideoSurfaceColorTests, NormalizesLimitedRangeBlackAndWhiteForBothMatrices) {
    for (const domain::ColorMatrix matrix :
         {domain::ColorMatrix::kBt601, domain::ColorMatrix::kBt709}) {
        const domain::ColorMetadata metadata{
            .matrix = matrix,
            .range = domain::ColorRange::kLimited,
            .matrixInferred = false,
        };
        const platform::Nv12ColorTransform transform = platform::nv12ColorTransform(metadata);
        const std::array<float, 3U> black =
            applyColorTransform(transform, 16.0F / 255.0F, 128.0F / 255.0F, 128.0F / 255.0F);
        const std::array<float, 3U> white =
            applyColorTransform(transform, 235.0F / 255.0F, 128.0F / 255.0F, 128.0F / 255.0F);
        for (const float channel : black) {
            EXPECT_NEAR(channel, 0.0F, 0.0002F);
        }
        for (const float channel : white) {
            EXPECT_NEAR(channel, 1.0F, 0.0002F);
        }
    }
}

TEST(DualVideoSurfacePropertyTests, ExposesTypedDifferenceDefaultsAndNotifiesOnlyOnChange) {
    DualVideoSurface surface;

    EXPECT_EQ(surface.viewMode(), DualVideoSurface::SideBySide);
    EXPECT_EQ(surface.differenceMetric(), DualVideoSurface::RgbAbsolute);
    EXPECT_EQ(surface.differenceGain(), DualVideoSurface::Gain1x);
    EXPECT_EQ(surface.differenceReference(), DualVideoSurface::ReferenceA);
    EXPECT_EQ(surface.differenceFilter(), DualVideoSurface::Bilinear);

    int viewModeChanges = 0;
    int metricChanges = 0;
    int gainChanges = 0;
    int referenceChanges = 0;
    int filterChanges = 0;
    QObject::connect(&surface, &DualVideoSurface::viewModeChanged, [&] { ++viewModeChanges; });
    QObject::connect(
        &surface, &DualVideoSurface::differenceMetricChanged, [&] { ++metricChanges; });
    QObject::connect(&surface, &DualVideoSurface::differenceGainChanged, [&] { ++gainChanges; });
    QObject::connect(
        &surface, &DualVideoSurface::differenceReferenceChanged, [&] { ++referenceChanges; });
    QObject::connect(
        &surface, &DualVideoSurface::differenceFilterChanged, [&] { ++filterChanges; });

    surface.setViewMode(DualVideoSurface::Difference);
    surface.setDifferenceMetric(DualVideoSurface::Heatmap);
    surface.setDifferenceGain(DualVideoSurface::Gain16x);
    surface.setDifferenceReference(DualVideoSurface::ReferenceB);
    surface.setDifferenceFilter(DualVideoSurface::Bicubic);

    EXPECT_EQ(surface.viewMode(), DualVideoSurface::Difference);
    EXPECT_EQ(surface.differenceMetric(), DualVideoSurface::Heatmap);
    EXPECT_EQ(surface.differenceGain(), DualVideoSurface::Gain16x);
    EXPECT_EQ(surface.differenceReference(), DualVideoSurface::ReferenceB);
    EXPECT_EQ(surface.differenceFilter(), DualVideoSurface::Bicubic);
    EXPECT_EQ(viewModeChanges, 1);
    EXPECT_EQ(metricChanges, 1);
    EXPECT_EQ(gainChanges, 1);
    EXPECT_EQ(referenceChanges, 1);
    EXPECT_EQ(filterChanges, 1);

    surface.setViewMode(DualVideoSurface::Difference);
    surface.setDifferenceMetric(DualVideoSurface::Heatmap);
    surface.setDifferenceGain(DualVideoSurface::Gain16x);
    surface.setDifferenceReference(DualVideoSurface::ReferenceB);
    surface.setDifferenceFilter(DualVideoSurface::Bicubic);
    EXPECT_EQ(viewModeChanges, 1);
    EXPECT_EQ(metricChanges, 1);
    EXPECT_EQ(gainChanges, 1);
    EXPECT_EQ(referenceChanges, 1);
    EXPECT_EQ(filterChanges, 1);
}

class CountingActivitySink final : public platform::IRenderActivitySink {
public:
    void notifyFramePublished() noexcept override {
        frameNotifications.fetch_add(1U, std::memory_order_relaxed);
    }

    void notifyAckPublished() noexcept override {
        acknowledgementNotifications.fetch_add(1U, std::memory_order_relaxed);
    }

    std::atomic<std::uint64_t> frameNotifications{0U};
    std::atomic<std::uint64_t> acknowledgementNotifications{0U};
};

[[nodiscard]] application::FrameRequestContext
makeContext(const std::uint64_t requestId = 1U, const std::uint64_t playbackGeneration = 1U) {
    return application::FrameRequestContext{
        .playback =
            application::PlaybackRequestContext{
                .request =
                    application::RequestContext{
                        .sessionId = domain::SessionId{41U},
                        .sessionEpoch = domain::SessionEpoch{3U},
                        .requestId = domain::RequestId{requestId},
                    },
                .playbackGeneration = domain::PlaybackGeneration{playbackGeneration},
            },
        .deviceGeneration = domain::DeviceGeneration{1U},
    };
}

[[nodiscard]] std::optional<application::FrameHandle>
makeSolidFrame(platform::FrameResourceFactory& factory,
               const std::uint32_t width,
               const std::uint32_t height,
               const std::uint8_t y,
               const std::uint8_t u,
               const std::uint8_t v,
               const domain::ColorMetadata color) {
    const std::uint32_t uvRowBytes = ((width + 1U) / 2U) * 2U;
    const platform::Nv12FrameLayout layout{
        .width = width,
        .height = height,
        .yStride = width,
        .uvStride = uvRowBytes,
    };
    std::vector<std::uint8_t> yPlane(static_cast<std::size_t>(width) * height, y);
    std::vector<std::uint8_t> uvPlane(static_cast<std::size_t>(uvRowBytes) * ((height + 1U) / 2U));
    for (std::size_t index = 0U; index < uvPlane.size(); index += 2U) {
        uvPlane[index] = u;
        uvPlane[index + 1U] = v;
    }
    return factory.createCpuNv12(layout, color, std::span{yPlane}, std::span{uvPlane});
}

[[nodiscard]] std::optional<application::FrameHandle>
makeHorizontalLumaFrame(platform::FrameResourceFactory& factory,
                        const std::span<const std::uint8_t> columns,
                        const std::uint32_t height,
                        const domain::ColorMetadata color) {
    if (columns.empty() || height == 0U ||
        columns.size() > (std::numeric_limits<std::uint32_t>::max)()) {
        return std::nullopt;
    }
    const std::uint32_t width = static_cast<std::uint32_t>(columns.size());
    const std::uint32_t uvRowBytes = ((width + 1U) / 2U) * 2U;
    const platform::Nv12FrameLayout layout{
        .width = width,
        .height = height,
        .yStride = width,
        .uvStride = uvRowBytes,
    };
    std::vector<std::uint8_t> yPlane(static_cast<std::size_t>(width) * height);
    for (std::uint32_t row = 0U; row < height; ++row) {
        std::copy(columns.begin(),
                  columns.end(),
                  yPlane.begin() + static_cast<std::ptrdiff_t>(row * width));
    }
    std::vector<std::uint8_t> uvPlane(static_cast<std::size_t>(uvRowBytes) * ((height + 1U) / 2U),
                                      128U);
    return factory.createCpuNv12(layout, color, std::span{yPlane}, std::span{uvPlane});
}

[[nodiscard]] std::optional<application::FramePair>
makeHorizontalLumaPair(platform::FrameBudget& budget,
                       const domain::FrameId frameId,
                       const std::span<const std::uint8_t> columnsA,
                       const std::span<const std::uint8_t> columnsB) {
    if (columnsA.size() != columnsB.size()) {
        return std::nullopt;
    }
    const domain::ColorMetadata metadata{
        .matrix = domain::ColorMatrix::kBt709,
        .range = domain::ColorRange::kFull,
        .matrixInferred = false,
    };
    platform::FrameResourceFactory factory{budget};
    const std::optional<application::FrameHandle> frameA =
        makeHorizontalLumaFrame(factory, columnsA, 8U, metadata);
    const std::optional<application::FrameHandle> frameB =
        makeHorizontalLumaFrame(factory, columnsB, 8U, metadata);
    if (!frameA.has_value() || !frameB.has_value()) {
        return std::nullopt;
    }
    return application::FramePair::create(frameId,
                                          domain::MediaTime{0},
                                          *frameA,
                                          domain::MediaTime{0},
                                          *frameB,
                                          domain::MediaTime{0});
}

[[nodiscard]] std::optional<application::FramePair>
makeSolidPairWithMetadata(platform::FrameBudget& budget,
                          const domain::FrameId frameId,
                          const std::uint8_t leftY,
                          const std::uint8_t leftU,
                          const std::uint8_t leftV,
                          const domain::ColorMetadata& leftColor,
                          const std::uint8_t rightY,
                          const std::uint8_t rightU,
                          const std::uint8_t rightV,
                          const domain::ColorMetadata& rightColor) {
    platform::FrameResourceFactory factory{budget};
    const std::optional<application::FrameHandle> left =
        makeSolidFrame(factory, 16U, 9U, leftY, leftU, leftV, leftColor);
    const std::optional<application::FrameHandle> right =
        makeSolidFrame(factory, 12U, 9U, rightY, rightU, rightV, rightColor);
    if (!left.has_value() || !right.has_value()) {
        return std::nullopt;
    }
    return application::FramePair::create(frameId,
                                          domain::MediaTime{0},
                                          *left,
                                          domain::MediaTime{0},
                                          *right,
                                          domain::MediaTime{0});
}

[[nodiscard]] std::optional<application::FramePair> makeSolidPair(platform::FrameBudget& budget,
                                                                  const domain::FrameId frameId,
                                                                  const std::uint8_t leftY,
                                                                  const std::uint8_t rightY) {
    return makeSolidPairWithMetadata(budget,
                                     frameId,
                                     leftY,
                                     128U,
                                     128U,
                                     domain::ColorMetadata{
                                         .matrix = domain::ColorMatrix::kBt601,
                                         .range = domain::ColorRange::kLimited,
                                         .matrixInferred = false,
                                     },
                                     rightY,
                                     128U,
                                     128U,
                                     domain::ColorMetadata{
                                         .matrix = domain::ColorMatrix::kBt709,
                                         .range = domain::ColorRange::kFull,
                                         .matrixInferred = false,
                                     });
}

template <typename Predicate>
[[nodiscard]] bool waitUntil(Predicate predicate, const std::chrono::milliseconds timeout) {
    QElapsedTimer timer;
    timer.start();
    while (!predicate() && timer.elapsed() < timeout.count()) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
        QThread::yieldCurrentThread();
    }
    return predicate();
}

class SurfaceWarpHarness final {
public:
    SurfaceWarpHarness()
        : mailbox(std::make_shared<platform::FrameMailbox>(domain::DeviceGeneration{1U})),
          acknowledgementMailbox(std::make_shared<platform::PresentationAckMailbox>()),
          activitySink(std::make_shared<CountingActivitySink>()), surface(window.contentItem()) {
        QQuickGraphicsConfiguration configuration;
        configuration.setPreferSoftwareDevice(true);
        configuration.setDepthBufferFor2D(true);
        window.setGraphicsConfiguration(configuration);
        window.setFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowDoesNotAcceptFocus);
        window.setColor(Qt::magenta);
        window.setGeometry(100, 100, 101, 64);
        surface.setSize(QSizeF{101.0, 64.0});
        attached =
            surface.attachRendererServices(broker, mailbox, acknowledgementMailbox, activitySink);
    }

    ~SurfaceWarpHarness() {
        releaseRenderer();
        window.hide();
    }

    [[nodiscard]] bool start() {
        if (!attached) {
            return false;
        }
        window.show();
        if (!waitUntil([this] { return window.isExposed(); }, 10s)) {
            return false;
        }
        if (QScreen* const screen = QGuiApplication::primaryScreen()) {
            const QRect desktop = screen->virtualGeometry();
            window.setPosition(desktop.right() + 32, desktop.bottom() + 32);
        }
        surface.update();
        window.requestUpdate();
        const bool ready = waitUntil(
            [this] { return broker->currentGeneration() == domain::DeviceGeneration{1U}; }, 10s);
        if (ready) {
            static_cast<void>(broker->tryConsumeNotification());
        }
        return ready;
    }

    void requestRender() {
        surface.update();
        window.requestUpdate();
    }

    [[nodiscard]] QImage grab() {
        requestRender();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        return window.grabWindow();
    }

    [[nodiscard]] bool ensureOddPhysicalWidth() {
        constexpr int firstLogicalWidth = 101;
        constexpr int lastLogicalWidth = 109;
        for (int logicalWidth = firstLogicalWidth; logicalWidth <= lastLogicalWidth;
             ++logicalWidth) {
            window.resize(logicalWidth, window.height());
            surface.setWidth(static_cast<qreal>(logicalWidth));
            const QImage image = grab();
            if (!image.isNull() && (image.width() % 2) == 1) {
                return true;
            }
        }
        return false;
    }

    void releaseRenderer() {
        if (!attached) {
            return;
        }
        surface.detachRendererServices();
        attached = false;
        surface.update();
        window.requestUpdate();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        if (window.isExposed()) {
            // grabWindow() is a blocking scene-graph sync/render barrier. With services detached,
            // updatePaintNode() deletes the render node and releases its pinned front pair.
            static_cast<void>(window.grabWindow());
        }
        window.releaseResources();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    }

    std::shared_ptr<platform::GraphicsDeviceBroker> broker =
        std::make_shared<platform::GraphicsDeviceBroker>();
    std::shared_ptr<platform::FrameMailbox> mailbox;
    std::shared_ptr<platform::PresentationAckMailbox> acknowledgementMailbox;
    std::shared_ptr<CountingActivitySink> activitySink;
    QQuickWindow window;
    DualVideoSurface surface;
    bool attached = false;
};

[[nodiscard]] application::FramePairPresented
makeDummyAcknowledgement(const std::uint64_t requestId) {
    return application::FramePairPresented{
        .context = makeContext(requestId),
        .frameId = domain::FrameId{static_cast<std::int64_t>(requestId)},
    };
}

TEST(DualVideoSurfaceWarpTests, RendersUnequalAspectNv12AcrossAnOddSplitWithoutABlackSeam) {
    SurfaceWarpHarness harness;
    ASSERT_TRUE(harness.start());
    if (!harness.ensureOddPhysicalWidth()) {
        GTEST_SKIP() << "The active screen scale cannot produce an odd physical window width.";
    }

    auto budget = std::make_shared<platform::FrameBudget>(16U * 1024U * 1024U);
    platform::GpuTransferActor actor{budget, harness.broker, harness.mailbox, harness.activitySink};
    const application::FrameRequestContext context = makeContext();
    std::optional<application::FramePair> pair =
        makeSolidPair(*budget, domain::FrameId{7}, 235U, 128U);
    ASSERT_TRUE(pair.has_value());
    ASSERT_EQ(actor.submit(context, std::move(*pair)), platform::GpuTransferSubmitResult::Accepted);
    ASSERT_TRUE(actor.waitUntilIdle(5s));

    const QImage image = harness.grab().convertToFormat(QImage::Format_RGBA8888);
    ASSERT_FALSE(image.isNull());
    ASSERT_GE(image.width(), 3);
    ASSERT_GE(image.height(), 3);
    ASSERT_EQ(image.width() % 2, 1);
    const int split = image.width() / 2;
    const QColor left = image.pixelColor(split - 1, image.height() / 2);
    const QColor right = image.pixelColor(split, image.height() / 2);
    EXPECT_GT(left.red(), 240);
    EXPECT_GT(left.green(), 240);
    EXPECT_GT(left.blue(), 240);
    EXPECT_GT(right.red(), 115);
    EXPECT_LT(right.red(), 140);
    EXPECT_GT(right.green(), 115);
    EXPECT_LT(right.green(), 140);
    EXPECT_GT(right.blue(), 115);
    EXPECT_LT(right.blue(), 140);

    const std::optional<application::FramePairPresented> acknowledgement =
        harness.acknowledgementMailbox->tryPop();
    ASSERT_TRUE(acknowledgement.has_value());
    EXPECT_EQ(acknowledgement->frameId, domain::FrameId{7});
    EXPECT_EQ(harness.activitySink->acknowledgementNotifications.load(std::memory_order_relaxed),
              1U);

    ASSERT_TRUE(harness.mailbox->clear(context.playback));
    const QImage afterClear = harness.grab().convertToFormat(QImage::Format_RGBA8888);
    ASSERT_FALSE(afterClear.isNull());
    expectColorNear(afterClear.pixelColor(split - 1, afterClear.height() / 2), left, 1);
    expectColorNear(afterClear.pixelColor(split, afterClear.height() / 2), right, 1);
    EXPECT_FALSE(harness.acknowledgementMailbox->tryPop().has_value());

    harness.surface.setOpacity(0.5);
    const QImage translucent = harness.grab().convertToFormat(QImage::Format_RGBA8888);
    ASSERT_FALSE(translucent.isNull());
    // Video and its non-overlapping letterbox bars each blend exactly once over magenta.
    expectColorNear(translucent.pixelColor(translucent.width() / 4, translucent.height() / 2),
                    QColor{255, 128, 255},
                    2);
    expectColorNear(translucent.pixelColor(translucent.width() / 4, 1), QColor{128, 0, 128}, 2);

    harness.releaseRenderer();
    EXPECT_TRUE(actor.shutdown(2s));
}

TEST(DualVideoSurfaceWarpTests, CoversTheEntireBackgroundWithBlack) {
    SurfaceWarpHarness harness;
    ASSERT_TRUE(harness.start());

    const QImage image = harness.grab().convertToFormat(QImage::Format_RGBA8888);
    ASSERT_FALSE(image.isNull());
    const std::array<QPoint, 5U> samples{
        QPoint{1, 1},
        QPoint{image.width() - 2, 1},
        QPoint{1, image.height() - 2},
        QPoint{image.width() - 2, image.height() - 2},
        QPoint{image.width() / 2, image.height() / 2},
    };
    for (const QPoint& sample : samples) {
        expectColorNear(image.pixelColor(sample), QColor{0, 0, 0}, 1);
    }
    harness.releaseRenderer();
}

TEST(DualVideoSurfaceWarpTests, AppliesEveryMatrixAndRangeCombinationInThePixelShader) {
    SurfaceWarpHarness harness;
    ASSERT_TRUE(harness.start());

    constexpr std::uint8_t y = 128U;
    constexpr std::uint8_t u = 96U;
    constexpr std::uint8_t v = 160U;
    const domain::ColorMetadata bt601Full{
        .matrix = domain::ColorMatrix::kBt601,
        .range = domain::ColorRange::kFull,
        .matrixInferred = false,
    };
    const domain::ColorMetadata bt709Limited{
        .matrix = domain::ColorMatrix::kBt709,
        .range = domain::ColorRange::kLimited,
        .matrixInferred = false,
    };
    const domain::ColorMetadata bt601Limited{
        .matrix = domain::ColorMatrix::kBt601,
        .range = domain::ColorRange::kLimited,
        .matrixInferred = false,
    };
    const domain::ColorMetadata bt709Full{
        .matrix = domain::ColorMatrix::kBt709,
        .range = domain::ColorRange::kFull,
        .matrixInferred = false,
    };

    auto budget = std::make_shared<platform::FrameBudget>(16U * 1024U * 1024U);
    platform::GpuTransferActor actor{budget, harness.broker, harness.mailbox, harness.activitySink};
    const auto verifyPair = [&](const std::uint64_t requestId,
                                const domain::FrameId frameId,
                                const domain::ColorMetadata& leftMetadata,
                                const domain::ColorMetadata& rightMetadata) {
        std::optional<application::FramePair> pair = makeSolidPairWithMetadata(
            *budget, frameId, y, u, v, leftMetadata, y, u, v, rightMetadata);
        ASSERT_TRUE(pair.has_value());
        ASSERT_EQ(actor.submit(makeContext(requestId), std::move(*pair)),
                  platform::GpuTransferSubmitResult::Accepted);
        ASSERT_TRUE(actor.waitUntilIdle(5s));

        const QImage image = harness.grab().convertToFormat(QImage::Format_RGBA8888);
        ASSERT_FALSE(image.isNull());
        expectColorNear(image.pixelColor(image.width() / 4, image.height() / 2),
                        expectedNv12Color(leftMetadata, y, u, v));
        expectColorNear(image.pixelColor(image.width() * 3 / 4, image.height() / 2),
                        expectedNv12Color(rightMetadata, y, u, v));
        const std::optional<application::FramePairPresented> acknowledgement =
            harness.acknowledgementMailbox->tryPop();
        ASSERT_TRUE(acknowledgement.has_value());
        EXPECT_EQ(acknowledgement->frameId, frameId);
    };

    verifyPair(20U, domain::FrameId{20}, bt601Full, bt709Limited);
    verifyPair(21U, domain::FrameId{21}, bt601Limited, bt709Full);
    harness.releaseRenderer();
    EXPECT_TRUE(actor.shutdown(2s));
}

TEST(DualVideoSurfaceWarpTests,
     RendersEveryDifferenceMetricGainAndFilterWithoutDuplicateAcknowledgement) {
    SurfaceWarpHarness harness;
    harness.surface.setViewMode(DualVideoSurface::Difference);
    ASSERT_TRUE(harness.start());

    constexpr std::array<std::uint8_t, 3U> sampleA{128U, 64U, 192U};
    constexpr std::array<std::uint8_t, 3U> sampleB{96U, 160U, 80U};
    const domain::ColorMetadata metadataA{
        .matrix = domain::ColorMatrix::kBt601,
        .range = domain::ColorRange::kFull,
        .matrixInferred = false,
    };
    const domain::ColorMetadata metadataB{
        .matrix = domain::ColorMatrix::kBt709,
        .range = domain::ColorRange::kLimited,
        .matrixInferred = false,
    };

    auto budget = std::make_shared<platform::FrameBudget>(16U * 1024U * 1024U);
    platform::GpuTransferActor actor{budget, harness.broker, harness.mailbox, harness.activitySink};
    std::optional<application::FramePair> pair = makeSolidPairWithMetadata(*budget,
                                                                           domain::FrameId{30},
                                                                           sampleA[0U],
                                                                           sampleA[1U],
                                                                           sampleA[2U],
                                                                           metadataA,
                                                                           sampleB[0U],
                                                                           sampleB[1U],
                                                                           sampleB[2U],
                                                                           metadataB);
    ASSERT_TRUE(pair.has_value());
    ASSERT_EQ(actor.submit(makeContext(30U), std::move(*pair)),
              platform::GpuTransferSubmitResult::Accepted);
    ASSERT_TRUE(actor.waitUntilIdle(5s));

    const QImage first = harness.grab().convertToFormat(QImage::Format_RGBA8888);
    ASSERT_FALSE(first.isNull());
    const QPoint samplePoint{first.width() / 2, first.height() / 2};
    expectColorNear(first.pixelColor(samplePoint),
                    expectedDifferenceColor(metadataA,
                                            sampleA,
                                            metadataB,
                                            sampleB,
                                            DualVideoSurface::RgbAbsolute,
                                            DualVideoSurface::Gain1x),
                    4);
    const std::optional<application::FramePairPresented> acknowledgement =
        harness.acknowledgementMailbox->tryPop();
    ASSERT_TRUE(acknowledgement.has_value());
    EXPECT_EQ(acknowledgement->frameId, domain::FrameId{30});

    constexpr std::array metrics{
        DualVideoSurface::RgbAbsolute,
        DualVideoSurface::Luma,
        DualVideoSurface::Chroma,
        DualVideoSurface::Heatmap,
    };
    constexpr std::array gains{
        DualVideoSurface::Gain1x,
        DualVideoSurface::Gain2x,
        DualVideoSurface::Gain4x,
        DualVideoSurface::Gain8x,
        DualVideoSurface::Gain16x,
    };
    for (const DualVideoSurface::DifferenceMetric metric : metrics) {
        harness.surface.setDifferenceMetric(metric);
        for (const DualVideoSurface::DifferenceGain gain : gains) {
            harness.surface.setDifferenceGain(gain);
            const QImage image = harness.grab().convertToFormat(QImage::Format_RGBA8888);
            ASSERT_FALSE(image.isNull());
            expectColorNear(
                image.pixelColor(image.width() / 2, image.height() / 2),
                expectedDifferenceColor(metadataA, sampleA, metadataB, sampleB, metric, gain),
                5);
        }
    }

    harness.surface.setDifferenceMetric(DualVideoSurface::RgbAbsolute);
    harness.surface.setDifferenceGain(DualVideoSurface::Gain1x);
    constexpr std::array filters{
        DualVideoSurface::Nearest,
        DualVideoSurface::Bilinear,
        DualVideoSurface::Bicubic,
    };
    for (const DualVideoSurface::DifferenceFilter filter : filters) {
        harness.surface.setDifferenceFilter(filter);
        const QImage image = harness.grab().convertToFormat(QImage::Format_RGBA8888);
        ASSERT_FALSE(image.isNull());
        expectColorNear(image.pixelColor(image.width() / 2, image.height() / 2),
                        expectedDifferenceColor(metadataA,
                                                sampleA,
                                                metadataB,
                                                sampleB,
                                                DualVideoSurface::RgbAbsolute,
                                                DualVideoSurface::Gain1x),
                        5);
    }

    const QColor opaqueDifference = expectedDifferenceColor(metadataA,
                                                            sampleA,
                                                            metadataB,
                                                            sampleB,
                                                            DualVideoSurface::RgbAbsolute,
                                                            DualVideoSurface::Gain1x);
    harness.surface.setOpacity(0.5);
    const QImage translucent = harness.grab().convertToFormat(QImage::Format_RGBA8888);
    ASSERT_FALSE(translucent.isNull());
    expectColorNear(translucent.pixelColor(translucent.width() / 2, translucent.height() / 2),
                    QColor{(opaqueDifference.red() + 255) / 2,
                           opaqueDifference.green() / 2,
                           (opaqueDifference.blue() + 255) / 2},
                    4);

    EXPECT_FALSE(harness.acknowledgementMailbox->tryPop().has_value());
    EXPECT_EQ(harness.activitySink->acknowledgementNotifications.load(std::memory_order_relaxed),
              1U);
    harness.releaseRenderer();
    EXPECT_TRUE(actor.shutdown(2s));
}

TEST(DualVideoSurfaceWarpTests, UsesTheSelectedReferenceCanvasForUnequalExtents) {
    SurfaceWarpHarness harness;
    harness.surface.setViewMode(DualVideoSurface::Difference);
    harness.surface.setDifferenceGain(DualVideoSurface::Gain4x);
    ASSERT_TRUE(harness.start());

    auto budget = std::make_shared<platform::FrameBudget>(16U * 1024U * 1024U);
    platform::GpuTransferActor actor{budget, harness.broker, harness.mailbox, harness.activitySink};
    std::optional<application::FramePair> pair =
        makeSolidPair(*budget, domain::FrameId{31}, 235U, 64U);
    ASSERT_TRUE(pair.has_value());
    ASSERT_EQ(actor.submit(makeContext(31U), std::move(*pair)),
              platform::GpuTransferSubmitResult::Accepted);
    ASSERT_TRUE(actor.waitUntilIdle(5s));

    const QImage referenceA = harness.grab().convertToFormat(QImage::Format_RGBA8888);
    ASSERT_FALSE(referenceA.isNull());
    const QColor topA = referenceA.pixelColor(referenceA.width() / 2, 1);
    const QColor leftA = referenceA.pixelColor(1, referenceA.height() / 2);
    expectColorNear(topA, QColor{0, 0, 0}, 2);
    EXPECT_GT(leftA.red() + leftA.green() + leftA.blue(), 20);
    ASSERT_TRUE(harness.acknowledgementMailbox->tryPop().has_value());

    harness.surface.setDifferenceReference(DualVideoSurface::ReferenceB);
    const QImage referenceB = harness.grab().convertToFormat(QImage::Format_RGBA8888);
    ASSERT_FALSE(referenceB.isNull());
    const QColor topB = referenceB.pixelColor(referenceB.width() / 2, 1);
    const QColor leftB = referenceB.pixelColor(1, referenceB.height() / 2);
    EXPECT_GT(topB.red() + topB.green() + topB.blue(), 20);
    expectColorNear(leftB, QColor{0, 0, 0}, 2);
    EXPECT_FALSE(harness.acknowledgementMailbox->tryPop().has_value());
    EXPECT_EQ(harness.activitySink->acknowledgementNotifications.load(std::memory_order_relaxed),
              1U);

    harness.releaseRenderer();
    EXPECT_TRUE(actor.shutdown(2s));
}

TEST(DualVideoSurfaceWarpTests, RendersIdenticalFramesBlackForEveryDifferenceMetric) {
    SurfaceWarpHarness harness;
    harness.surface.setViewMode(DualVideoSurface::Difference);
    harness.surface.setDifferenceGain(DualVideoSurface::Gain16x);
    ASSERT_TRUE(harness.start());

    const domain::ColorMetadata metadata{
        .matrix = domain::ColorMatrix::kBt709,
        .range = domain::ColorRange::kLimited,
        .matrixInferred = false,
    };
    auto budget = std::make_shared<platform::FrameBudget>(16U * 1024U * 1024U);
    platform::GpuTransferActor actor{budget, harness.broker, harness.mailbox, harness.activitySink};
    std::optional<application::FramePair> pair = makeSolidPairWithMetadata(
        *budget, domain::FrameId{32}, 128U, 91U, 173U, metadata, 128U, 91U, 173U, metadata);
    ASSERT_TRUE(pair.has_value());
    ASSERT_EQ(actor.submit(makeContext(32U), std::move(*pair)),
              platform::GpuTransferSubmitResult::Accepted);
    ASSERT_TRUE(actor.waitUntilIdle(5s));

    constexpr std::array metrics{
        DualVideoSurface::RgbAbsolute,
        DualVideoSurface::Luma,
        DualVideoSurface::Chroma,
        DualVideoSurface::Heatmap,
    };
    for (const DualVideoSurface::DifferenceMetric metric : metrics) {
        harness.surface.setDifferenceMetric(metric);
        const QImage image = harness.grab().convertToFormat(QImage::Format_RGBA8888);
        ASSERT_FALSE(image.isNull());
        expectColorNear(
            image.pixelColor(image.width() / 2, image.height() / 2), QColor{0, 0, 0}, 2);
    }
    const std::optional<application::FramePairPresented> acknowledgement =
        harness.acknowledgementMailbox->tryPop();
    ASSERT_TRUE(acknowledgement.has_value());
    EXPECT_EQ(acknowledgement->frameId, domain::FrameId{32});
    EXPECT_FALSE(harness.acknowledgementMailbox->tryPop().has_value());
    EXPECT_EQ(harness.activitySink->acknowledgementNotifications.load(std::memory_order_relaxed),
              1U);

    harness.releaseRenderer();
    EXPECT_TRUE(actor.shutdown(2s));
}

TEST(DualVideoSurfaceWarpTests, AppliesDistinctDeterministicSpatialFilters) {
    SurfaceWarpHarness harness;
    harness.surface.setViewMode(DualVideoSurface::Difference);
    ASSERT_TRUE(harness.start());

    constexpr std::array<std::uint8_t, 8U> patternA{0U, 255U, 0U, 255U, 0U, 255U, 0U, 255U};
    constexpr std::array<std::uint8_t, 8U> patternB{};
    auto budget = std::make_shared<platform::FrameBudget>(16U * 1024U * 1024U);
    platform::GpuTransferActor actor{budget, harness.broker, harness.mailbox, harness.activitySink};
    std::optional<application::FramePair> pair = makeHorizontalLumaPair(
        *budget, domain::FrameId{33}, std::span{patternA}, std::span{patternB});
    ASSERT_TRUE(pair.has_value());
    ASSERT_EQ(actor.submit(makeContext(33U), std::move(*pair)),
              platform::GpuTransferSubmitResult::Accepted);
    ASSERT_TRUE(actor.waitUntilIdle(5s));

    harness.surface.setDifferenceFilter(DualVideoSurface::Nearest);
    const QImage nearest = harness.grab().convertToFormat(QImage::Format_RGBA8888);
    harness.surface.setDifferenceFilter(DualVideoSurface::Bilinear);
    const QImage bilinear = harness.grab().convertToFormat(QImage::Format_RGBA8888);
    harness.surface.setDifferenceFilter(DualVideoSurface::Bicubic);
    const QImage bicubic = harness.grab().convertToFormat(QImage::Format_RGBA8888);
    const QImage bicubicAgain = harness.grab().convertToFormat(QImage::Format_RGBA8888);
    ASSERT_FALSE(nearest.isNull());
    ASSERT_FALSE(bilinear.isNull());
    ASSERT_FALSE(bicubic.isNull());
    ASSERT_FALSE(bicubicAgain.isNull());
    EXPECT_GT(maximumRgbDifference(nearest, bilinear), 20);
    EXPECT_GT(maximumRgbDifference(bilinear, bicubic), 2);
    EXPECT_LE(maximumRgbDifference(bicubic, bicubicAgain), 1);

    const std::optional<application::FramePairPresented> acknowledgement =
        harness.acknowledgementMailbox->tryPop();
    ASSERT_TRUE(acknowledgement.has_value());
    EXPECT_EQ(acknowledgement->frameId, domain::FrameId{33});
    EXPECT_FALSE(harness.acknowledgementMailbox->tryPop().has_value());
    EXPECT_EQ(harness.activitySink->acknowledgementNotifications.load(std::memory_order_relaxed),
              1U);

    harness.releaseRenderer();
    EXPECT_TRUE(actor.shutdown(2s));
}

TEST(DualVideoSurfaceWarpTests, HonorsAncestorScissorInDifferenceMode) {
    SurfaceWarpHarness harness;
    QQuickItem clipper{harness.window.contentItem()};
    clipper.setSize(QSizeF{51.0, 64.0});
    clipper.setClip(true);
    harness.surface.setParentItem(&clipper);
    harness.surface.setViewMode(DualVideoSurface::Difference);
    harness.surface.setDifferenceGain(DualVideoSurface::Gain4x);
    ASSERT_TRUE(harness.start());

    auto budget = std::make_shared<platform::FrameBudget>(16U * 1024U * 1024U);
    platform::GpuTransferActor actor{budget, harness.broker, harness.mailbox, harness.activitySink};
    std::optional<application::FramePair> pair =
        makeSolidPair(*budget, domain::FrameId{34}, 235U, 64U);
    ASSERT_TRUE(pair.has_value());
    ASSERT_EQ(actor.submit(makeContext(34U), std::move(*pair)),
              platform::GpuTransferSubmitResult::Accepted);
    ASSERT_TRUE(actor.waitUntilIdle(5s));

    const QImage image = harness.grab().convertToFormat(QImage::Format_RGBA8888);
    ASSERT_FALSE(image.isNull());
    const QColor inside = image.pixelColor(image.width() / 4, image.height() / 2);
    const QColor outside = image.pixelColor(image.width() * 3 / 4, image.height() / 2);
    EXPECT_GT(inside.red() + inside.green() + inside.blue(), 20);
    expectColorNear(outside, QColor{255, 0, 255}, 2);
    ASSERT_TRUE(harness.acknowledgementMailbox->tryPop().has_value());
    EXPECT_FALSE(harness.acknowledgementMailbox->tryPop().has_value());

    harness.releaseRenderer();
    EXPECT_TRUE(actor.shutdown(2s));
}

TEST(DualVideoSurfaceWarpTests, AcknowledgesOnlyTheLatestReplacementAndRetriesAFullQueueOnce) {
    SurfaceWarpHarness harness;
    ASSERT_TRUE(harness.start());

    ASSERT_EQ(harness.acknowledgementMailbox->tryPush(makeDummyAcknowledgement(91U)),
              platform::PresentationAckPushResult::Accepted);
    ASSERT_EQ(harness.acknowledgementMailbox->tryPush(makeDummyAcknowledgement(92U)),
              platform::PresentationAckPushResult::Accepted);

    auto budget = std::make_shared<platform::FrameBudget>(16U * 1024U * 1024U);
    platform::GpuTransferActor actor{budget, harness.broker, harness.mailbox, harness.activitySink};
    std::optional<application::FramePair> first =
        makeSolidPair(*budget, domain::FrameId{10}, 64U, 64U);
    ASSERT_TRUE(first.has_value());
    ASSERT_EQ(actor.submit(makeContext(10U), std::move(*first)),
              platform::GpuTransferSubmitResult::Accepted);
    ASSERT_TRUE(actor.waitUntilIdle(5s));

    const QImage firstImage = harness.grab().convertToFormat(QImage::Format_RGBA8888);
    ASSERT_FALSE(firstImage.isNull());
    EXPECT_LT(firstImage.pixelColor(firstImage.width() / 4, firstImage.height() / 2).red(), 90);
    EXPECT_EQ(harness.activitySink->acknowledgementNotifications.load(std::memory_order_relaxed),
              0U);

    std::optional<application::FramePair> second =
        makeSolidPair(*budget, domain::FrameId{11}, 128U, 128U);
    ASSERT_TRUE(second.has_value());
    ASSERT_EQ(actor.submit(makeContext(11U), std::move(*second)),
              platform::GpuTransferSubmitResult::Accepted);
    ASSERT_TRUE(actor.waitUntilIdle(5s));
    std::optional<application::FramePair> third =
        makeSolidPair(*budget, domain::FrameId{12}, 192U, 192U);
    ASSERT_TRUE(third.has_value());
    ASSERT_EQ(actor.submit(makeContext(12U), std::move(*third)),
              platform::GpuTransferSubmitResult::Accepted);
    ASSERT_TRUE(actor.waitUntilIdle(5s));

    const QImage backpressured = harness.grab().convertToFormat(QImage::Format_RGBA8888);
    ASSERT_FALSE(backpressured.isNull());
    EXPECT_LT(backpressured.pixelColor(backpressured.width() / 4, backpressured.height() / 2).red(),
              90);
    EXPECT_EQ(harness.activitySink->acknowledgementNotifications.load(std::memory_order_relaxed),
              0U);
    const std::optional<application::FramePairPresented> dummyA =
        harness.acknowledgementMailbox->tryPop();
    ASSERT_TRUE(dummyA.has_value());
    EXPECT_EQ(dummyA->frameId, domain::FrameId{91});

    const QImage latest = harness.grab().convertToFormat(QImage::Format_RGBA8888);
    ASSERT_FALSE(latest.isNull());
    EXPECT_GT(latest.pixelColor(latest.width() / 4, latest.height() / 2).red(), 180);
    EXPECT_EQ(harness.activitySink->acknowledgementNotifications.load(std::memory_order_relaxed),
              1U);
    const std::optional<application::FramePairPresented> dummyB =
        harness.acknowledgementMailbox->tryPop();
    ASSERT_TRUE(dummyB.has_value());
    EXPECT_EQ(dummyB->frameId, domain::FrameId{92});
    const std::optional<application::FramePairPresented> presented =
        harness.acknowledgementMailbox->tryPop();
    ASSERT_TRUE(presented.has_value());
    EXPECT_EQ(presented->frameId, domain::FrameId{10});

    ASSERT_FALSE(harness.grab().isNull());
    const std::optional<application::FramePairPresented> latestPresented =
        harness.acknowledgementMailbox->tryPop();
    ASSERT_TRUE(latestPresented.has_value());
    EXPECT_EQ(latestPresented->frameId, domain::FrameId{12});
    EXPECT_FALSE(harness.acknowledgementMailbox->tryPop().has_value());
    EXPECT_EQ(harness.activitySink->acknowledgementNotifications.load(std::memory_order_relaxed),
              2U);
    harness.releaseRenderer();
    EXPECT_TRUE(actor.shutdown(2s));
}

} // namespace
} // namespace dvs::ui
