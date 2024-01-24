#include <iostream>
#include <cmath>

#include <torch/torch.h>
#include <mve/image_io.h>

#include "project_gaussians.h"

#define PI 3.14159265358979323846

using namespace torch::indexing;


mve::ByteImage::Ptr tensorToImage(const torch::Tensor &t){
    int w = t.sizes()[1];
    int h = t.sizes()[0];
    int c = t.sizes()[2];

    mve::ByteImage::Ptr image = mve::ByteImage::create(w, h, c);
    uint8_t *dataPtr = static_cast<uint8_t *>((t * 255.0).toType(torch::kU8).data_ptr());
    std::copy(dataPtr, dataPtr + (w * h * c), image->get_data().data());

    return image;
}

torch::Tensor imageToTensor(const mve::ByteImage::Ptr &image){
    return torch::from_blob(image->get_data().data(), { image->height(), image->width(), image->channels() }, torch::kByte);
}



int main(int argc, char **argv){
    int width = 256,
        height = 256;
    int numPoints = 100000;
    int iterations = 1000;
    float learningRate = 0.01;

    torch::Device device = torch::kCPU;
    if (torch::cuda::is_available()) {
        std::cout << "Using CUDA" << std::endl;
        device = torch::kCUDA;
    }else{
        std::cout << "Using CPU" << std::endl;
    }

    // Test image
    // Top left red
    // Bottom right blue
    torch::Tensor gtImage = torch::ones({height, width, 3});
    gtImage.index_put_({Slice(None, height / 2), Slice(None, width / 2), Slice()}, torch::tensor({1.0, 0.0, 0.0}));
    gtImage.index_put_({Slice(height / 2, None), Slice(width / 2, None), Slice()}, torch::tensor({0.0, 0.0, 1.0}));

    // mve::ByteImage::Ptr image = tensorToImage(gtImage);
    // mve::image::save_file(image, "test.png");

    gtImage = gtImage.to(device);
    const int BLOCK_X = 16;
    const int BLOCK_Y = 16;
    double fovX = PI / 2.0; // horizontal field of view (90 deg)
    double focal = 0.5 * static_cast<double>(width) / std::tan(0.5 * fovX);

    TileBounds tileBounds = std::make_tuple((width + BLOCK_X - 1) / BLOCK_X,
                      (height + BLOCK_Y - 1) / BLOCK_Y,
                      1);
    
    torch::Tensor imgSize = torch::tensor({width, height, 1}, device);
    torch::Tensor block = torch::tensor({BLOCK_X, BLOCK_Y, 1}, device);
    
    // Init gaussians
    torch::cuda::manual_seed_all(0);

    // Random points, scales and colors
    torch::Tensor means = 2.0 * (torch::rand({numPoints, 3}, device) - 0.5); // Positions [-1, 1]
    torch::Tensor scales = torch::rand({numPoints, 3}, device);
    torch::Tensor rgbs = torch::rand({numPoints, 3}, device);
    
    // Random rotations (quaternions)
    // quats = ( sqrt(1-u) sin(2πv), sqrt(1-u) cos(2πv), sqrt(u) sin(2πw), sqrt(u) cos(2πw))
    torch::Tensor u = torch::rand({numPoints, 1}, device);
    torch::Tensor v = torch::rand({numPoints, 1}, device);
    torch::Tensor w = torch::rand({numPoints, 1}, device);
    torch::Tensor quats = torch::cat({
                torch::sqrt(1.0 - u) * torch::sin(2.0 * PI * v),
                torch::sqrt(1.0 - u) * torch::cos(2.0 * PI * v),
                torch::sqrt(u) * torch::sin(2.0 * PI * w),
                torch::sqrt(u) * torch::cos(2.0 * PI * w),
            }, -1);
    
    torch::Tensor opacities = torch::ones({numPoints, 1}, device);

    // View matrix (translation in Z by 8 units)
    torch::Tensor viewMat = torch::tensor({
            {1.0, 0.0, 0.0, 0.0},
            {0.0, 1.0, 0.0, 0.0},
            {0.0, 0.0, 1.0, 8.0},
            {0.0, 0.0, 0.0, 1.0}
        }, device);

    torch::Tensor background = torch::zeros(3, device);
    
    means.requires_grad_(true);
    scales.requires_grad_(true);
    quats.requires_grad_(true);
    rgbs.requires_grad_(true);
    opacities.requires_grad_(true);

    torch::optim::Adam optimizer({rgbs, means, scales, opacities, quats}, learningRate);
    torch::nn::MSELoss mseLoss;

    for (size_t i = 0; i < iterations; i++){
        ProjectGaussians::apply(means, scales, 1, 
                                quats, viewMat, viewMat,
                                focal, focal,
                                width / 2,
                                height / 2,
                                height,
                                width,
                                tileBounds);

        break;
    }
}