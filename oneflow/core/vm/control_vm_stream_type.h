#ifndef ONEFLOW_CORE_VM_CONTROL_VM_STREAM_TYPE_H_
#define ONEFLOW_CORE_VM_CONTROL_VM_STREAM_TYPE_H_

#include "oneflow/core/vm/vm_stream_type.h"
#include "oneflow/core/vm/vm_instruction.msg.h"

namespace oneflow {

class VmScheduler;
class VmInstructionMsg;

class ControlVmStreamType final {
 public:
  ControlVmStreamType() = default;
  ~ControlVmStreamType() = default;

  static const VmStreamTypeId kVmStreamTypeId = 0;

  ObjectMsgPtr<VmInstructionMsg> NewMirroredObjectSymbol(const LogicalObjectId& logical_object_id,
                                                         int64_t parallel_num) const;
  ObjectMsgPtr<VmInstructionMsg> DeleteMirroredObjectSymbol(
      const LogicalObjectId& logical_object_id) const;

  void Run(VmScheduler* scheduler, VmInstructionMsg* vm_instr_msg) const;
};

}  // namespace oneflow

#endif  // ONEFLOW_CORE_VM_CONTROL_VM_STREAM_TYPE_H_
