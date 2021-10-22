#define TORCH_ASSERT_NO_OPERATORS
#include <caffe2/core/export_caffe2_op_to_c10.h>
#undef TORCH_ASSERT_NO_OPERATORS

#if defined(EXPOSE_C2_OPS) ||                               \
  !defined(CAFFE2_IS_XPLAT_BUILD) && !defined(C10_MOBILE)

#include <ATen/core/function_schema.h>
#include <ATen/core/dispatch/Dispatcher.h>
#include <torch/csrc/jit/frontend/function_schema_parser.h>
#include <torch/library.h>

namespace caffe2 {
namespace detail {

// This function is inline in the hope that compilers optimizing for speed will
// inline it into call_caffe2_op_from_c10, allowing call_op to be inlined and
// avoiding the function pointer indirection, while compilers optimizing for
// binary size will keep it a separate function instead of inlining it into
// a template and will reuse the binary code of this function between ops.
// We measured and confirmed that binary size off the instagram ios app is
// reduced when having _call_caffe2_op_from_c10 separate from the templated
// call_caffe2_op_from_c10.
void call_caffe2_op_from_c10(
    const OperatorHandle &opHandle,
    c10::Stack* stack,
    _CallCaffe2OpFunc* call_op) {
  // precondition: on the stack, there's one IValue for each argument of the
  // c10 schema. The last argument is an optional tensor list that
  // (if not ivalue::None) contains a preallocated output tensor for each
  // operator output.

  // As an invariant, we don't want any autograd gradients to be tracked in
  // Caffe2 operators.
  at::NoGradGuard guard;

  const auto &schema = opHandle.schema();
  AT_ASSERT(
      schema.arguments().size() != 0 &&
      schema.arguments().back().type()->isSubtypeOf(
          *OptionalType::create(ListType::ofTensors())));
  IValue preallocated_outputs = torch::jit::pop(*stack);

  const size_t num_outputs = schema.returns().size();
  const size_t num_inputs = schema.arguments().size() -
      1; // -1 because the last argument is the list of preallocated tensors

  c10::List<at::Tensor> outputs;
  if (preallocated_outputs.isNone()) {
    // either the schema doesn't support preallocated outputs or it does but
    // they haven't been passed in. Pass a list of uninitialized tensors to
    // the caffe2 operator as preallocated outputs.
    outputs.resize(num_outputs);
  } else {
    AT_ASSERT(preallocated_outputs.isTensorList());
    outputs = std::move(preallocated_outputs).toTensorList();
  }

  // TODO Avoid vector allocation. One idea would be to keep the std::vector
  // instances in the cache.
  std::vector<IValue> inputs = torch::jit::pop(*stack, num_inputs);

  // Convert outputs to caffe2::Tensor
  c10::SmallVector<caffe2::Tensor, 6> outputs_c2(num_outputs);
  for (auto i : c10::irange(num_outputs)) {
    outputs_c2[i] = caffe2::Tensor(outputs.get(i));
  }

  const StreamId stream(-1);
  auto new_outputs_c2 = (*call_op)(schema, std::move(inputs), outputs_c2, stream);


  bool return_tensor_list = false;
  if (schema.returns().size() == 1) {
    auto type = schema.returns()[0].type();
    if (c10::ListTypePtr list_type = type->cast<c10::ListType>()) {
      if (list_type->getElementType()->kind() == c10::TypeKind::TensorType) {
        return_tensor_list = true;
      }
    }
  }
  if (return_tensor_list) {
    for (auto i : c10::irange(num_outputs)) {
      outputs.set(i, at::Tensor(std::move(new_outputs_c2[i])));
    }
    torch::jit::push(*stack, outputs);
  } else {
    for (auto i : c10::irange(num_outputs)) {
      torch::jit::push(*stack, at::Tensor(std::move(new_outputs_c2[i])));
    }
  }

  // postcondition: All inputs are cleared from the stack, there's now one
  //                IValue for each output which holds the result. This
  //                might reuse one of the preallocated tensors but doesn't have
  //                to.
}

static FunctionSchema make_function_schema_for_c10(const char* schema_str) {
#if !defined(EXPOSE_C2_OPS) &&                              \
  (defined(CAFFE2_IS_XPLAT_BUILD) || defined(C10_MOBILE))
  throw std::logic_error(
      "We don't support registering c10 ops on mobile yet because the function schema parser isn't present in the mobile build.");
#else
  c10::FunctionSchema parsed_schema = torch::jit::parseSchema(schema_str);
  std::vector<c10::Argument> arguments = parsed_schema.arguments();
  arguments.emplace_back(
      PREALLOCATED_OUTPUT_ARGNAME,
      c10::OptionalType::create(c10::ListType::ofTensors()),
      nullopt,
      IValue());

  return FunctionSchema(
      parsed_schema.name(),
      parsed_schema.overload_name(),
      std::move(arguments),
      parsed_schema.returns(),
      parsed_schema.is_vararg(),
      parsed_schema.is_varret());
#endif
}

InitCPUDefinition::InitCPUDefinition(const char *name, KernelFunction func) {
  static torch::Library cpu_lib(
      torch::Library::IMPL, "_caffe2", c10::DispatchKey::CPU,
      __FILE__, __LINE__);
  if (c10::impl::dispatch_key_allowlist_check(c10::DispatchKey::CPU)) {
    cpu_lib.def(name, torch::CppFunction::makeFromKernelFunction(func));
  }
}

InitCUDADefinition::InitCUDADefinition(const char *name, KernelFunction func) {
  static torch::Library cuda_lib(
      torch::Library::IMPL, "_caffe2", c10::DispatchKey::CUDA,
      __FILE__, __LINE__);
  if (c10::impl::dispatch_key_allowlist_check(c10::DispatchKey::CUDA)) {
    cuda_lib.def(name, torch::CppFunction::makeFromKernelFunction(func));
  }
}

InitHIPDefinition::InitHIPDefinition(const char *name, KernelFunction func) {
  static torch::Library hip_lib(
      torch::Library::IMPL, "_caffe2", c10::DispatchKey::HIP,
      __FILE__, __LINE__);
  if (c10::impl::dispatch_key_allowlist_check(c10::DispatchKey::HIP)) {
    hip_lib.def(name, torch::CppFunction::makeFromKernelFunction(func));
  }
}

InitSchema::InitSchema(const char *schema_str) {
  static torch::Library schema_lib(
      torch::Library::FRAGMENT, "_caffe2", c10::nullopt,
      __FILE__, __LINE__);
  schema_lib.def(make_function_schema_for_c10(schema_str));
}

}  // namespace detail
}  // namespace caffe2

#endif
