//#include <TH/TH.h>
#include<torch/extension.h>
#include<c10/cuda/CUDAStream.h>
#include <math.h>
#include <omp.h>


void RODAlignForwardCpu(const float* bottom_data, const float spatial_scale, const int num_rois,
                     const int height, const int width, const int channels,
                     const int aligned_height, const int aligned_width, const float * bottom_rois,
                     float* top_data);

void RODAlignBackwardCpu(const float* top_diff, const float spatial_scale, const int num_rois,
                     const int height, const int width, const int channels,
                     const int aligned_height, const int aligned_width, const float * bottom_rois,
                     float* top_data);

int rod_align_forward(int aligned_height, int aligned_width, float spatial_scale,
                     torch::Tensor  features, torch::Tensor  rois, torch::Tensor  output)
{
    //Grab the input tensor
    float * data_flat = features.data<float>();//THFloatTensor_data(features);
    float * rois_flat = rois.data<float>(); //THFloatTensor_data(rois);

    float * output_flat = output.data<float>();//THFloatTensor_data(output);

    // Number of ROIs
    auto rois_size = rois.sizes();
    int num_rois = rois_size[0];//THFloatTensor_size(rois, 0);
    int size_rois = rois_size[1];//THFloatTensor_size(rois, 1);
    if (size_rois != 5)
    {
        return 0;
    }

    // data height
    auto features_size = features.sizes();
    int data_height = features_size[2];//THFloatTensor_size(features, 2);
    // data width
    int data_width = features_size[3];//THFloatTensor_size(features, 3);
    // Number of channels
    int num_channels = features_size[1];//THFloatTensor_size(features, 1);

    // do ROIAlignForward
    RODAlignForwardCpu(data_flat, spatial_scale, num_rois, data_height, data_width, num_channels,
            aligned_height, aligned_width, rois_flat, output_flat);

    return 1;
}

int rod_align_backward(int aligned_height, int aligned_width, float spatial_scale,
                       torch::Tensor * top_grad, torch::Tensor * rois, torch::Tensor * bottom_grad)
{
    //Grab the input tensor
    float * top_grad_flat = *top_grad.data<float>();//HFloatTensor_data(top_grad);
    float * rois_flat = *rois.data<float>(); //THFloatTensor_data(rois);

    float * bottom_grad_flat = *bottom_grad.data<float>();//THFloatTensor_data(bottom_grad);

    // Number of ROIs
    auto rois_size = *rois.sizes();
    int num_rois = rois_size[0];// THFloatTensor_size(rois, 0);
    int size_rois = rois_size[1];// THFloatTensor_size(rois, 1);
    if (size_rois != 5)
    {
        return 0;
    }

    // batch size
    // int batch_size = THFloatTensor_size(bottom_grad, 0);
    // data height
    auto bottom_size = *bottom_grad.sizes();
    int data_height = bottom_size[2]; //THFloatTensor_size(bottom_grad, 2);
    // data width
    int data_width = bottom_size[3];//THFloatTensor_size(bottom_grad, 3);
    // Number of channels
    int num_channels = bottom_size[1];// THFloatTensor_size(bottom_grad, 1);

    // do ROIAlignBackward
    RODAlignBackwardCpu(top_grad_flat, spatial_scale, num_rois, data_height,
            data_width, num_channels, aligned_height, aligned_width, rois_flat, bottom_grad_flat);

    return 1;
}

void RODAlignForwardCpu(const float* bottom_data, const float spatial_scale, const int num_rois,
                     const int height, const int width, const int channels,
                     const int aligned_height, const int aligned_width, const float * bottom_rois,
                     float* top_data)
{
    const int output_size = num_rois * aligned_height * aligned_width * channels;

    int idx = 0;
    float bin_size_h = (float)(height - 1.001) / (aligned_height - 1.);
    float bin_size_w = (float)(width - 1.001) / (aligned_width - 1.);
    for (idx = 0; idx < output_size; ++idx)
    {
        // (n, c, ph, pw) is an element in the aligned output
        int pw = idx % aligned_width;
        int ph = (idx / aligned_width) % aligned_height;
        int c = (idx / aligned_width / aligned_height) % channels;
        int n = idx / aligned_width / aligned_height / channels;

        float roi_batch_ind = bottom_rois[n * 5 + 0];
        float roi_start_w = bottom_rois[n * 5 + 1] * spatial_scale;
        float roi_start_h = bottom_rois[n * 5 + 2] * spatial_scale;
        float roi_end_w = bottom_rois[n * 5 + 3] * spatial_scale;
        float roi_end_h = bottom_rois[n * 5 + 4] * spatial_scale;
        

        float h = (float)(ph) * bin_size_h;
        float w = (float)(pw) * bin_size_w;

        int hstart = fminf(floor(h), height - 2);
        int wstart = fminf(floor(w), width - 2);

        int img_start = roi_batch_ind * channels * height * width;

        // bilinear interpolation
        if (h >= roi_start_h && h <= roi_end_h && w >= roi_start_w && w <= roi_end_w){
            top_data[idx] = 0.;
        } else {
            float h_ratio = h - (float)(hstart);
            float w_ratio = w - (float)(wstart);
            int upleft = img_start + (c * height + hstart) * width + wstart;
            int upright = upleft + 1;
            int downleft = upleft + width;
            int downright = downleft + 1;

            top_data[idx] = bottom_data[upleft] * (1. - h_ratio) * (1. - w_ratio)
                            + bottom_data[upright] * (1. - h_ratio) * w_ratio
                            + bottom_data[downleft] * h_ratio * (1. - w_ratio)
                            + bottom_data[downright] * h_ratio * w_ratio;
        }
    }
}

void RODAlignBackwardCpu(const float* top_diff, const float spatial_scale, const int num_rois,
                     const int height, const int width, const int channels,
                     const int aligned_height, const int aligned_width, const float * bottom_rois,
                     float* bottom_diff)
{
    const int output_size = num_rois * aligned_height * aligned_width * channels;

    int idx = 0;
    float bin_size_h = (float)(height - 1.001) / (aligned_height - 1.);
    float bin_size_w = (float)(width - 1.001) / (aligned_width - 1.);
    for (idx = 0; idx < output_size; ++idx)
    {
        // (n, c, ph, pw) is an element in the aligned output
        int pw = idx % aligned_width;
        int ph = (idx / aligned_width) % aligned_height;
        int c = (idx / aligned_width / aligned_height) % channels;
        int n = idx / aligned_width / aligned_height / channels;

        float roi_batch_ind = bottom_rois[n * 5 + 0];
        float roi_start_w = bottom_rois[n * 5 + 1] * spatial_scale;
        float roi_start_h = bottom_rois[n * 5 + 2] * spatial_scale;
        float roi_end_w = bottom_rois[n * 5 + 3] * spatial_scale;
        float roi_end_h = bottom_rois[n * 5 + 4] * spatial_scale;

        float h = (float)(ph) * bin_size_h;
        float w = (float)(pw) * bin_size_w;

        int hstart = fminf(floor(h), height - 2);
        int wstart = fminf(floor(w), width - 2);

        int img_start = roi_batch_ind * channels * height * width;

        // bilinear interpolation
        if (!(h >= roi_start_h && h <= roi_end_h && w >= roi_start_w && w <= roi_end_w)) {
            float h_ratio = h - (float)(hstart);
            float w_ratio = w - (float)(wstart);
            int upleft = img_start + (c * height + hstart) * width + wstart;
            int upright = upleft + 1;
            int downleft = upleft + width;
            int downright = downleft + 1;

            bottom_diff[upleft] += top_diff[idx] * (1. - h_ratio) * (1. - w_ratio);
            bottom_diff[upright] += top_diff[idx] * (1. - h_ratio) *  w_ratio;
            bottom_diff[downleft] += top_diff[idx] * h_ratio * (1. - w_ratio);
            bottom_diff[downright] += top_diff[idx] * h_ratio * w_ratio;
        }
    }
}

PYBIND11_MODULE(TORCH_EXTENSION_NAME, m) {
  m.def("forward", &rod_align_forward, "Rod align forward");
  m.def("backward", &rod_align_backward, "Rod align backward");
}
