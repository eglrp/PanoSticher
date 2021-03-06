#include "RunTimeObjects.h"
#include "IntelOpenCLInterface.h"

void ioclReproject(const iocl::UMat& src, iocl::UMat& dst, const iocl::UMat& xmap, const iocl::UMat& ymap,
    OpenCLBasic& ocl, OpenCLProgramOneKernel& executable)
{
    CV_Assert(src.data && src.type == CV_8UC4 && xmap.size() == ymap.size() &&
        xmap.data && xmap.type == CV_32FC1 && ymap.data && ymap.type == CV_32FC1 &&
        ocl.context && ocl.queue && executable.kernel);

    dst.create(xmap.rows, ymap.cols, CV_8UC4);

    cl_int err = CL_SUCCESS;

    err = clSetKernelArg(executable.kernel, 0, sizeof(cl_mem), (void *)&src.mem);
    SAMPLE_CHECK_ERRORS(err);
    err = clSetKernelArg(executable.kernel, 1, sizeof(int), &src.cols);
    SAMPLE_CHECK_ERRORS(err);
    err = clSetKernelArg(executable.kernel, 2, sizeof(int), &src.rows);
    SAMPLE_CHECK_ERRORS(err);
    err = clSetKernelArg(executable.kernel, 3, sizeof(int), (void *)&src.step);
    SAMPLE_CHECK_ERRORS(err);
    err = clSetKernelArg(executable.kernel, 4, sizeof(cl_mem), (void *)&dst.mem);
    SAMPLE_CHECK_ERRORS(err);
    err = clSetKernelArg(executable.kernel, 5, sizeof(int), &dst.cols);
    SAMPLE_CHECK_ERRORS(err);
    err = clSetKernelArg(executable.kernel, 6, sizeof(int), &dst.rows);
    SAMPLE_CHECK_ERRORS(err);
    err = clSetKernelArg(executable.kernel, 7, sizeof(int), &dst.step);
    SAMPLE_CHECK_ERRORS(err);
    err = clSetKernelArg(executable.kernel, 8, sizeof(cl_mem), (void *)&xmap.mem);
    SAMPLE_CHECK_ERRORS(err);
    err = clSetKernelArg(executable.kernel, 9, sizeof(int), &xmap.step);
    SAMPLE_CHECK_ERRORS(err);
    err = clSetKernelArg(executable.kernel, 10, sizeof(cl_mem), (void *)&ymap.mem);
    SAMPLE_CHECK_ERRORS(err);
    err = clSetKernelArg(executable.kernel, 11, sizeof(int), &ymap.step);
    SAMPLE_CHECK_ERRORS(err);

    size_t globalWorkSize[2] = { (size_t)round_up_aligned(dst.cols, 16), (size_t)round_up_aligned(dst.rows, 16) };
    size_t localWorkSize[2] = { 16, 16 };
    size_t offset[2] = { 0, 0 };

    err = clEnqueueNDRangeKernel(ocl.queue, executable.kernel, 2, offset, globalWorkSize, localWorkSize, 0, NULL, NULL);
    SAMPLE_CHECK_ERRORS(err);
    err = clFinish(ocl.queue);
    SAMPLE_CHECK_ERRORS(err);
}

void ioclReprojectAccumulateWeightedTo32F(const iocl::UMat& src, iocl::UMat& dst, const iocl::UMat& xmap, const iocl::UMat& ymap,
    const iocl::UMat& weight, OpenCLBasic& ocl, OpenCLProgramOneKernel& executable)
{
    CV_Assert(src.data && src.type == CV_8UC4 && xmap.size() == ymap.size() &&
        xmap.data && xmap.type == CV_32FC1 && ymap.data && ymap.type == CV_32FC1 &&
        dst.data && dst.type == CV_32FC4 && dst.size() == xmap.size() &&
        xmap.size() == weight.size() && weight.type == CV_32FC1 &&
        ocl.context && ocl.queue && executable.kernel);

    cl_int err = CL_SUCCESS;

    err = clSetKernelArg(executable.kernel, 0, sizeof(cl_mem), (void *)&src.mem);
    SAMPLE_CHECK_ERRORS(err);
    err = clSetKernelArg(executable.kernel, 1, sizeof(int), &src.cols);
    SAMPLE_CHECK_ERRORS(err);
    err = clSetKernelArg(executable.kernel, 2, sizeof(int), &src.rows);
    SAMPLE_CHECK_ERRORS(err);
    err = clSetKernelArg(executable.kernel, 3, sizeof(int), &src.step);
    SAMPLE_CHECK_ERRORS(err);
    err = clSetKernelArg(executable.kernel, 4, sizeof(cl_mem), (void *)&dst.mem);
    SAMPLE_CHECK_ERRORS(err);
    err = clSetKernelArg(executable.kernel, 5, sizeof(int), &dst.cols);
    SAMPLE_CHECK_ERRORS(err);
    err = clSetKernelArg(executable.kernel, 6, sizeof(int), &dst.rows);
    SAMPLE_CHECK_ERRORS(err);
    err = clSetKernelArg(executable.kernel, 7, sizeof(int), &dst.step);
    SAMPLE_CHECK_ERRORS(err);
    err = clSetKernelArg(executable.kernel, 8, sizeof(cl_mem), (void *)&xmap.mem);
    SAMPLE_CHECK_ERRORS(err);
    err = clSetKernelArg(executable.kernel, 9, sizeof(int), &xmap.step);
    SAMPLE_CHECK_ERRORS(err);
    err = clSetKernelArg(executable.kernel, 10, sizeof(cl_mem), (void *)&ymap.mem);
    SAMPLE_CHECK_ERRORS(err);
    err = clSetKernelArg(executable.kernel, 11, sizeof(int), &ymap.step);
    SAMPLE_CHECK_ERRORS(err);
    err = clSetKernelArg(executable.kernel, 12, sizeof(cl_mem), (void *)&weight.mem);
    SAMPLE_CHECK_ERRORS(err);
    err = clSetKernelArg(executable.kernel, 13, sizeof(int), &weight.step);
    SAMPLE_CHECK_ERRORS(err);

    size_t globalWorkSize[2] = { (size_t)round_up_aligned(dst.cols, 16), (size_t)round_up_aligned(dst.rows, 16) };
    size_t localWorkSize[2] = { 16, 16 };
    size_t offset[2] = { 0, 0 };

    err = clEnqueueNDRangeKernel(ocl.queue, executable.kernel, 2, offset, globalWorkSize, localWorkSize, 0, NULL, NULL);
    SAMPLE_CHECK_ERRORS(err);
    err = clFinish(ocl.queue);
    SAMPLE_CHECK_ERRORS(err);
}

void ioclReproject(const iocl::UMat& src, iocl::UMat& dst, const iocl::UMat& xmap, const iocl::UMat& ymap)
{
    CV_Assert(src.data && src.type == CV_8UC4 && xmap.size() == ymap.size() &&
        xmap.data && xmap.type == CV_32FC1 && ymap.data && ymap.type == CV_32FC1 &&
        iocl::ocl && iocl::ocl->context && iocl::ocl->queue && 
        iocl::reproject && iocl::reproject->kernel);

    dst.create(xmap.rows, ymap.cols, CV_8UC4);

    cl_int err = CL_SUCCESS;

    cl_kernel kernel = iocl::reproject->kernel;
    cl_command_queue queue = iocl::ocl->queue;

    err = clSetKernelArg(kernel, 0, sizeof(cl_mem), (void *)&src.mem);
    SAMPLE_CHECK_ERRORS(err);
    err = clSetKernelArg(kernel, 1, sizeof(int), &src.cols);
    SAMPLE_CHECK_ERRORS(err);
    err = clSetKernelArg(kernel, 2, sizeof(int), &src.rows);
    SAMPLE_CHECK_ERRORS(err);
    err = clSetKernelArg(kernel, 3, sizeof(int), (void *)&src.step);
    SAMPLE_CHECK_ERRORS(err);
    err = clSetKernelArg(kernel, 4, sizeof(cl_mem), (void *)&dst.mem);
    SAMPLE_CHECK_ERRORS(err);
    err = clSetKernelArg(kernel, 5, sizeof(int), &dst.cols);
    SAMPLE_CHECK_ERRORS(err);
    err = clSetKernelArg(kernel, 6, sizeof(int), &dst.rows);
    SAMPLE_CHECK_ERRORS(err);
    err = clSetKernelArg(kernel, 7, sizeof(int), &dst.step);
    SAMPLE_CHECK_ERRORS(err);
    err = clSetKernelArg(kernel, 8, sizeof(cl_mem), (void *)&xmap.mem);
    SAMPLE_CHECK_ERRORS(err);
    err = clSetKernelArg(kernel, 9, sizeof(int), &xmap.step);
    SAMPLE_CHECK_ERRORS(err);
    err = clSetKernelArg(kernel, 10, sizeof(cl_mem), (void *)&ymap.mem);
    SAMPLE_CHECK_ERRORS(err);
    err = clSetKernelArg(kernel, 11, sizeof(int), &ymap.step);
    SAMPLE_CHECK_ERRORS(err);

    size_t globalWorkSize[2] = { (size_t)round_up_aligned(src.cols, 16), (size_t)round_up_aligned(dst.rows, 16) };
    size_t localWorkSize[2] = { 16, 16 };
    size_t offset[2] = { 0, 0 };

    err = clEnqueueNDRangeKernel(queue, kernel, 2, offset, globalWorkSize, localWorkSize, 0, NULL, NULL);
    SAMPLE_CHECK_ERRORS(err);
    err = clFinish(queue);
    SAMPLE_CHECK_ERRORS(err);
}

void ioclReprojectTo16S(const iocl::UMat& src, iocl::UMat& dst, const iocl::UMat& xmap, const iocl::UMat& ymap)
{
    CV_Assert(src.data && src.type == CV_8UC4 && xmap.size() == ymap.size() &&
        xmap.data && xmap.type == CV_32FC1 && ymap.data && ymap.type == CV_32FC1 &&
        iocl::ocl && iocl::ocl->context && iocl::ocl->queue &&
        iocl::reproject && iocl::reproject->kernel);

    dst.create(xmap.rows, ymap.cols, CV_16SC4);

    cl_int err = CL_SUCCESS;

    cl_kernel kernel = iocl::reprojectTo16S->kernel;
    cl_command_queue queue = iocl::ocl->queue;

    err = clSetKernelArg(kernel, 0, sizeof(cl_mem), (void *)&src.mem);
    SAMPLE_CHECK_ERRORS(err);
    err = clSetKernelArg(kernel, 1, sizeof(int), &src.cols);
    SAMPLE_CHECK_ERRORS(err);
    err = clSetKernelArg(kernel, 2, sizeof(int), &src.rows);
    SAMPLE_CHECK_ERRORS(err);
    err = clSetKernelArg(kernel, 3, sizeof(int), (void *)&src.step);
    SAMPLE_CHECK_ERRORS(err);
    err = clSetKernelArg(kernel, 4, sizeof(cl_mem), (void *)&dst.mem);
    SAMPLE_CHECK_ERRORS(err);
    err = clSetKernelArg(kernel, 5, sizeof(int), &dst.cols);
    SAMPLE_CHECK_ERRORS(err);
    err = clSetKernelArg(kernel, 6, sizeof(int), &dst.rows);
    SAMPLE_CHECK_ERRORS(err);
    err = clSetKernelArg(kernel, 7, sizeof(int), &dst.step);
    SAMPLE_CHECK_ERRORS(err);
    err = clSetKernelArg(kernel, 8, sizeof(cl_mem), (void *)&xmap.mem);
    SAMPLE_CHECK_ERRORS(err);
    err = clSetKernelArg(kernel, 9, sizeof(int), &xmap.step);
    SAMPLE_CHECK_ERRORS(err);
    err = clSetKernelArg(kernel, 10, sizeof(cl_mem), (void *)&ymap.mem);
    SAMPLE_CHECK_ERRORS(err);
    err = clSetKernelArg(kernel, 11, sizeof(int), &ymap.step);
    SAMPLE_CHECK_ERRORS(err);

    size_t globalWorkSize[2] = { (size_t)round_up_aligned(dst.cols, 16), (size_t)round_up_aligned(dst.rows, 16) };
    size_t localWorkSize[2] = { 16, 16 };
    size_t offset[2] = { 0, 0 };

    err = clEnqueueNDRangeKernel(queue, kernel, 2, offset, globalWorkSize, localWorkSize, 0, NULL, NULL);
    SAMPLE_CHECK_ERRORS(err);
    err = clFinish(queue);
    SAMPLE_CHECK_ERRORS(err);
}

void ioclReprojectWeightedAccumulateTo32F(const iocl::UMat& src, iocl::UMat& dst,
    const iocl::UMat& xmap, const iocl::UMat& ymap, const iocl::UMat& weight)
{
    CV_Assert(src.data && src.type == CV_8UC4 && xmap.size() == ymap.size() &&
        xmap.data && xmap.type == CV_32FC1 && ymap.data && ymap.type == CV_32FC1 &&
        dst.data && dst.type == CV_32FC4 && dst.size() == xmap.size() &&
        xmap.size() == weight.size() && weight.type == CV_32FC1 &&
        iocl::ocl && iocl::ocl->context && iocl::ocl->queue &&
        iocl::reprojectWeightedAccumulateTo32F && iocl::reprojectWeightedAccumulateTo32F->kernel);

    cl_int err = CL_SUCCESS;

    cl_kernel kernel = iocl::reprojectWeightedAccumulateTo32F->kernel;
    cl_command_queue queue = iocl::ocl->queue;

    err = clSetKernelArg(kernel, 0, sizeof(cl_mem), (void *)&src.mem);
    SAMPLE_CHECK_ERRORS(err);
    err = clSetKernelArg(kernel, 1, sizeof(int), &src.cols);
    SAMPLE_CHECK_ERRORS(err);
    err = clSetKernelArg(kernel, 2, sizeof(int), &src.rows);
    SAMPLE_CHECK_ERRORS(err);
    err = clSetKernelArg(kernel, 3, sizeof(int), &src.step);
    SAMPLE_CHECK_ERRORS(err);
    err = clSetKernelArg(kernel, 4, sizeof(cl_mem), (void *)&dst.mem);
    SAMPLE_CHECK_ERRORS(err);
    err = clSetKernelArg(kernel, 5, sizeof(int), &dst.cols);
    SAMPLE_CHECK_ERRORS(err);
    err = clSetKernelArg(kernel, 6, sizeof(int), &dst.rows);
    SAMPLE_CHECK_ERRORS(err);
    err = clSetKernelArg(kernel, 7, sizeof(int), &dst.step);
    SAMPLE_CHECK_ERRORS(err);
    err = clSetKernelArg(kernel, 8, sizeof(cl_mem), (void *)&xmap.mem);
    SAMPLE_CHECK_ERRORS(err);
    err = clSetKernelArg(kernel, 9, sizeof(int), &xmap.step);
    SAMPLE_CHECK_ERRORS(err);
    err = clSetKernelArg(kernel, 10, sizeof(cl_mem), (void *)&ymap.mem);
    SAMPLE_CHECK_ERRORS(err);
    err = clSetKernelArg(kernel, 11, sizeof(int), &ymap.step);
    SAMPLE_CHECK_ERRORS(err);
    err = clSetKernelArg(kernel, 12, sizeof(cl_mem), (void *)&weight.mem);
    SAMPLE_CHECK_ERRORS(err);
    err = clSetKernelArg(kernel, 13, sizeof(int), &weight.step);
    SAMPLE_CHECK_ERRORS(err);

    size_t globalWorkSize[2] = { (size_t)round_up_aligned(dst.cols, 16), (size_t)round_up_aligned(dst.rows, 16) };
    size_t localWorkSize[2] = { 16, 16 };
    size_t offset[2] = { 0, 0 };

    err = clEnqueueNDRangeKernel(queue, kernel, 2, offset, globalWorkSize, localWorkSize, 0, NULL, NULL);
    SAMPLE_CHECK_ERRORS(err);
    err = clFinish(queue);
    SAMPLE_CHECK_ERRORS(err);
}
