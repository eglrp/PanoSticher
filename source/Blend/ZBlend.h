﻿#pragma once

#include "opencv2/core.hpp"
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <memory>
#include <atomic>

struct BlendConfig
{
    enum SeamMode
    {
        SEAM_SKIP,
        SEAM_DISTANCE_TRANSFORM,
        SEAM_GRAPH_CUT
    };
    enum BlendMode
    {
        BLEND_PASTE,
        BLEND_LINEAR,
        BLEND_MULTIBAND
    };
    BlendConfig(int seamMode_ = SEAM_GRAPH_CUT, 
        int blendMode_ = BLEND_MULTIBAND, int radiusForLinear_ = 125, 
        int maxLevelsForMultiBand_ = 16, int minLengthForMultiBand_ = 2,
        int padForGraphCut_ = 8, int scaleForGraphCut_ = 8, 
        int refineForGraphCut_ = 1, double ratioForGraphCut_ = 0.75)
        : seamMode(seamMode_), blendMode(blendMode_), radiusForLinear(radiusForLinear_), 
        maxLevelsForMultiBand(maxLevelsForMultiBand_), minLengthForMultiBand(minLengthForMultiBand_),
        padForGraphCut(padForGraphCut_), scaleForGraphCut(scaleForGraphCut_), 
        refineForGraphCut(refineForGraphCut_), ratioForGraphCut(ratioForGraphCut_)
    {};
    void setSeamSkip()
    {
        seamMode = SEAM_SKIP;
    }
    void setSeamDistanceTransform()
    {
        seamMode = SEAM_DISTANCE_TRANSFORM;
    }
    void setSeamGraphCut(int pad = 8, int scale = 8, int refine = 1, double ratio = 0.75)
    {
        padForGraphCut = pad;
        scaleForGraphCut = scale;
        refineForGraphCut = refine;
        ratioForGraphCut = ratio;
    }
    void setBlendPaste()
    {
        blendMode = BLEND_PASTE;
    }
    void setBlendLinear(int radius = 125)
    {
        blendMode = BLEND_LINEAR;
        radiusForLinear = radius;
    }
    void setBlendMultiBand(int maxLevels = 16, int minLength = 2)
    {
        blendMode = BLEND_MULTIBAND;
        maxLevelsForMultiBand = maxLevels;
        minLengthForMultiBand = minLength;
    }
    int seamMode;
    int blendMode;
    int radiusForLinear;
    int maxLevelsForMultiBand;
    int minLengthForMultiBand;
    int padForGraphCut;
    int scaleForGraphCut;
    int refineForGraphCut;
    double ratioForGraphCut;    
};

void serialBlend(const BlendConfig& config, const cv::Mat& image, const cv::Mat& mask, 
    cv::Mat& blendImage, cv::Mat& blendMask);

void parallelBlend(const BlendConfig& config, const std::vector<cv::Mat>& images,
    const std::vector<cv::Mat>& masks, cv::Mat& blendImage);

class MultibandBlendBase
{
public:
    virtual ~MultibandBlendBase() {};
    virtual bool prepare(const std::vector<cv::Mat>& masks, int maxLevels, int minLength) { return false; }
    virtual void blend(const std::vector<cv::Mat>& images, cv::Mat& blendImage) {};
    virtual void blend(const std::vector<cv::Mat>& images, const std::vector<cv::Mat>& masks, cv::Mat& blendImage) {};
    virtual void blendAndCompensate(const std::vector<cv::Mat>& images, const std::vector<cv::Mat>& masks, cv::Mat& blendImage) {};
};

class TilingMultibandBlend : public MultibandBlendBase
{
public:
    TilingMultibandBlend() : numImages(0), rows(0), cols(0), numLevels(0), success(false) {}
    ~TilingMultibandBlend() {};
    bool prepare(const std::vector<cv::Mat>& masks, int maxLevels, int minLength);
    void tile(const cv::Mat& image, const cv::Mat& mask, int index);
    void composite(cv::Mat& blendImage);
    void blend(const std::vector<cv::Mat>& images, cv::Mat& blendImage);
    void blend(const std::vector<cv::Mat>& images, const std::vector<cv::Mat>& masks, cv::Mat& blendImage);
    void blendAndCompensate(const std::vector<cv::Mat>& images, const std::vector<cv::Mat>& masks, cv::Mat& blendImage);

private:
    std::vector<cv::Mat> uniqueMasks;
    std::vector<cv::Mat> resultPyr;
    std::vector<cv::Mat> resultWeightPyr;
    int numImages;
    int rows, cols;
    int numLevels;
    bool success;
};

class TilingMultibandBlendFast : public MultibandBlendBase
{
public:
    TilingMultibandBlendFast() : numImages(0), rows(0), cols(0), numLevels(0), success(false) {}
    ~TilingMultibandBlendFast() {}
    bool prepare(const std::vector<cv::Mat>& masks, int maxLevels, int minLength);
    void blend(const std::vector<cv::Mat>& images, cv::Mat& blendImage);
    void blend(const std::vector<cv::Mat>& images, const std::vector<cv::Mat>& masks, cv::Mat& blendImage);
    void blendAndCompensate(const std::vector<cv::Mat>& images, const std::vector<cv::Mat>& masks, cv::Mat& blendImage);
    void getUniqueMasks(std::vector<cv::Mat>& masks) const;

private:
    std::vector<cv::Mat> uniqueMasks;
    std::vector<cv::Mat> resultPyr, resultUpPyr, resultWeightPyr;
    std::vector<cv::Mat> imagePyr, image32SPyr, imageUpPyr;
    std::vector<std::vector<cv::Mat> > alphaPyrs, weightPyrs;    
    cv::Mat maskNot;
    int numImages;
    int rows, cols;
    int numLevels;
    bool fullMask;
    bool success;

    std::vector<cv::Mat> customResultWeightPyr;
    std::vector<std::vector<cv::Mat> > customWeightPyrs;
    cv::Mat customAux, customMaskNot;

    cv::Mat remain, matchArea;
    std::vector<cv::Mat> adjustMasks, tempAlphaPyr, adjustAlphaPyr;
};

// DEPRECATED
// Just only a little faster than TilingMultibandBlendFast at the expense of more memory consumption
class TilingMultibandBlendFastParallel : public MultibandBlendBase
{
public:
    TilingMultibandBlendFastParallel() : numImages(0), rows(0), cols(0), numLevels(0), success(false), threadEnd(true) {}
    ~TilingMultibandBlendFastParallel();
    bool prepare(const std::vector<cv::Mat>& masks, int maxLevels, int minLength);
    void blend(const std::vector<cv::Mat>& images, cv::Mat& blendImage);
    void blend(const std::vector<cv::Mat>& images, const std::vector<cv::Mat>& masks, cv::Mat& blendImage);
    void getUniqueMasks(std::vector<cv::Mat>& masks) const;

private:
    std::vector<cv::Mat> uniqueMasks;
    std::vector<cv::Mat> resultPyr, resultUpPyr, resultWeightPyr;
    std::vector<std::vector<cv::Mat> > imagePyrs, image32SPyrs, imageUpPyrs;
    std::vector<std::vector<cv::Mat> > alphaPyrs, weightPyrs;
    std::vector<std::vector<unsigned char> > rowBuffers, tabBuffers;
    std::vector<unsigned char> restoreRowBuffer, restoreTabBuffer;
    cv::Mat maskNot;
    int numImages;
    int rows, cols;
    int numLevels;
    bool fullMask;
    bool success;

    void init();
    void endThreads();
    std::vector<cv::Mat> imageHeaders;
    std::vector<std::unique_ptr<std::thread> > threads;
    std::mutex mtxBuildPyr, mtxAccum;
    std::condition_variable cvBuildPyr, cvAccum;
    std::atomic<int> buildCount;
    bool threadEnd;
    void buildPyramid(int index);

    std::vector<cv::Mat> customMasks, customAuxes;
    std::vector<cv::Mat> customResultWeightPyr;
    std::vector<std::vector<cv::Mat> > customWeightPyrs;
    cv::Mat customMaskNot;
};

class TilingLinearBlend
{
public:
    TilingLinearBlend() : numImages(0), rows(0), cols(0), success(false) {}
    bool prepare(const std::vector<cv::Mat>& masks, int radius);
    void blend(const std::vector<cv::Mat>& images, cv::Mat& blendImage) const;
private:
    std::vector<cv::Mat> weights;
    int numImages;
    int rows, cols;
    bool success;
};
