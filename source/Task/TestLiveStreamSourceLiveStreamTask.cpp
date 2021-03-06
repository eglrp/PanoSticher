#include "PanoramaTask.h"
#include <opencv2/core/core.hpp>
#include <opencv2/imgproc/imgproc.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <iostream>
#include <thread>

struct ShowTiledImages
{
    ShowTiledImages() : hasInit(false) {};
    bool init(int width_, int height_, int numImages_)
    {
        origWidth = width_;
        origHeight = height_;
        numImages = numImages_;

        showWidth = 480;
        showHeight = origHeight * double(showWidth) / double(origWidth) + 0.5;

        int totalWidth = numImages * showWidth;
        if (totalWidth <= screenWidth)
            tileWidth = numImages * showWidth;
        else
            tileWidth = screenWidth;
        tileHeight = ((totalWidth + screenWidth - 1) / screenWidth) * showHeight;

        int horiNumImages = screenWidth / showWidth;
        locations.resize(numImages);
        for (int i = 0; i < numImages; i++)
        {
            int gridx = i % horiNumImages;
            int gridy = i / horiNumImages;
            locations[i] = cv::Rect(gridx * showWidth, gridy * showHeight, showWidth, showHeight);
        }

        hasInit = true;
        return true;
    }
    bool show(const std::string& winName, const std::vector<cv::Mat>& images)
    {
        if (!hasInit)
            return false;

        if (images.size() != numImages)
            return false;

        for (int i = 0; i < numImages; i++)
        {
            if (images[i].rows != origHeight || images[i].cols != origWidth || images[i].type() != CV_8UC4)
                return false;
        }

        tileImage.create(tileHeight, tileWidth, CV_8UC4);
        for (int i = 0; i < numImages; i++)
        {
            cv::Mat curr = tileImage(locations[i]);
            cv::resize(images[i], curr, cv::Size(showWidth, showHeight), 0, 0, CV_INTER_NN);
        }
        cv::imshow(winName, tileImage);

        return true;
    }

    const int screenWidth = 1920;
    int origWidth, origHeight;
    int showWidth, showHeight;
    int numImages;
    int tileWidth, tileHeight;
    cv::Mat tileImage;
    std::vector<cv::Rect> locations;
    bool hasInit;
};

cv::Size srcSize(1920, 1080);

int frameRate;
int audioBitRate = 96000;
cv::Size stitchFrameSize(1440, 720);

cv::Size streamFrameSize(1440, 720);
int streamBitRate;
std::string streamEncoder;
std::string streamEncodePreset;
std::string streamURL;

int saveFile;
cv::Size fileFrameSize(1440, 720);
int fileDuration;
int fileBitRate;
std::string fileEncoder;
std::string fileEncodePreset;

std::string cameraParamPath;
std::string cameraModel;

int numCameras;
int audioOpened;
int waitTime = 30;

bool highQualityBlend = true;

ShowTiledImages showTiledImages;
PanoramaLiveStreamTask task;

void showVideoSources()
{
    size_t id = std::this_thread::get_id().hash();
    printf("Thread %s [%8x] started\n", __FUNCTION__, id);

    std::vector<avp::AudioVideoFrame2> frames;
    std::vector<cv::Mat> images(numCameras);
    while (true)
    {
        if (task.hasFinished())
            break;
        task.getVideoSourceFrames(frames);
        if (frames.size() == numCameras)
        {
            for (int i = 0; i < numCameras; i++)
            {
                images[i] = cv::Mat(frames[i].height, frames[i].width,
                    frames[i].pixelType == avp::PixelTypeBGR24 ? CV_8UC3 : CV_8UC4, frames[i].data[0], frames[i].steps[0]);
            }
            showTiledImages.show("src images", images);
            int key = cv::waitKey(waitTime / 2);
            if (key == 'q')
            {
                task.closeAll();
                break;
            }
        }
    }

    printf("Thread %s [%8x] end\n", __FUNCTION__, id);
}

void showVideoResult()
{
    size_t id = std::this_thread::get_id().hash();
    printf("Thread %s [%8x] started\n", __FUNCTION__, id);

    avp::AudioVideoFrame2 frame;
    while (true)
    {
        if (task.hasFinished())
            break;
        task.getStitchedVideoFrame(frame);
        if (frame.data[0])
        {
            cv::Mat show(frame.height, frame.width, frame.pixelType == avp::PixelTypeBGR24 ? CV_8UC3 : CV_8UC4, frame.data[0], frame.steps[0]);
            cv::imshow("result", show);
            int key = cv::waitKey(waitTime / 2);
            if (key == 'q')
            {
                task.closeAll();
                break;
            }
        }
    }

    printf("Thread %s [%8x] end\n", __FUNCTION__, id);
}

int main(int argc, char* argv[])
{
    const char* keys =
        "{camera_model                | dualgopro     | camera model}"
        "{camera_param_path           | null          | camera parameter file path, may be xml file path or ptgui pts file path}"
        "{num_cameras                 | 2             | number of cameras}"
        "{camera_width                | 1920          | camera picture width}"
        "{camera_height               | 1080          | camera picture height}"
        "{frames_per_second           | 30            | camera frame rate}"
        "{pano_stitch_frame_width     | 1440          | pano video picture width}"
        "{pano_stitch_frame_height    | 720           | pano video picture height}"
        "{pano_stream_frame_width     | 1440          | pano video live stream picture width}"
        "{pano_stream_frame_height    | 720           | pano video live stream picture height}"
        "{pano_stream_bits_per_second | 1000000       | pano video live stream bits per second}"
        "{pano_stream_encoder         | h264          | pano video live stream encoder}"
        "{pano_stream_encode_preset   | veryfast      | pano video live stream encode preset}"
        "{pano_stream_url             | rtsp://127.0.0.1/test.sdp | pano live stream address}"
        "{pano_save_file              | false         | whether to save audio video to local hard disk}"
        "{pano_file_duration          | 60            | each local pano audio video file duration in seconds}"
        "{pano_file_frame_width       | 1440          | pano video local file picture width}"
        "{pano_file_frame_height      | 720           | pano video local file picture height}"
        "{pano_file_bits_per_second   | 1000000       | pano video local file bits per second}"
        "{pano_file_encoder           | h264          | pano video local file encoder}"
        "{pano_file_encode_preset     | veryfast      | pano video local file encode preset}"
        "{enable_audio                | false         | enable audio or not}"
        "{enable_interactive_select_devices | false   | enable interactice select devices}"
        "{high_quality_blend          | false         | use multiband blend}";

    cv::CommandLineParser parser(argc, argv, keys);

    stitchFrameSize.width = parser.get<int>("pano_stitch_frame_width");
    stitchFrameSize.height = parser.get<int>("pano_stitch_frame_height");
    if (stitchFrameSize.width <= 0 || stitchFrameSize.height <= 0 ||
        (stitchFrameSize.width & 1) || (stitchFrameSize.height & 1) ||
        (stitchFrameSize.width != stitchFrameSize.height * 2))
    {
        printf("pano_stitch_frame_width and pano_stitch_frame_height should be positive even numbers, "
            "and pano_stitch_frame_width should be two times of pano_stitch_frame_height\n");
        return 0;
    }

    bool ok;    
    std::vector<std::string> urls;
    urls.push_back("rtsp://192.168.1.204:554/stream1");
    urls.push_back("rtsp://192.168.1.205:554/stream1");
    ok = task.openVideoStreams(urls);
    if (!ok)
    {
        printf("Could not open urls\n");
        return 0;
    }

    highQualityBlend = parser.get<bool>("high_quality_blend");
    cameraParamPath = parser.get<std::string>("camera_param_path");
    cameraParamPath = "dualgopro.pts";
    highQualityBlend = true;
    if (cameraParamPath.size() && cameraParamPath != "null")
    {
        ok = task.beginVideoStitch(cameraParamPath, stitchFrameSize.width, stitchFrameSize.height, highQualityBlend);
        if (!ok)
        {
            printf("Could not prepare for panorama render\n");
            return 0;
        }
    }
    else
    {
        printf("camera_param_path empty, no stitch\n");
    }

    streamURL = parser.get<std::string>("pano_stream_url");
    streamURL = "rtsp://127.0.0.1/test.sdp";
    if (streamURL.size() && streamURL != "null")
    {
        streamFrameSize.width = parser.get<int>("pano_stream_frame_width");
        streamFrameSize.height = parser.get<int>("pano_stream_frame_height");
        if (streamFrameSize.width <= 0 || streamFrameSize.height <= 0 ||
            (streamFrameSize.width & 1) || (streamFrameSize.height & 1) ||
            (streamFrameSize.width != streamFrameSize.height * 2))
        {
            printf("pano_stream_frame_width and pano_stream_frame_height should be positive even numbers, "
                "and pano_stream_frame_width should be two times of pano_stream_frame_height\n");
            return 0;
        }

        streamBitRate = parser.get<int>("pano_stream_bits_per_second");
        streamEncoder = parser.get<std::string>("pano_stream_encoder");
        if (streamEncoder != "h264_qsv")
            streamEncoder = "h264";
        streamEncodePreset = parser.get<std::string>("pano_stream_encode_preset");
        if (streamEncodePreset != "ultrafast" || streamEncodePreset != "superfast" ||
            streamEncodePreset != "veryfast" || streamEncodePreset != "faster" ||
            streamEncodePreset != "fast" || streamEncodePreset != "medium" || streamEncodePreset != "slow" ||
            streamEncodePreset != "slower" || streamEncodePreset != "veryslow")
            streamEncodePreset = "veryfast";

        ok = task.openLiveStream(streamURL, streamFrameSize.width, streamFrameSize.height,
            streamBitRate, streamEncoder, streamEncodePreset, 96000);
        if (!ok)
        {
            printf("Could not open rtmp streaming url with frame rate = %d and bit rate = %d\n", frameRate, streamBitRate);
            return 0;
        }
    }
    else
    {
        printf("pano_stream_url empty, no live stream\n");
    }

    saveFile = parser.get<bool>("pano_save_file");
    if (saveFile)
    {
        fileFrameSize.width = parser.get<int>("pano_file_frame_width");
        fileFrameSize.height = parser.get<int>("pano_file_frame_height");
        if (fileFrameSize.width <= 0 || fileFrameSize.height <= 0 ||
            (fileFrameSize.width & 1) || (fileFrameSize.height & 1) ||
            (fileFrameSize.width != fileFrameSize.height * 2))
        {
            printf("pano_file_frame_width and pano_file_frame_height should be positive even numbers, "
                "and pano_file_frame_width should be two times of pano_file_frame_height\n");
            return 0;
        }

        fileDuration = parser.get<int>("pano_file_duration");
        fileBitRate = parser.get<int>("pano_file_bits_per_second");
        fileEncoder = parser.get<std::string>("pano_file_encoder");
        if (fileEncoder != "h264_qsv")
            fileEncoder = "h264";
        fileEncodePreset = parser.get<std::string>("pano_file_encode_preset");
        if (fileEncodePreset != "ultrafast" || fileEncodePreset != "superfast" ||
            fileEncodePreset != "veryfast" || fileEncodePreset != "faster" ||
            fileEncodePreset != "fast" || fileEncodePreset != "medium" || fileEncodePreset != "slow" ||
            fileEncodePreset != "slower" || fileEncodePreset != "veryslow")
            fileEncodePreset = "veryfast";

        task.beginSaveToDisk(".", fileFrameSize.width, fileFrameSize.height,
            fileBitRate, fileEncoder, fileEncodePreset, 96000, fileDuration);
    }

    frameRate = 15;
    waitTime = std::max(5.0, 1000.0 / frameRate - 5);

    numCameras = 2;
    showTiledImages.init(/*srcSize.width*/2560, /*srcSize.height*/1440, /*numCameras*/2);
    std::thread svr(showVideoResult);    
    std::thread svs(showVideoSources);
    svr.join();
    svs.join();

    task.closeVideoDevices();
    task.closeAudioDevice();
    task.stopVideoStitch();
    task.closeLiveStream();
    task.stopSaveToDisk();

    return 0;
}