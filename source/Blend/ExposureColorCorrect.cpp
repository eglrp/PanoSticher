#include "VisualManip.h"
#include "ZBlendAlgo.h"
#include "opencv2/imgproc.hpp"

bool ExposureColorCorrect::prepare(const std::vector<cv::Mat>& masks)
{
    prepareSuccess = 0;
    if (masks.empty())
        return false;

    numImages = masks.size();
    rows = masks[0].rows;
    cols = masks[0].cols;
    for (int i = 0; i < numImages; i++)
    {
        if (!masks[i].data || masks[i].type() != CV_8UC1 ||
            masks[i].rows != rows || masks[i].cols != cols)
            return false;
    }

    origMasks.resize(numImages);
    for (int i = 0; i < numImages; i++)
        origMasks[i] = masks[i].clone();

    prepareSuccess = 1;
    return true;
}

bool ExposureColorCorrect::correctExposure(const std::vector<cv::Mat>& images, std::vector<double>& exposures)
{
    if (!prepareSuccess)
        return false;

    for (int i = 0; i < numImages; i++)
    {
        if (!images[i].data || images[i].type() != CV_8UC3 ||
            images[i].rows != rows || images[i].cols != cols)
            return false;
    }

    std::vector<cv::Mat> grayImages(numImages);
    for (int i = 0; i < numImages; i++)
        cv::cvtColor(images[i], grayImages[i], CV_BGR2GRAY);

    std::vector<int> hist;
    bool histValid = true;
    for (int i = 0; i < numImages; i++)
    {
        calcHist(grayImages[i], origMasks[i], hist);
        int count = countNonZeroHistBins(hist);
        if (count < 3)
        {
            histValid = false;
            break;
        }
    }
    if (!histValid)
    {
        exposures.resize(numImages);
        for (int i = 0; i < numImages; i++)
            exposures[i] = 1;
        return true;
    }

    getTransformsGrayPairWiseMutualError(grayImages, origMasks, exposures);
    return true;
}

bool ExposureColorCorrect::correctExposureAndWhiteBalance(const std::vector<cv::Mat>& images,
    std::vector<double>& exposures, std::vector<double>& redRatios, std::vector<double>& blueRatios)
{
    if (!prepareSuccess)
        return false;

    for (int i = 0; i < numImages; i++)
    {
        if (!images[i].data || images[i].type() != CV_8UC3 ||
            images[i].rows != rows || images[i].cols != cols)
            return false;
    }

    std::vector<cv::Mat> grayImages(numImages);
    for (int i = 0; i < numImages; i++)
        cv::cvtColor(images[i], grayImages[i], CV_BGR2GRAY);

    std::vector<int> hist;
    bool histValid = true;
    for (int i = 0; i < numImages; i++)
    {
        calcHist(grayImages[i], origMasks[i], hist);
        int count = countNonZeroHistBins(hist);
        if (count < 3)
        {
            histValid = false;
            break;
        }
    }
    if (!histValid)
    {
        exposures.resize(numImages);
        redRatios.resize(numImages);
        blueRatios.resize(numImages);
        for (int i = 0; i < numImages; i++)
        {
            exposures[i] = 1;
            redRatios[i] = 1;
            blueRatios[i] = 1;
        }
        return true;
    }

    getTransformsGrayPairWiseMutualError(grayImages, origMasks, exposures);

    transImages.resize(numImages);
    std::vector<unsigned char> lut;
    for (int i = 0; i < numImages; i++)
    {
        getLUTBezierSmooth(lut, exposures[i]);
        adjust(images[i], transImages[i], lut);
    }

    getTintTransformsPairWiseMimicSiftPanoPaper(transImages, origMasks, redRatios, blueRatios);

    std::vector<double> diff(numImages);
    for (int i = 0; i < numImages; i++)
    {
        cv::Scalar mean = cv::mean(images[i], origMasks[i]);
        diff[i] = abs(1 - mean[0] / mean[1]) + abs(1 - mean[2] / mean[1]);
    }

    int anchorIndex = 0;
    int minDiff = diff[0];
    for (int i = 0; i < numImages; i++)
    {
        if (minDiff > diff[i])
        {
            minDiff = diff[i];
            anchorIndex = i;
        }
    }
    //printf("anchor = %d\n", anchorIndex);

    double rgScale = 1.0 / redRatios[anchorIndex], bgScale = 1.0 / blueRatios[anchorIndex];

    for (int i = 0; i < numImages; i++)
    {
        redRatios[i] *= rgScale;
        blueRatios[i] *= bgScale;
    }
    return true;
}

bool ExposureColorCorrect::correctColorExposure(const std::vector<cv::Mat>& images, std::vector<std::vector<double> >& exposures)
{
    if (!prepareSuccess)
        return false;

    for (int i = 0; i < numImages; i++)
    {
        if (!images[i].data || images[i].type() != CV_8UC3 ||
            images[i].rows != rows || images[i].cols != cols)
            return false;
    }

    std::vector<cv::Mat> grayImages(numImages);
    for (int i = 0; i < numImages; i++)
        cv::cvtColor(images[i], grayImages[i], CV_BGR2GRAY);

    std::vector<int> hist;
    bool histValid = true;
    for (int i = 0; i < numImages; i++)
    {
        calcHist(grayImages[i], origMasks[i], hist);
        int count = countNonZeroHistBins(hist);
        if (count < 3)
        {
            histValid = false;
            break;
        }
    }
    if (!histValid)
    {
        exposures.resize(numImages);
        for (int i = 0; i < numImages; i++)
            exposures[i].resize(3, 1.0);
        return true;
    }

    getTransformsBGRPairWiseMutualError(images, origMasks, exposures);
    return true;
}

void ExposureColorCorrect::clear()
{
    prepareSuccess = 0;
    numImages = 0;
    rows = 0;
    cols = 0;
    origMasks.clear();
    transImages.clear();
}

bool ExposureColorCorrect::getExposureLUTs(const std::vector<double>& exposures, std::vector<std::vector<unsigned char> >& luts)
{
    luts.clear();
    int numImages = exposures.size();
    if (numImages == 0)
        return false;

    luts.resize(numImages);
    for (int i = 0; i < numImages; i++)
        getLUTBezierSmooth(luts[i], exposures[i]);
    return true;
}

bool ExposureColorCorrect::getExposureAndWhiteBalanceLUTs(const std::vector<double>& exposures, const std::vector<double>& redRatios, 
    const std::vector<double>& blueRatios, std::vector<std::vector<std::vector<unsigned char> > >& luts)
{
    luts.clear();
    int numImages = exposures.size();
    if (numImages == 0)
        return false;
    if (numImages != redRatios.size() || numImages != blueRatios.size())
        return false;

    luts.resize(numImages);
    for (int i = 0; i < numImages; i++)
    {
        luts[i].resize(3);
        getLUTBezierSmooth(luts[i][0], exposures[i] * blueRatios[i]);
        getLUTBezierSmooth(luts[i][1], exposures[i]);
        getLUTBezierSmooth(luts[i][2], exposures[i] * redRatios[i]);
    }
    return true;
}

bool ExposureColorCorrect::getColorExposureLUTs(const std::vector<std::vector<double> >& exposures, 
    std::vector<std::vector<std::vector<unsigned char> > >& luts)
{
    luts.clear();
    int numImages = exposures.size();
    if (numImages == 0)
        return false;

    for (int i = 0; i < numImages; i++)
    {
        if (exposures[i].size() != 3)
            return false;
    }

    luts.resize(numImages);
    for (int i = 0; i < numImages; i++)
    {
        luts[i].resize(3);
        for (int j = 0; j < 3; j++)
            getLUTBezierSmooth(luts[i][j], exposures[i][j]);
    }
    return true;
}