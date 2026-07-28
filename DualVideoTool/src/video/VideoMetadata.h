#pragma once

#include <cstdint>
#include <QMetaType>
#include <string>

struct VideoMetadata {
    std::string path;
    int width = 0;
    int height = 0;
    double fps = 0.0;
    int64_t frameCount = 0;
    double duration = 0.0;
    std::string codecName;
    std::string pixelFormat;

    bool isValid() const { return frameCount > 0 && width > 0 && height > 0; }
};

Q_DECLARE_METATYPE(VideoMetadata)
