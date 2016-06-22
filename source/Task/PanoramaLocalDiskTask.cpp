#include "PanoramaTask.h"
#include "PanoramaTaskUtil.h"
#include "ConcurrentQueue.h"
#include "ZBlend.h"
#include "ZReproject.h"
#include "RicohUtil.h"
#include "PinnedMemoryPool.h"
#include "SharedAudioVideoFramePool.h"
#include "CudaPanoramaTaskUtil.h"
#include "Timer.h"
#include "Image.h"
#include "opencv2/highgui.hpp"

struct CPUPanoramaLocalDiskTask::Impl
{
    Impl();
    ~Impl();
    bool init(const std::vector<std::string>& srcVideoFiles, const std::vector<int> offsets, int audioIndex,
        const std::string& cameraParamFile, const std::string& customMaskFile, const std::string& dstVideoFile, int dstWidth, int dstHeight,
        int dstVideoBitRate, const std::string& dstVideoEncoder, const std::string& dstVideoPreset, 
        int dstVideoMaxFrameCount);
    bool start();
    void waitForCompletion();
    int getProgress() const;
    void cancel();

    void getLastSyncErrorMessage(std::string& message) const;
    bool hasAsyncErrorMessage() const;
    void getLastAsyncErrorMessage(std::string& message);

    void run();
    void clear();

    int numVideos;
    int audioIndex;
    cv::Size srcSize, dstSize;
    std::vector<avp::AudioVideoReader3> readers;
    std::vector<cv::Mat> dstSrcMaps, dstMasks, dstUniqueMasks, currMasks;
    int useCustomMasks;
    std::vector<CustomIntervaledMasks> customMasks;
    TilingMultibandBlendFast blender;
    std::vector<cv::Mat> reprojImages;
    cv::Mat blendImage;
    LogoFilter logoFilter;
    avp::AudioVideoWriter3 writer;
    bool endFlag;
    std::atomic<int> finishPercent;
    int validFrameCount;
    std::unique_ptr<std::thread> thread;

    std::string syncErrorMessage;
    std::mutex mtxAsyncErrorMessage;
    std::string asyncErrorMessage;
    int hasAsyncError;
    void setAsyncErrorMessage(const std::string& message);
    void clearAsyncErrorMessage();

    bool initSuccess;
    bool finish;
};

CPUPanoramaLocalDiskTask::Impl::Impl()
{
    clear();
}

CPUPanoramaLocalDiskTask::Impl::~Impl()
{
    clear();
}

bool CPUPanoramaLocalDiskTask::Impl::init(const std::vector<std::string>& srcVideoFiles, const std::vector<int> offsets,
    int tryAudioIndex, const std::string& cameraParamFile, const std::string& customMaskFile, const std::string& dstVideoFile, int dstWidth, int dstHeight,
    int dstVideoBitRate, const std::string& dstVideoEncoder, const std::string& dstVideoPreset,
    int dstVideoMaxFrameCount)
{
    clear();

    if (srcVideoFiles.empty() || (srcVideoFiles.size() != offsets.size()))
    {
        ptlprintf("Error in %s, size of srcVideoFiles and size of offsets empty or unmatch.\n", __FUNCTION__);
        syncErrorMessage = "参数校验失败。";
        return false;
    }

    numVideos = srcVideoFiles.size();

    std::vector<PhotoParam> params;
    if (!loadPhotoParams(cameraParamFile, params))
    {
        ptlprintf("Error in %s, failed to load params\n", __FUNCTION__);
        syncErrorMessage = "初始化拼接失败。";
        return false;
    }
    if (params.size() != numVideos)
    {
        ptlprintf("Error in %s, params.size() != numVideos\n", __FUNCTION__);
        syncErrorMessage = "初始化拼接失败。";
        return false;
    }

    dstSize.width = dstWidth;
    dstSize.height = dstHeight;

    bool ok = false;
    ok = prepareSrcVideos(srcVideoFiles, avp::PixelTypeBGR24, offsets, tryAudioIndex, readers, audioIndex, srcSize, validFrameCount);
    if (!ok)
    {
        ptlprintf("Error in %s, could not open video file(s)\n", __FUNCTION__);
        syncErrorMessage = "打开视频失败。";
        return false;
    }

    if (dstVideoMaxFrameCount > 0 && validFrameCount > dstVideoMaxFrameCount)
        validFrameCount = dstVideoMaxFrameCount;

    getReprojectMapsAndMasks(params, srcSize, dstSize, dstSrcMaps, dstMasks);

    ok = blender.prepare(dstMasks, 16, 2);
    //ok = blender.prepare(dstMasks, 50);
    if (!ok)
    {
        ptlprintf("Error in %s, blender prepare failed\n", __FUNCTION__);
        syncErrorMessage = "初始化拼接失败。";
        return false;
    }

    useCustomMasks = 0;
    if (customMaskFile.size())
    {
        std::vector<std::vector<IntervaledContour> > contours;
        ok = loadIntervaledContours(customMaskFile, contours);
        if (!ok)
        {
            ptlprintf("Error in %s, load custom masks failed\n", __FUNCTION__);
            syncErrorMessage = "初始化拼接失败。";
            return false;
        }
        if (contours.size() != numVideos)
        {
            ptlprintf("Error in %s, loaded contours.size() != numVideos\n", __FUNCTION__);
            syncErrorMessage = "初始化拼接失败。";
            return false;
        }
        if (!cvtContoursToMasks(contours, dstMasks, customMasks))
        {
            ptlprintf("Error in %s, convert contours to customMasks failed\n", __FUNCTION__);
            syncErrorMessage = "初始化拼接失败。";
            return false;
        }
        blender.getUniqueMasks(dstUniqueMasks);
        useCustomMasks = 1;
    }

    ok = logoFilter.init(dstSize.width, dstSize.height, CV_8UC3);
    if (!ok)
    {
        ptlprintf("Error in %s, init logo filter failed\n", __FUNCTION__);
        syncErrorMessage = "初始化拼接失败。";
        return false;
    }

    std::vector<avp::Option> options;
    options.push_back(std::make_pair("preset", dstVideoPreset));
    std::string format = dstVideoEncoder == "h264_qsv" ? "h264_qsv" : "h264";
    if (audioIndex >= 0 && audioIndex < numVideos)
    {
        ok = writer.open(dstVideoFile, "", true, 
            true, "aac", readers[audioIndex].getAudioSampleType(), readers[audioIndex].getAudioChannelLayout(), 
            readers[audioIndex].getAudioSampleRate(), 128000,
            true, format, avp::PixelTypeBGR24, dstSize.width, dstSize.height, readers[0].getVideoFrameRate(), dstVideoBitRate, options);
    }
    else
    {
        ok = writer.open(dstVideoFile, "", false, false, "", avp::SampleTypeUnknown, 0, 0, 0,
            true, format, avp::PixelTypeBGR24, dstSize.width, dstSize.height, readers[0].getVideoFrameRate(), dstVideoBitRate, options);
    }
    if (!ok)
    {
        ptlprintf("Error in %s, video writer open failed\n", __FUNCTION__);
        syncErrorMessage = "无法创建拼接视频。";
        return false;
    }

    finishPercent.store(0);

    initSuccess = true;
    finish = false;
    return true;
}

void CPUPanoramaLocalDiskTask::Impl::run()
{
    if (!initSuccess)
        return;

    if (finish)
        return;

    ptlprintf("Info in %s, write video begin\n", __FUNCTION__);

    int count = 0;
    int step = 1;
    if (validFrameCount > 0)
        step = validFrameCount / 100.0 + 0.5;
    if (step < 1)
        step = 1;
    ptlprintf("Info in %s, validFrameCount = %d, step = %d\n", __FUNCTION__, validFrameCount, step);

    try
    {
        std::vector<avp::AudioVideoFrame2> frames(numVideos);
        std::vector<cv::Mat> images(numVideos);
        bool ok = true;
        blendImage.create(dstSize, CV_8UC3);
        while (true)
        {
            ok = true;
            if (audioIndex >= 0 && audioIndex < numVideos)
            {
                if (!readers[audioIndex].read(frames[audioIndex]))
                    break;

                if (frames[audioIndex].mediaType == avp::AUDIO)
                {
                    ok = writer.write(frames[audioIndex]);
                    if (!ok)
                    {
                        ptlprintf("Error in %s, write audio frame fail\n", __FUNCTION__);
                        setAsyncErrorMessage("写入视频失败，任务终止。");
                        break;
                    }
                    continue;
                }
                else
                {
                    images[audioIndex] = cv::Mat(frames[audioIndex].height, frames[audioIndex].width, CV_8UC3,
                        frames[audioIndex].data[0], frames[audioIndex].steps[0]);
                }
            }
            for (int i = 0; i < numVideos; i++)
            {
                if (i == audioIndex)
                    continue;

                if (!readers[i].read(frames[i]))
                {
                    ok = false;
                    break;
                }

                images[i] = cv::Mat(frames[i].height, frames[i].width, CV_8UC3, frames[i].data[0], frames[i].steps[0]);
            }
            if (!ok || endFlag)
                break;

            reprojectParallelTo16S(images, reprojImages, dstSrcMaps);

            if (useCustomMasks)
            {
                bool custom = false;
                currMasks.resize(numVideos);
                for (int i = 0; i < numVideos; i++)
                {
                    if (customMasks[i].getMask(frames[i].timeStamp, currMasks[i]))
                        custom = true;
                    else
                        currMasks[i] = dstUniqueMasks[i];
                }

                if (custom)
                {
                    printf("custom masks\n");
                    blender.blend(reprojImages, currMasks, blendImage);
                }
                else
                    blender.blend(reprojImages, blendImage);
            }
            else
                blender.blend(reprojImages, blendImage);

            if (addLogo)
                ok = logoFilter.addLogo(blendImage);
            if (!ok)
            {
                ptlprintf("Error in %s, add logo fail\n", __FUNCTION__);
                setAsyncErrorMessage("写入视频失败，任务终止。");
                break;
            }
            unsigned char* data[4] = { blendImage.data, 0, 0, 0 };
            int steps[4] = { blendImage.step, 0, 0, 0 };
            avp::AudioVideoFrame2 frame(data, steps, avp::PixelTypeBGR24, blendImage.cols, blendImage.rows, frames[0].timeStamp);
            ok = writer.write(frame);
            if (!ok)
            {
                ptlprintf("Error in %s, write video frame fail\n", __FUNCTION__);
                setAsyncErrorMessage("写入视频失败，任务终止。");
                break;
            }

            count++;
            if (count % step == 0)
                finishPercent.store(double(count) / (validFrameCount > 0 ? validFrameCount : 100) * 100);

            if (count >= validFrameCount)
                break;
        }

        for (int i = 0; i < numVideos; i++)
            readers[i].close();
        writer.close();
    }
    catch (std::exception& e)
    {
        ptlprintf("Error in %s, exception caught: %s\n", __FUNCTION__, e.what());
        setAsyncErrorMessage("视频拼接发生错误，任务终止。");
    }

    finishPercent.store(100);

    ptlprintf("Info in %s, write video finish\n", __FUNCTION__);

    finish = true;
}

bool CPUPanoramaLocalDiskTask::Impl::start()
{
    if (!initSuccess)
        return false;

    if (finish)
        return false;

    thread.reset(new std::thread(&CPUPanoramaLocalDiskTask::Impl::run, this));
    return true;
}

void CPUPanoramaLocalDiskTask::Impl::waitForCompletion()
{
    if (thread && thread->joinable())
        thread->join();
    thread.reset(0);
}

int CPUPanoramaLocalDiskTask::Impl::getProgress() const
{
    return finishPercent.load();
}

void CPUPanoramaLocalDiskTask::Impl::cancel()
{
    endFlag = true;
}

void CPUPanoramaLocalDiskTask::Impl::getLastSyncErrorMessage(std::string& message) const
{
    message = syncErrorMessage;
}

bool CPUPanoramaLocalDiskTask::Impl::hasAsyncErrorMessage() const
{
    return hasAsyncError;
}

void CPUPanoramaLocalDiskTask::Impl::getLastAsyncErrorMessage(std::string& message)
{
    if (hasAsyncError)
    {
        std::lock_guard<std::mutex> lg(mtxAsyncErrorMessage);
        message = asyncErrorMessage;
        hasAsyncError = 0;
    }
    else
        message.clear();
}

void CPUPanoramaLocalDiskTask::Impl::clear()
{
    numVideos = 0;
    srcSize = cv::Size();
    dstSize = cv::Size();
    readers.clear();
    dstMasks.clear();
    dstUniqueMasks.clear();
    currMasks.clear();
    useCustomMasks = 0;
    customMasks.clear();
    dstSrcMaps.clear();
    reprojImages.clear();
    writer.close();
    endFlag = false;

    finishPercent.store(0);

    validFrameCount = 0;

    if (thread && thread->joinable())
        thread->join();
    thread.reset(0);

    syncErrorMessage.clear();
    clearAsyncErrorMessage();

    initSuccess = false;
    finish = true;
}

void CPUPanoramaLocalDiskTask::Impl::setAsyncErrorMessage(const std::string& message)
{
    std::lock_guard<std::mutex> lg(mtxAsyncErrorMessage);
    hasAsyncError = 1;
    asyncErrorMessage = message;
}

void CPUPanoramaLocalDiskTask::Impl::clearAsyncErrorMessage()
{
    std::lock_guard<std::mutex> lg(mtxAsyncErrorMessage);
    hasAsyncError = 0;
    asyncErrorMessage.clear();
}

CPUPanoramaLocalDiskTask::CPUPanoramaLocalDiskTask()
{
    ptrImpl.reset(new Impl);
}

CPUPanoramaLocalDiskTask::~CPUPanoramaLocalDiskTask()
{

}

bool CPUPanoramaLocalDiskTask::init(const std::vector<std::string>& srcVideoFiles, const std::vector<int> offsets, int audioIndex,
    const std::string& cameraParamFile, const std::string& customMaskFile, const std::string& dstVideoFile, int dstWidth, int dstHeight,
    int dstVideoBitRate, const std::string& dstVideoEncoder, const std::string& dstVideoPreset, 
    int dstVideoMaxFrameCount)
{
    return ptrImpl->init(srcVideoFiles, offsets, audioIndex, cameraParamFile, customMaskFile, dstVideoFile, dstWidth, dstHeight,
        dstVideoBitRate, dstVideoEncoder, dstVideoPreset, dstVideoMaxFrameCount);
}

bool CPUPanoramaLocalDiskTask::start()
{
    return ptrImpl->start();
}

void CPUPanoramaLocalDiskTask::waitForCompletion()
{
    ptrImpl->waitForCompletion();
}

int CPUPanoramaLocalDiskTask::getProgress() const
{
    return ptrImpl->getProgress();
}

void CPUPanoramaLocalDiskTask::cancel()
{
    ptrImpl->cancel();
}

void CPUPanoramaLocalDiskTask::getLastSyncErrorMessage(std::string& message) const
{
    ptrImpl->getLastSyncErrorMessage(message);
}

bool CPUPanoramaLocalDiskTask::hasAsyncErrorMessage() const
{
    return ptrImpl->hasAsyncErrorMessage();
}

void CPUPanoramaLocalDiskTask::getLastAsyncErrorMessage(std::string& message)
{
    return ptrImpl->getLastAsyncErrorMessage(message);
}

struct StampedPinnedMemoryVector
{
    std::vector<cv::cuda::HostMem> frames;
    std::vector<long long int> timeStamps;
};

typedef BoundedCompleteQueue<avp::AudioVideoFrame2> FrameBuffer;
typedef BoundedCompleteQueue<StampedPinnedMemoryVector> FrameVectorBuffer;
typedef BoundedCompleteQueue<MixedAudioVideoFrame> MixedFrameBuffer;

struct CudaPanoramaLocalDiskTask::Impl
{
    Impl();
    ~Impl();
    bool init(const std::vector<std::string>& srcVideoFiles, const std::vector<int> offsets, int audioIndex,
        const std::string& cameraParamFile, const std::string& customMaskFile, const std::string& dstVideoFile, int dstWidth, int dstHeight,
        int dstVideoBitRate, const std::string& dstVideoEncoder, const std::string& dstVideoPreset, 
        int dstVideoMaxFrameCount);
    bool start();
    void waitForCompletion();
    int getProgress() const;
    void cancel();

    void getLastSyncErrorMessage(std::string& message) const;
    bool hasAsyncErrorMessage() const;
    void getLastAsyncErrorMessage(std::string& message);

    void clear();

    int numVideos;
    int audioIndex;
    cv::Size srcSize, dstSize;
    std::vector<avp::AudioVideoReader3> readers;
    CudaPanoramaRender2 render;
    PinnedMemoryPool srcFramesMemoryPool;
    AudioVideoFramePool audioFramesMemoryPool;
    FrameVectorBuffer decodeFramesBuffer;
    CudaHostMemVideoFrameMemoryPool dstFramesMemoryPool;
    MixedFrameBuffer procFrameBuffer;
    cv::Mat blendImageCpu;
    CudaLogoFilter logoFilter;
    avp::AudioVideoWriter3 writer;
    int isLibX264;

    int decodeCount;
    int procCount;
    int encodeCount;
    std::atomic<int> finishPercent;
    int validFrameCount;

    void decode();
    void proc();
    void encode();
    std::unique_ptr<std::thread> decodeThread;
    std::unique_ptr<std::thread> procThread;
    std::unique_ptr<std::thread> encodeThread;

    std::string syncErrorMessage;
    std::mutex mtxAsyncErrorMessage;
    std::string asyncErrorMessage;
    int hasAsyncError;
    void setAsyncErrorMessage(const std::string& message);
    void clearAsyncErrorMessage();

    bool initSuccess;
    bool finish;
    bool isCanceled;
};

CudaPanoramaLocalDiskTask::Impl::Impl()
{
    clear();
}

CudaPanoramaLocalDiskTask::Impl::~Impl()
{
    clear();
}

bool CudaPanoramaLocalDiskTask::Impl::init(const std::vector<std::string>& srcVideoFiles, const std::vector<int> offsets,
    int tryAudioIndex, const std::string& cameraParamFile, const std::string& customMaskFile, const std::string& dstVideoFile, int dstWidth, int dstHeight,
    int dstVideoBitRate, const std::string& dstVideoEncoder, const std::string& dstVideoPreset, 
    int dstVideoMaxFrameCount)
{
    clear();

    if (srcVideoFiles.empty() || (srcVideoFiles.size() != offsets.size()))
    {
        ptlprintf("Error in %s, size of srcVideoFiles and size of offsets empty or unmatch.\n", __FUNCTION__);
        syncErrorMessage = "参数校验失败。";
        return false;
    }

    numVideos = srcVideoFiles.size();

    dstSize.width = dstWidth;
    dstSize.height = dstHeight;

    bool ok = false;
    ok = prepareSrcVideos(srcVideoFiles, avp::PixelTypeBGR32, offsets, tryAudioIndex, readers, audioIndex, srcSize, validFrameCount);
    if (!ok)
    {
        ptlprintf("Error in %s, could not open video file(s)\n", __FUNCTION__);
        syncErrorMessage = "打开视频失败。";
        return false;
    }

    if (dstVideoMaxFrameCount > 0 && validFrameCount > dstVideoMaxFrameCount)
        validFrameCount = dstVideoMaxFrameCount;

    ok = srcFramesMemoryPool.init(readers[0].getVideoHeight(), readers[0].getVideoWidth(), CV_8UC4);
    if (!ok)
    {
        ptlprintf("Error in %s, could not init memory pool\n", __FUNCTION__);
        syncErrorMessage = "初始化拼接失败。";
        return false;
    }

    if (audioIndex >= 0 && audioIndex < numVideos)
    {
        ok = audioFramesMemoryPool.initAsAudioFramePool(readers[audioIndex].getAudioSampleType(),
            readers[audioIndex].getAudioNumChannels(), readers[audioIndex].getAudioChannelLayout(),
            readers[audioIndex].getAudioNumSamples());
        if (!ok)
        {
            ptlprintf("Error in %s, could not init memory pool\n", __FUNCTION__);
            syncErrorMessage = "初始化拼接失败。";
            return false;
        }
    }

    ok = render.prepare(cameraParamFile, customMaskFile, 1, srcSize, dstSize);
    if (!ok)
    {
        ptlprintf("Error in %s, render prepare failed\n", __FUNCTION__);
        syncErrorMessage = "初始化拼接失败。";
        return false;
    }

    if (render.getNumImages() != numVideos)
    {
        ptlprintf("Error in %s, num images in render not equal to num videos\n", __FUNCTION__);
        syncErrorMessage = "初始化拼接失败。";
        return false;
    }

    isLibX264 = dstVideoEncoder == "h264_qsv" ? 0 : 1;

    ok = dstFramesMemoryPool.init(isLibX264 ? avp::PixelTypeYUV420P : avp::PixelTypeNV12, dstSize.width, dstSize.height);
    if (!ok)
    {
        ptlprintf("Error in %s, could not init memory pool\n", __FUNCTION__);
        syncErrorMessage = "初始化拼接失败。";
        return false;
    }

    ok = logoFilter.init(dstSize.width, dstSize.height);
    if (!ok)
    {
        ptlprintf("Error in %s, init logo filter failed\n", __FUNCTION__);
        syncErrorMessage = "初始化拼接失败。";
        return false;
    }

    std::vector<avp::Option> options;
    options.push_back(std::make_pair("preset", dstVideoPreset));
    std::string format = dstVideoEncoder == "h264_qsv" ? "h264_qsv" : "h264";
    if (audioIndex >= 0 && audioIndex < numVideos)
    {
        ok = writer.open(dstVideoFile, "", true, true, "aac", readers[audioIndex].getAudioSampleType(),
            readers[audioIndex].getAudioChannelLayout(), readers[audioIndex].getAudioSampleRate(), 128000,
            true, format, isLibX264 ? avp::PixelTypeYUV420P : avp::PixelTypeNV12, 
            dstSize.width, dstSize.height, readers[0].getVideoFrameRate(), dstVideoBitRate, options);
    }
    else
    {
        ok = writer.open(dstVideoFile, "", false, false, "", avp::SampleTypeUnknown, 0, 0, 0,
            true, format, isLibX264 ? avp::PixelTypeYUV420P : avp::PixelTypeNV12,
            dstSize.width, dstSize.height, readers[0].getVideoFrameRate(), dstVideoBitRate, options);
    }
    if (!ok)
    {
        ptlprintf("Error in %s, video writer open failed\n", __FUNCTION__);
        syncErrorMessage = "无法创建拼接视频。";
        return false;
    }

    decodeFramesBuffer.setMaxSize(4);
    procFrameBuffer.setMaxSize(16);

    finishPercent.store(0);

    initSuccess = true;
    finish = false;
    return true;
}

bool CudaPanoramaLocalDiskTask::Impl::start()
{
    if (!initSuccess)
        return false;

    if (finish)
        return false;

    decodeThread.reset(new std::thread(&CudaPanoramaLocalDiskTask::Impl::decode, this));
    procThread.reset(new std::thread(&CudaPanoramaLocalDiskTask::Impl::proc, this));
    encodeThread.reset(new std::thread(&CudaPanoramaLocalDiskTask::Impl::encode, this));

    return true;
}

void CudaPanoramaLocalDiskTask::Impl::waitForCompletion()
{
    if (decodeThread && decodeThread->joinable())
        decodeThread->join();
    decodeThread.reset();
    if (procThread && procThread->joinable())
        procThread->join();
    procThread.reset(0);
    if (encodeThread && encodeThread->joinable())
        encodeThread->join();
    encodeThread.reset(0);

    if (!finish)
        ptlprintf("Info in %s, write video finish\n", __FUNCTION__);

    finish = true;
}

int CudaPanoramaLocalDiskTask::Impl::getProgress() const
{
    return finishPercent.load();
}

void CudaPanoramaLocalDiskTask::Impl::getLastSyncErrorMessage(std::string& message) const
{
    message = syncErrorMessage;
}

bool CudaPanoramaLocalDiskTask::Impl::hasAsyncErrorMessage() const
{
    return hasAsyncError;
}

void CudaPanoramaLocalDiskTask::Impl::getLastAsyncErrorMessage(std::string& message)
{
    if (hasAsyncError)
    {
        std::lock_guard<std::mutex> lg(mtxAsyncErrorMessage);
        message = asyncErrorMessage;
        hasAsyncError = 0;
    }
    else
        message.clear();
}

void CudaPanoramaLocalDiskTask::Impl::clear()
{
    numVideos = 0;
    srcSize = cv::Size();
    dstSize = cv::Size();
    readers.clear();
    writer.close();

    srcFramesMemoryPool.clear();
    audioFramesMemoryPool.clear();
    dstFramesMemoryPool.clear();

    decodeFramesBuffer.clear();
    procFrameBuffer.clear();

    decodeCount = 0;
    procCount = 0;
    encodeCount = 0;
    finishPercent.store(0);

    validFrameCount = 0;

    if (decodeThread && decodeThread->joinable())
        decodeThread->join();
    decodeThread.reset(0);
    if (procThread && procThread->joinable())
        procThread->join();
    procThread.reset(0);
    if (encodeThread && encodeThread->joinable())
        encodeThread->join();
    encodeThread.reset(0);

    syncErrorMessage.clear();
    clearAsyncErrorMessage();

    initSuccess = false;
    finish = true;
    isCanceled = false;
}

void CudaPanoramaLocalDiskTask::Impl::cancel()
{
    isCanceled = true;
}

void CudaPanoramaLocalDiskTask::Impl::decode()
{
    size_t id = std::this_thread::get_id().hash();
    ptlprintf("Thread %s [%8x] started\n", __FUNCTION__, id);

    decodeCount = 0;
    int mediaType;
    while (true)
    {
        StampedPinnedMemoryVector videoFrames;
        avp::AudioVideoFrame2 audioFrame;
        unsigned char* data[4] = { 0 };
        int steps[4] = { 0 };

        videoFrames.timeStamps.resize(numVideos);
        videoFrames.frames.resize(numVideos);

        if (audioIndex >= 0 && audioIndex < numVideos)
        {
            audioFramesMemoryPool.get(audioFrame);
            srcFramesMemoryPool.get(videoFrames.frames[audioIndex]);
            data[0] = videoFrames.frames[audioIndex].data;
            steps[0] = videoFrames.frames[audioIndex].step;
            avp::AudioVideoFrame2 videoFrame(data, steps, avp::PixelTypeBGR32, srcSize.width, srcSize.height, -1LL);
            if (!readers[audioIndex].readTo(audioFrame, videoFrame, mediaType))
                break;
            if (mediaType == avp::AUDIO)
            {
                procFrameBuffer.push(audioFrame);
                continue;
            }
            else if (mediaType == avp::VIDEO)
                videoFrames.timeStamps[audioIndex] = videoFrame.timeStamp;
            else
                break;
        }

        bool successRead = true;
        for (int i = 0; i < numVideos; i++)
        {
            if (i == audioIndex)
                continue;

            srcFramesMemoryPool.get(videoFrames.frames[i]);
            data[0] = videoFrames.frames[i].data;
            steps[0] = videoFrames.frames[i].step;
            avp::AudioVideoFrame2 videoFrame(data, steps, avp::PixelTypeBGR32, srcSize.width, srcSize.height, -1LL);
            if (!readers[i].readTo(audioFrame, videoFrame, mediaType))
            {
                successRead = false;
                break;
            }
            if (mediaType == avp::VIDEO)
                videoFrames.timeStamps[i] = videoFrame.timeStamp;
            else
            {
                successRead = false;
                break;
            }
        }
        if (!successRead || isCanceled)
            break;

        decodeFramesBuffer.push(videoFrames);
        decodeCount++;
        //ptlprintf("decode count = %d\n", decodeCount);

        if (decodeCount >= validFrameCount)
            break;
    }

    if (!isCanceled)
    {
        while (decodeFramesBuffer.size())
            std::this_thread::sleep_for(std::chrono::microseconds(25));
    }    
    decodeFramesBuffer.stop();

    for (int i = 0; i < numVideos; i++)
        readers[i].close();

    ptlprintf("In %s, total decode %d\n", __FUNCTION__, decodeCount);
    ptlprintf("Thread %s [%8x] end\n", __FUNCTION__, id);
}

void CudaPanoramaLocalDiskTask::Impl::proc()
{
    size_t id = std::this_thread::get_id().hash();
    ptlprintf("Thread %s [%8x] started\n", __FUNCTION__, id);

    procCount = 0;
    StampedPinnedMemoryVector srcFrames;
    std::vector<cv::Mat> images(numVideos);
    cv::cuda::GpuMat bgr32;
    MixedAudioVideoFrame videoFrame;
    cv::cuda::GpuMat y, u, v, uv;
    int index = audioIndex >= 0 ? audioIndex : 0;
    while (true)
    {
        if (!decodeFramesBuffer.pull(srcFrames))
            break;

        if (isCanceled)
            break;
        
        for (int i = 0; i < numVideos; i++)
            images[i] = srcFrames.frames[i].createMatHeader();        
        bool ok = render.render(images, srcFrames.timeStamps, bgr32);
        if (!ok)
        {
            ptlprintf("Error in %s, render failed\n", __FUNCTION__);
            setAsyncErrorMessage("视频拼接发生错误，任务终止。");
            isCanceled = true;
            break;
        }

        if (addLogo)
        {
            ok = logoFilter.addLogo(bgr32);
            if (!ok)
            {
                ptlprintf("Error in %s, render failed\n", __FUNCTION__);
                setAsyncErrorMessage("视频拼接发生错误，任务终止。");
                isCanceled = true;
                break;
            }
        }

        // IMPORTANT NOTICE!!!
        // I use cv::cuda::GpuMat::download to copy gpu memory to cpu memory.
        // If cpu memory is not page-locked, download will take quite a long time.
        // But in the following, cpu memory is page-locked, which costs just a little time.
        // NVIDIA's documentation does not mention that calling cudaMemcpy2D to copy
        // gpu memory to page-locked cpu memory costs less time than pageable memory.
        // Another implementation is to make the cpu memory as zero-copy,
        // then gpu color conversion writes result directly to cpu zero-copy memory.
        // If image size is too large, such writing costs a large amount of time.
        dstFramesMemoryPool.get(videoFrame);
        videoFrame.frame.timeStamp = srcFrames.timeStamps[index];
        if (isLibX264)
        {
            cvtBGR32ToYUV420P(bgr32, y, u, v);
            cv::Mat yy = videoFrame.planes[0].createMatHeader();
            cv::Mat uu = videoFrame.planes[1].createMatHeader();
            cv::Mat vv = videoFrame.planes[2].createMatHeader();
            y.download(yy);
            u.download(uu);
            v.download(vv);
        }
        else
        {
            cvtBGR32ToNV12(bgr32, y, uv);
            cv::Mat yy = videoFrame.planes[0].createMatHeader();
            cv::Mat uvuv = videoFrame.planes[1].createMatHeader();
            y.download(yy);
            uv.download(uvuv);
        }

        procFrameBuffer.push(videoFrame);
        procCount++;
        //ptlprintf("proc count = %d\n", procCount);
    }
    
    if (!isCanceled)
    {
        while (procFrameBuffer.size())
            std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    procFrameBuffer.stop();

    ptlprintf("In %s, total proc %d\n", __FUNCTION__, procCount);
    ptlprintf("Thread %s [%8x] end\n", __FUNCTION__, id);
}

void CudaPanoramaLocalDiskTask::Impl::encode()
{
    size_t id = std::this_thread::get_id().hash();
    ptlprintf("Thread %s [%8x] started\n", __FUNCTION__, id);

    encodeCount = 0;
    int step = 1;
    if (validFrameCount > 0)
        step = validFrameCount / 100.0 + 0.5;
    if (step < 1)
        step = 1;
    ptlprintf("In %s, validFrameCount = %d, step = %d\n", __FUNCTION__, validFrameCount, step);
    ztool::Timer timerEncode;
    encodeCount = 0;
    MixedAudioVideoFrame frame;
    while (true)
    {
        if (!procFrameBuffer.pull(frame))
            break;

        if (isCanceled)
            break;

        //timerEncode.start();
        bool ok = writer.write(frame.frame);
        //timerEncode.end();
        if (!ok)
        {
            ptlprintf("Error in %s, render failed\n", __FUNCTION__);
            setAsyncErrorMessage("视频拼接发生错误，任务终止。");
            isCanceled = true;
            break;
        }

        // Only when the frame is of type video can we increase encodeCount
        if (frame.frame.mediaType == avp::VIDEO)
            encodeCount++;
        //ptlprintf("frame %d finish, encode time = %f\n", encodeCount, timerEncode.elapse());

        if (encodeCount % step == 0)
            finishPercent.store(double(encodeCount) / (validFrameCount > 0 ? validFrameCount : 100) * 100);
    }

    writer.close();

    finishPercent.store(100);

    ptlprintf("In %s, total encode %d\n", __FUNCTION__, encodeCount);
    ptlprintf("Thread %s [%8x] end\n", __FUNCTION__, id);
}

void CudaPanoramaLocalDiskTask::Impl::setAsyncErrorMessage(const std::string& message)
{
    std::lock_guard<std::mutex> lg(mtxAsyncErrorMessage);
    hasAsyncError = 1;
    asyncErrorMessage = message;
}

void CudaPanoramaLocalDiskTask::Impl::clearAsyncErrorMessage()
{
    std::lock_guard<std::mutex> lg(mtxAsyncErrorMessage);
    hasAsyncError = 0;
    asyncErrorMessage.clear();
}

CudaPanoramaLocalDiskTask::CudaPanoramaLocalDiskTask()
{
    ptrImpl.reset(new Impl);
}

CudaPanoramaLocalDiskTask::~CudaPanoramaLocalDiskTask()
{

}

bool CudaPanoramaLocalDiskTask::init(const std::vector<std::string>& srcVideoFiles, const std::vector<int> offsets, int audioIndex,
    const std::string& cameraParamFile, const std::string& customMaskFile, const std::string& dstVideoFile, int dstWidth, int dstHeight,
    int dstVideoBitRate, const std::string& dstVideoEncoder, const std::string& dstVideoPreset, 
    int dstVideoMaxFrameCount)
{
    return ptrImpl->init(srcVideoFiles, offsets, audioIndex, cameraParamFile, customMaskFile, dstVideoFile, dstWidth, dstHeight,
        dstVideoBitRate, dstVideoEncoder, dstVideoPreset, dstVideoMaxFrameCount);
}

bool CudaPanoramaLocalDiskTask::start()
{
    return ptrImpl->start();
}

void CudaPanoramaLocalDiskTask::waitForCompletion()
{
    ptrImpl->waitForCompletion();
}

int CudaPanoramaLocalDiskTask::getProgress() const
{
    return ptrImpl->getProgress();
}

void CudaPanoramaLocalDiskTask::cancel()
{
    ptrImpl->cancel();
}

void CudaPanoramaLocalDiskTask::getLastSyncErrorMessage(std::string& message) const
{
    ptrImpl->getLastSyncErrorMessage(message);
}

bool CudaPanoramaLocalDiskTask::hasAsyncErrorMessage() const
{
    return ptrImpl->hasAsyncErrorMessage();
}

void CudaPanoramaLocalDiskTask::getLastAsyncErrorMessage(std::string& message)
{
    return ptrImpl->getLastAsyncErrorMessage(message);
}