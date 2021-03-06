#pragma once

#include <ATen/Error.h>
#include <ATen/ScalarType.h>
#include <type_traits>

// Implements instruction set specific function dispatch.
//
// Kernels that may make use of specialized instruction sets (e.g. AVX) are
// compiled multiple times with different compiler flags (e.g. -mavx). A
// DispatchStub contains a table of function pointers for a kernel. At runtime,
// the fastest available kernel is chosen based on the features reported by
// cpuinfo.
//
// Example:
//
// In native/MyKernel.h:
//   using fn_type = void(*)(const Tensor& x);
//   DECLARE_DISPATCH(fn_type, stub);
//
// In native/MyKernel.cpp
//   DEFINE_DISPATCH(stub);
//
// In native/cpu/MyKernel.cpp:
//   void kernel(const Tensor& x) { ... }
//   REGISTER_DISPATCH(stub, &kernel);
//
// To call:
//   stub(kCPU, tensor);

// ignore warnings about DispatchStub::DEFAULT, AVX, AVX2 defined elsewhere
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wundefined-var-template"
#endif

namespace at { namespace native {

enum class CPUCapability {
  DEFAULT = 0,
  AVX = 1,
  AVX2 = 2,
  NUM_OPTIONS
};

CPUCapability get_cpu_capability();

template <typename FnPtr, typename T>
struct DispatchStub {
  static_assert(std::is_pointer<FnPtr>::value, "FnPtr should be a pointer type");

  template <typename... ArgTypes>
  void operator()(Backend backend, ArgTypes... args) {
    if (backend == Backend::CPU) {
      if (!cpu_dispatch_ptr) {
        cpu_dispatch_ptr = choose_cpu_impl();
      }
      (*cpu_dispatch_ptr)(args...);
    } else if (backend == Backend::CUDA) {
      AT_ASSERTM(cuda_dispatch_ptr, "DispatchStub: missing CUDA kernel");
      (*cuda_dispatch_ptr)(args...);
    } else {
      AT_ERROR("DispatchStub: unsupported backend", backend);
    }
  }

  FnPtr choose_cpu_impl() {
    auto capability = static_cast<int>(get_cpu_capability());
    (void)capability;
#ifdef HAVE_AVX2_CPU_DEFINITION
    if (capability >= static_cast<int>(CPUCapability::AVX2)) {
      AT_ASSERTM(AVX2, "DispatchStub: missing AVX2 kernel");
      return AVX2;
    }
#endif
#ifdef HAVE_AVX_CPU_DEFINITION
    if (capability >= static_cast<int>(CPUCapability::AVX)) {
      AT_ASSERTM(AVX, "DispatchStub: missing AVX kernel");
      return AVX;
    }
#endif
    AT_ASSERTM(DEFAULT, "DispatchStub: missing default kernel");
    return DEFAULT;
  }

  FnPtr cpu_dispatch_ptr = nullptr;
  FnPtr cuda_dispatch_ptr = nullptr;
  static FnPtr DEFAULT;
#ifdef HAVE_AVX_CPU_DEFINITION
  static FnPtr AVX;
#endif
#ifdef HAVE_AVX2_CPU_DEFINITION
  static FnPtr AVX2;
#endif
};

namespace {
template <typename FnPtr, typename T>
struct RegisterDispatch {
  RegisterDispatch(DispatchStub<FnPtr, T>& stub, FnPtr value) {
    stub.cuda_dispatch_ptr = value;
  }
};
} // anonymous namespace

#define DECLARE_DISPATCH(fn, name) \
  extern struct name : DispatchStub<fn, name> {} name

#define DEFINE_DISPATCH(name) struct name name

#if defined(__CUDACC__)
#define REGISTER_DISPATCH(name, fn) \
  static RegisterDispatch<decltype(fn), struct name> name ## __register(name, fn);
#elif defined(CPU_CAPABILITY)
#define REGISTER_DISPATCH(name, fn) \
  template <> decltype(fn) DispatchStub<decltype(fn), struct name>::CPU_CAPABILITY = fn;
#endif


}} // namespace at::native


#if defined(__clang__)
#pragma clang diagnostic pop
#endif
