#ifndef ONEFLOW_CORE_OPERATOR_CUDA_RING_ALL_REDUCE_OP_H_
#define ONEFLOW_CORE_OPERATOR_CUDA_RING_ALL_REDUCE_OP_H_

#include "oneflow/core/operator/operator.h"

namespace oneflow {

class CudaRingAllReduceOp : public Operator {
 public:
  OF_DISALLOW_COPY_AND_MOVE(CudaRingAllReduceOp);
  CudaRingAllReduceOp() = default;
  ~CudaRingAllReduceOp() override = default;

  void InitFromOpConf() override;

 private:
  LogicalBlobId ibn2lbi(const std::string& input_bn) const override;
  LogicalBlobId obn2lbi(const std::string& output_bn) const override;
  const PbMessage& GetCustomizedConf() const override;
  void InferBlobDescs(std::function<BlobDesc*(const std::string&)> GetBlobDesc4BnInOp,
                      const ParallelContext* parallel_ctx) const override;
  void VirtualGenKernelConf(std::function<const BlobDesc*(const std::string&)> GetBlobDesc4BnInOp,
                            const ParallelContext*, KernelConf*, const OpContext*) const override;
};

}  // namespace oneflow

#endif  // #define ONEFLOW_CORE_OPERATOR_CUDA_RING_ALL_REDUCE_OP_H_
