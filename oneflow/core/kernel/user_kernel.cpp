#include "oneflow/core/kernel/kernel.h"
#include "oneflow/core/kernel/eager_kernel.h"
#include "oneflow/core/framework/op_kernel.h"
#include "oneflow/core/framework/op_kernel_infer_cache.h"
#include "oneflow/core/framework/op_registration.h"
#include "oneflow/core/framework/kernel_registration.h"
#include "oneflow/core/framework/tensor.h"
#include "oneflow/core/framework/user_op_conf.h"
#include "oneflow/core/framework/infer_util.h"
#include "oneflow/core/kernel/kernel.h"

namespace oneflow {

namespace {

void FillTensorDescWithBlob(const Blob* blob, user_op::TensorDesc* tensor_desc) {
  BlobDescProto proto;
  blob->blob_desc().header_pod_desc().ToProto(proto.mutable_header());
  blob->blob_desc().body().ToProto(proto.mutable_body());
  proto.set_is_tensor_list(blob->blob_desc().is_tensor_list());
  proto.set_is_body_disabled(blob->blob_desc().is_body_disabled());
  proto.set_is_dynamic(blob->blob_desc().is_dynamic());
  proto.set_header_is_opaque(blob->blob_desc().header_is_opaque());
  *tensor_desc = proto;
  tensor_desc->mut_shape()->CheckNumAxesIdenticalAndAssign(blob->shape());
}

}  // namespace

using Arg2Tensor = HashMap<std::pair<std::string, int32_t>, std::unique_ptr<user_op::Tensor>>;
using ArgVec = std::vector<std::pair<std::string, int32_t>>;

class UserKernelBaseContext {
 public:
  UserKernelBaseContext(const KernelConf& kernel_conf) {
    CHECK(kernel_conf.has_user_conf());
    CHECK(kernel_conf.op_attribute().op_conf().has_user_conf());

    auto InitInOrOut = [&](const PbMap<std::string, UserOpConf::ListString>& arg_map,
                           ArgVec* arg_vec) {
      for (auto it = arg_map.begin(); it != arg_map.end(); ++it) {
        for (int32_t i = 0; i < it->second.s_size(); ++i) {
          arg_vec->emplace_back(std::make_pair(it->first, i));
        }
      }
    };
    InitInOrOut(kernel_conf.op_attribute().op_conf().user_conf().input(), &inputs_);
    InitInOrOut(kernel_conf.op_attribute().op_conf().user_conf().output(), &outputs_);

    device_type_ = kernel_conf.op_attribute().op_conf().device_type();
    parallel_ctx_ = kernel_conf.user_conf().parallel_ctx();
    for (const auto& pair : kernel_conf.user_conf().bn_in_op2blob_desc()) {
      arg2tensor_desc_.emplace(GenUnRepeatedBn(pair.first), user_op::TensorDesc(pair.second));
    }
  }
  ~UserKernelBaseContext() = default;

  DeviceType device_type() const { return device_type_; }
  const ParallelContext& parallel_ctx() const { return parallel_ctx_; }
  const user_op::TensorDesc* TensorDesc4ArgNameAndIndex(const std::string& arg_name,
                                                        int32_t index) const {
    auto it = arg2tensor_desc_.find(std::make_pair(arg_name, index));
    if (it == arg2tensor_desc_.end()) { return nullptr; }
    return &(it->second);
  }

  const ArgVec& inputs() const { return inputs_; }
  const ArgVec& outputs() const { return outputs_; }

 private:
  ArgVec inputs_;
  ArgVec outputs_;
  DeviceType device_type_;
  ParallelContext parallel_ctx_;
  HashMap<std::pair<std::string, int32_t>, user_op::TensorDesc> arg2tensor_desc_;
};

class UserKernelInitContext final : public user_op::KernelInitContext {
 public:
  explicit UserKernelInitContext(DeviceCtx* device_ctx, const KernelConf& kernel_conf)
      : user_op::KernelInitContext(
            user_op::UserOpConfWrapper(kernel_conf.op_attribute().op_conf())),
        device_ctx_(device_ctx),
        base_ctx_(UserKernelBaseContext(kernel_conf)),
        sbp_signature_(&(kernel_conf.user_conf().sbp_sig())) {}
  ~UserKernelInitContext() = default;

  DeviceCtx* device_ctx() override { return device_ctx_; }

  DeviceType device_type() const override { return base_ctx_.device_type(); }
  const ParallelContext& parallel_ctx() const override { return base_ctx_.parallel_ctx(); }
  const user_op::TensorDesc* TensorDesc4ArgNameAndIndex(const std::string& arg_name,
                                                        int32_t index) const override {
    return base_ctx_.TensorDesc4ArgNameAndIndex(arg_name, index);
  }
  const SbpParallel& SbpParallel4ArgNameAndIndex(const std::string& arg_name,
                                                 int32_t index) const override {
    const auto& bn2sbp = sbp_signature_->bn_in_op2sbp_parallel();
    std::string bn = GenRepeatedBn(arg_name, index);
    auto it = bn2sbp.find(bn);
    CHECK(it != bn2sbp.end());
    return it->second;
  }
  const ArgVec& inputs() const override { return base_ctx_.inputs(); }
  const ArgVec& outputs() const override { return base_ctx_.outputs(); }

 private:
  DeviceCtx* device_ctx_;
  UserKernelBaseContext base_ctx_;
  const SbpSignature* sbp_signature_;
};

class UserKernelOpInferContext : public user_op::InferContext {
 public:
  UserKernelOpInferContext(const OperatorConf& op_conf)
      : user_op::InferContext(user_op::UserOpConfWrapper(op_conf)) {
    auto* bn2sbp = sbp_signature_.mutable_bn_in_op2sbp_parallel();
    auto InitArgs7TensorDesc7Sbp = [&](const PbMap<std::string, UserOpConf::ListString>& arg_map,
                                       ArgVec* arg_vec) {
      for (auto it = arg_map.begin(); it != arg_map.end(); ++it) {
        const std::string& arg_name = it->first;
        for (int32_t i = 0; i < it->second.s_size(); ++i) {
          std::pair<std::string, int32_t> arg_pair = std::make_pair(arg_name, i);
          arg_vec->emplace_back(arg_pair);
          arg2tensor_desc_.emplace(arg_pair, nullptr);
          const std::string& bn_in_op = GenRepeatedBn(arg_name, i);
          (*bn2sbp)[bn_in_op].mutable_split_parallel()->set_axis(0);
        }
      }
    };
    InitArgs7TensorDesc7Sbp(op_conf.user_conf().input(), &inputs_);
    InitArgs7TensorDesc7Sbp(op_conf.user_conf().output(), &outputs_);
    parallel_ctx_.set_parallel_id(0);
    parallel_ctx_.set_parallel_num(1);
  }
  ~UserKernelOpInferContext() = default;

  user_op::TensorDesc* TensorDesc4ArgNameAndIndex(const std::string& arg_name,
                                                  int32_t index) override {
    return arg2tensor_desc_.at(std::make_pair(arg_name, index)).get();
  }
  Shape* Shape4ArgNameAndIndex(const std::string& arg_name, int32_t index) override {
    return TensorDesc4ArgNameAndIndex(arg_name, index)->mut_shape();
  }
  DataType* Dtype4ArgNameAndIndex(const std::string& arg_name, int32_t index) override {
    return TensorDesc4ArgNameAndIndex(arg_name, index)->mut_data_type();
  }
  bool* IsDynamic4ArgNameAndIndex(const std::string& arg_name, int32_t index) override {
    return TensorDesc4ArgNameAndIndex(arg_name, index)->mut_is_dynamic();
  }
  bool* IsTensorList4ArgNameAndIndex(const std::string& arg_name, int32_t index) override {
    return TensorDesc4ArgNameAndIndex(arg_name, index)->mut_is_tensor_list();
  }

  const ArgVec& inputs() const override { return inputs_; }
  const ArgVec& outputs() const override { return outputs_; }
  const ParallelContext& parallel_ctx() const override { return parallel_ctx_; };
  const SbpParallel& SbpParallel4ArgNameAndIndex(const std::string& arg_name,
                                                 int32_t index) const override {
    const auto& bn2sbp = sbp_signature_.bn_in_op2sbp_parallel();
    std::string bn = GenRepeatedBn(arg_name, index);
    auto it = bn2sbp.find(bn);
    CHECK(it != bn2sbp.end());
    return it->second;
  }

  void UpdateArg2TensorDesc(const std::function<Blob*(const std::string&)>& BnInOp2Blob) {
    for (auto& pair : arg2tensor_desc_) {
      const auto& arg_pair = pair.first;
      auto& arg_tensor_desc = pair.second;
      Blob* blob = BnInOp2Blob(GenRepeatedBn(arg_pair.first, arg_pair.second));
      CHECK_NOTNULL(blob);
      if (arg_tensor_desc) {
        arg_tensor_desc->mut_shape()->CheckNumAxesIdenticalAndAssign(blob->shape());
      } else {
        arg_tensor_desc.reset(new user_op::TensorDesc());
        FillTensorDescWithBlob(blob, arg_tensor_desc.get());
      }
    }
  }

 private:
  ArgVec inputs_;
  ArgVec outputs_;
  ParallelContext parallel_ctx_;
  SbpSignature sbp_signature_;
  HashMap<std::pair<std::string, int32_t>, std::unique_ptr<user_op::TensorDesc>> arg2tensor_desc_;
};

class UserKernelInferContext final : public user_op::KernelInferContext {
 public:
  explicit UserKernelInferContext(DeviceCtx* device_ctx, const KernelConf& kernel_conf)
      : user_op::KernelInferContext(
            user_op::UserOpConfWrapper(kernel_conf.op_attribute().op_conf())),
        device_ctx_(device_ctx),
        base_ctx_(UserKernelBaseContext(kernel_conf)),
        op_infer_ctx_(kernel_conf.op_attribute().op_conf()) {
    auto InitArg2Blob = [this](const PbMap<std::string, UserOpConf::ListString>& arg_map) {
      for (auto it = arg_map.begin(); it != arg_map.end(); ++it) {
        const std::string& arg_name = it->first;
        for (int32_t i = 0; i < it->second.s_size(); ++i) {
          arg2tensor_.emplace(std::make_pair(arg_name, i), nullptr);
        }
      }
    };
    InitArg2Blob(kernel_conf.op_attribute().op_conf().user_conf().input());
    InitArg2Blob(kernel_conf.op_attribute().op_conf().user_conf().output());

    const auto* op_reg_val = user_op::LookUpInOpRegistry(
        kernel_conf.op_attribute().op_conf().user_conf().op_type_name());
    CHECK_NOTNULL(op_reg_val);
    tensor_desc_infer_fn_ = op_reg_val->tensor_desc_infer_fn;
  }
  ~UserKernelInferContext() = default;

  DeviceType device_type() const override { return base_ctx_.device_type(); }
  const ParallelContext& parallel_ctx() const override { return base_ctx_.parallel_ctx(); }
  const user_op::TensorDesc* TensorDesc4ArgNameAndIndex(const std::string& arg_name,
                                                        int32_t index) const override {
    return base_ctx_.TensorDesc4ArgNameAndIndex(arg_name, index);
  }
  const ArgVec& inputs() const override { return base_ctx_.inputs(); }
  const ArgVec& outputs() const override { return base_ctx_.outputs(); }

  DeviceCtx* device_ctx() override { return device_ctx_; }
  user_op::Tensor* Tensor4ArgNameAndIndex(const std::string& arg_name, int32_t arg_index) override {
    auto it = arg2tensor_.find(std::make_pair(arg_name, arg_index));
    CHECK(it != arg2tensor_.end()) << "Arg (" << arg_name << "," << arg_index << ") is not found";
    return it->second.get();
  }
  const ShapeView& ShapeView4ArgNameAndIndex(const std::string& arg_name,
                                             int32_t arg_index) override {
    user_op::Tensor* arg_tensor = Tensor4ArgNameAndIndex(arg_name, arg_index);
    CHECK(arg_tensor != nullptr) << "Tensor of arg (" << arg_name << "," << arg_index
                                 << ") is not found";
    return arg_tensor->shape();
  }
  MutShapeView* MutShapeView4ArgNameAndIndex(const std::string& arg_name,
                                             int32_t arg_index) override {
    user_op::Tensor* arg_tensor = Tensor4ArgNameAndIndex(arg_name, arg_index);
    CHECK(arg_tensor != nullptr) << "Tensor of arg (" << arg_name << "," << arg_index
                                 << ") is not found";
    return arg_tensor->mut_shape();
  }

  user_op::InferContext* MutOpInferContext() override { return &op_infer_ctx_; }
  const user_op::TensorDescInferFn& GetOpInferFn() override { return tensor_desc_infer_fn_; }

  void UpdateArg2Tensor(const std::function<Blob*(const std::string&)>& BnInOp2Blob) {
    for (auto& pair : arg2tensor_) {
      const auto& arg_pair = pair.first;
      auto& arg_tensor = pair.second;
      Blob* blob = BnInOp2Blob(GenRepeatedBn(arg_pair.first, arg_pair.second));
      CHECK_NOTNULL(blob);
      if (arg_tensor) {
        *arg_tensor = std::move(user_op::Tensor(blob));
      } else {
        arg_tensor.reset(new user_op::Tensor(blob));
      }
    }
  }

 private:
  DeviceCtx* device_ctx_;
  UserKernelBaseContext base_ctx_;
  UserKernelOpInferContext op_infer_ctx_;
  user_op::TensorDescInferFn tensor_desc_infer_fn_;
  HashMap<std::pair<std::string, int32_t>, std::unique_ptr<user_op::Tensor>> arg2tensor_;
};

class UserKernelComputeContext final : public user_op::KernelComputeContext {
 public:
  explicit UserKernelComputeContext(DeviceCtx* device_ctx, const KernelConf& kernel_conf,
                                    const JobDesc& job_desc)
      : user_op::KernelComputeContext(
            user_op::UserOpConfWrapper(kernel_conf.op_attribute().op_conf())),
        device_ctx_(device_ctx),
        base_ctx_(std::move(UserKernelBaseContext(kernel_conf))),
        job_desc_(job_desc) {
    auto InitInOrOut = [&](const PbMap<std::string, UserOpConf::ListString>& arg_map) {
      for (auto it = arg_map.begin(); it != arg_map.end(); ++it) {
        const std::string& arg_name = it->first;
        for (int32_t i = 0; i < it->second.s_size(); ++i) {
          arg2tensor_.emplace(std::make_pair(arg_name, i), std::unique_ptr<user_op::Tensor>());
        }
      }
    };
    InitInOrOut(kernel_conf.op_attribute().op_conf().user_conf().input());
    InitInOrOut(kernel_conf.op_attribute().op_conf().user_conf().output());
    arg2tensor_.emplace(std::make_pair("tmp_buffer", 0), std::unique_ptr<user_op::Tensor>());
  }
  ~UserKernelComputeContext() = default;

  user_op::Tensor* Tensor4ArgNameAndIndex(const std::string& arg_name, int32_t index) override {
    auto it = arg2tensor_.find(std::make_pair(arg_name, index));
    if (it == arg2tensor_.end()) { return nullptr; }
    return it->second.get();
  }
  DeviceCtx* device_ctx() override { return device_ctx_; }
  const JobDesc& job_desc() const override { return job_desc_; }

  void UpdateTensorWithCorrBlob(std::function<Blob*(const std::string&)> BnInOp2Blob) {
    for (auto& pair : arg2tensor_) {
      std::string bn_in_op = GenRepeatedBn(pair.first.first, pair.first.second);
      Blob* blob = BnInOp2Blob(bn_in_op);
      if (blob == nullptr) {
        pair.second.reset();
      } else {
        pair.second.reset(new user_op::Tensor(blob));
      }
    }
  }

  DeviceType device_type() const override { return base_ctx_.device_type(); }
  const ParallelContext& parallel_ctx() const override { return base_ctx_.parallel_ctx(); }

  const ArgVec& inputs() const override { return base_ctx_.inputs(); }
  const ArgVec& outputs() const override { return base_ctx_.outputs(); }

 private:
  DeviceCtx* device_ctx_;
  Arg2Tensor arg2tensor_;
  UserKernelBaseContext base_ctx_;
  const JobDesc& job_desc_;
};

class UserKernelRegContext final : public user_op::KernelRegContext {
 public:
  explicit UserKernelRegContext(const KernelConf& kernel_conf)
      : user_op::KernelRegContext(user_op::UserOpConfWrapper(kernel_conf.op_attribute().op_conf())),
        base_ctx_(UserKernelBaseContext(kernel_conf)) {}
  ~UserKernelRegContext() = default;

  DeviceType device_type() const override { return base_ctx_.device_type(); }
  const ParallelContext& parallel_ctx() const override { return base_ctx_.parallel_ctx(); }
  const user_op::TensorDesc* TensorDesc4ArgNameAndIndex(const std::string& arg_name,
                                                        int32_t index) const override {
    return base_ctx_.TensorDesc4ArgNameAndIndex(arg_name, index);
  }
  const ArgVec& inputs() const override { return base_ctx_.inputs(); }
  const ArgVec& outputs() const override { return base_ctx_.outputs(); }

 private:
  UserKernelBaseContext base_ctx_;
};

class UserKernel final : public Kernel {
 public:
  OF_DISALLOW_COPY_AND_MOVE(UserKernel);
  UserKernel() = default;
  ~UserKernel() = default;

  void InitUserKernel(DeviceCtx* device_ctx) {
    ctx_.reset(new UserKernelComputeContext(device_ctx, kernel_conf(), job_desc()));
    infer_ctx_.reset(new UserKernelInferContext(device_ctx, kernel_conf()));
    infer_cache_.reset(new user_op::OpKernelInferCache(kernel_conf(), job_desc()));
    {
      const std::string& op_type_name =
          kernel_conf().op_attribute().op_conf().user_conf().op_type_name();
      const auto* kernel_reg_val =
          user_op::LookUpInKernelRegistry(op_type_name, UserKernelRegContext(kernel_conf()));
      CHECK_NOTNULL(kernel_reg_val);
      kernel_.reset(kernel_reg_val->create_fn());
    }
  }
  std::shared_ptr<user_op::OpKernelState> CreateOpKernelState(DeviceCtx* device_ctx) {
    UserKernelInitContext init_ctx(device_ctx, kernel_conf());
    return kernel_->CreateOpKernelState(&init_ctx);
  }
  void ForwardUserKernel(std::function<Blob*(const std::string&)> BnInOp2Blob,
                         user_op::OpKernelState* opkernel_state) const {
    ctx_->UpdateTensorWithCorrBlob(BnInOp2Blob);
    kernel_->Compute(ctx_.get(), opkernel_state);
  }

 private:
  void VirtualKernelInit(DeviceCtx* device_ctx) override {
    InitUserKernel(device_ctx);
    CHECK(opkernel_state_.get() == nullptr);
    opkernel_state_ = CreateOpKernelState(device_ctx);
  }

  void ForwardDataContent(const KernelCtx& ctx,
                          std::function<Blob*(const std::string&)> BnInOp2Blob) const override {
    ForwardUserKernel(BnInOp2Blob, opkernel_state_.get());
  }

  void ForwardShape(const KernelCtx& ctx,
                    std::function<Blob*(const std::string&)> BnInOp2Blob) const override {
    infer_ctx_->UpdateArg2Tensor(BnInOp2Blob);
    infer_cache_->UpdateCacheKey(infer_ctx_.get());
    if (!infer_cache_->IsCacheHit()) {
      auto* op_infer_ctx = dynamic_cast<UserKernelOpInferContext*>(infer_ctx_->MutOpInferContext());
      if (op_infer_ctx) { op_infer_ctx->UpdateArg2TensorDesc(BnInOp2Blob); }
      kernel_->InferShape(infer_ctx_.get());
      infer_cache_->UpdateCacheValue(infer_ctx_.get());
    } else {
      auto cache_value_ptr = infer_cache_->GetCacheValue();
      FOR_RANGE(int, i, 0, infer_ctx_->outputs().size()) {
        const auto& out_arg_pair = infer_ctx_->outputs().at(i);
        auto* mut_shape_view =
            infer_ctx_->MutShapeView4ArgNameAndIndex(out_arg_pair.first, out_arg_pair.second);
        mut_shape_view->set_shape(*cache_value_ptr->obn_idx2shape_sym.at(i));
      }
    }
  }

  bool IsStateless() const override { return !kernel_->AlwaysComputeWhenAllOutputsEmpty(); }

  std::shared_ptr<user_op::OpKernelState> opkernel_state_;
  std::unique_ptr<const user_op::OpKernel> kernel_;
  std::unique_ptr<UserKernelComputeContext> ctx_;
  std::unique_ptr<UserKernelInferContext> infer_ctx_;
  std::unique_ptr<user_op::OpKernelInferCache> infer_cache_;
};

NEW_REGISTER_KERNEL(OperatorConf::kUserConf, UserKernel).SetIsMatchedPred([](const KernelConf&) {
  return true;
});

EagerKernel::EagerKernel(const JobDesc* job_desc, const KernelConf& kernel_conf) {
  InitBase(job_desc, kernel_conf);
  InitOpKernel(kernel_conf);
}

void EagerKernel::InitOpKernel(const KernelConf& kernel_conf) {
  const std::string& op_type_name = kernel_conf.op_attribute().op_conf().user_conf().op_type_name();
  auto kernel_reg_val =
      user_op::LookUpInKernelRegistry(op_type_name, UserKernelRegContext(kernel_conf));
  CHECK_NOTNULL(kernel_reg_val);
  kernel_.reset(kernel_reg_val->create_fn());
}

void EagerKernel::Infer(std::function<Blob*(const std::string&)> BnInOp2Blob) const {
  if (!kernel_conf().need_do_shape()) { return; }
  UserKernelInferContext infer_ctx(nullptr, kernel_conf());
  infer_ctx.UpdateArg2Tensor(BnInOp2Blob);
  auto* op_infer_ctx = dynamic_cast<UserKernelOpInferContext*>(infer_ctx.MutOpInferContext());
  if (op_infer_ctx) { op_infer_ctx->UpdateArg2TensorDesc(BnInOp2Blob); }
  kernel_->InferShape(&infer_ctx);
}

std::shared_ptr<user_op::OpKernelState> EagerKernel::EagerModelForward(
    const std::shared_ptr<user_op::OpKernelState>& old_opkernel_state, DeviceCtx* device_ctx,
    std::function<Blob*(const std::string&)> BnInOp2Blob) const {
  std::shared_ptr<user_op::OpKernelState> new_opkernel_state;
  if (old_opkernel_state) {
    new_opkernel_state = old_opkernel_state;
  } else {
    CHECK_NOTNULL(&job_desc());
    UserKernelInitContext init_ctx(device_ctx, kernel_conf());
    new_opkernel_state = kernel_->CreateOpKernelState(&init_ctx);
  }
  // TODO(lixinqi): refactor to a lightweight KernelComputeContext
  UserKernelComputeContext compute_ctx(device_ctx, kernel_conf(), job_desc());
  compute_ctx.UpdateTensorWithCorrBlob(BnInOp2Blob);
  kernel_->Compute(&compute_ctx, new_opkernel_state.get());
  return new_opkernel_state;
}

}  // namespace oneflow
