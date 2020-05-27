#include <ATen/ATen.h>

namespace at {
namespace native {

bool is_vulkan_available();

Tensor& vulkan_copy_(Tensor& self, const Tensor& src);

at::Tensor vulkan_convolution(
    const at::Tensor& input, // Vulkan
    const at::Tensor& weight, // CPU
    const at::Tensor& bias, // CPU
    IntArrayRef padding,
    IntArrayRef stride,
    IntArrayRef dilation,
    int64_t groups);

at::Tensor vulkan_convolution_prepack_weights(const at::Tensor& weight);

at::Tensor vulkan_convolution_prepacked(
    const at::Tensor& input, // Vulkan
    IntArrayRef weightSizes,
    const at::Tensor& weight_prepacked_vulkan, // Vulkan
    const c10::optional<at::Tensor>& bias, // Vulkan|CPU
    IntArrayRef padding,
    IntArrayRef stride,
    IntArrayRef dilation,
    int64_t groups);

// No-op batch norm at the moment to be able to profile models with batch_norm
std::tuple<at::Tensor, at::Tensor, at::Tensor, at::Tensor>
vulkan_batch_norm_noop(
    const at::Tensor& input,
    const at::Tensor& weight /* optional */,
    const at::Tensor& bias /* optional */,
    const at::Tensor& running_mean /* optional */,
    const at::Tensor& running_var /* optional */,
    double momentum,
    double eps);

} // namespace native
} // namespace at
