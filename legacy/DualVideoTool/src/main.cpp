#include "app/MainWindow.h"
#include "playback/ProxyBuilder.h"
#include "smoke/SmokeTest.h"
#include "utils/FFmpegUtils.h"

#include <QApplication>
#include <QMessageBox>
#include <QDir>
#include <QFile>
#include <QStandardPaths>

#ifdef _WIN32
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}
#endif

static bool checkRuntimeEnvironment() {
    // Check FFmpeg codec availability
    const AVCodec* h264 = avcodec_find_encoder(AV_CODEC_ID_H264);
    if (!h264) {
        QMessageBox::critical(nullptr, "运行时错误",
            "未找到 H.264 编码器。\n\n"
            "FFmpeg 编解码库可能未正确安装。");
        return false;
    }

    const AVCodec* h264dec = avcodec_find_decoder(AV_CODEC_ID_H264);
    if (!h264dec) {
        QMessageBox::critical(nullptr, "运行时错误",
            "未找到 H.264 解码器。\n\n"
            "FFmpeg 编解码库可能未正确安装。");
        return false;
    }

    QDir appDir(QCoreApplication::applicationDirPath());
    if (ProxyBuilder::ffmpegExecutable().isEmpty()) {
        QMessageBox::critical(nullptr, "运行时错误",
            "未找到 ffmpeg.exe。\n\n"
            "V1.8 使用代理视频播放，生成代理需要 ffmpeg.exe。\n"
            "请将 ffmpeg.exe 放到程序目录的 tools 文件夹，或把 ffmpeg 加入 PATH。");
        return false;
    }

    if (!appDir.exists("logs") && !appDir.mkpath("logs")) {
        QMessageBox::critical(nullptr, "运行时错误",
            "无法创建日志目录。\n\n"
            "请确认程序所在目录可写。");
        return false;
    }

    QString logTestPath = appDir.filePath("logs/.write_test");
    QFile logTest(logTestPath);
    if (!logTest.open(QIODevice::WriteOnly)) {
        QMessageBox::critical(nullptr, "运行时错误",
            "日志目录不可写。\n\n"
            "请将程序移动到可写目录后重试。");
        return false;
    }
    logTest.close();
    QFile::remove(logTestPath);

    QDir tempDir(QDir::tempPath());
    QString tempTestPath = tempDir.filePath("DualVideoTool_write_test.tmp");
    QFile tempTest(tempTestPath);
    if (!tempTest.open(QIODevice::WriteOnly)) {
        QMessageBox::critical(nullptr, "运行时错误",
            "系统临时目录不可写。\n\n"
            "导出和解码缓存可能无法正常工作。");
        return false;
    }
    tempTest.close();
    QFile::remove(tempTestPath);

    return true;
}

int main(int argc, char* argv[]) {
    FFmpegUtils::initFFmpeg();

    QApplication app(argc, argv);
    app.setApplicationName("DualVideoTool");
    app.setApplicationVersion("1.8.0");

    QStringList arguments = app.arguments();
    if (arguments.contains("--smoke-test") ||
        arguments.contains("--smoke-failure-test") ||
        arguments.contains("--smoke-batch-export")) {
        return SmokeTest::run(arguments);
    }

    if (arguments.contains("--startup-check")) {
        return checkRuntimeEnvironment() ? 0 : 1;
    }

    // Startup self-test
    if (!checkRuntimeEnvironment()) {
        return 1;
    }

    MainWindow window;
    window.show();

    return app.exec();
}
