#include "oneflow/core/vm/control_vm_stream_type.h"
#include "oneflow/core/common/util.h"
#include "oneflow/core/vm/scheduler.msg.h"
#include "oneflow/core/vm/vm_desc.msg.h"
#include "oneflow/core/common/cached_object_msg_allocator.h"

namespace oneflow {

namespace test {

namespace {

using VmInstructionMsgList = OBJECT_MSG_LIST(VmInstructionMsg, vm_instr_msg_link);

TEST(ControlVmStreamType, new_symbol) {
  auto vm_desc = ObjectMsgPtr<VmDesc>::New();
  CachedObjectMsgAllocator allocator(20, 100);
  auto scheduler = ObjectMsgPtr<VmScheduler>::NewFrom(&allocator, vm_desc.Get());
  VmInstructionMsgList list;
  int64_t parallel_num = 8;
  uint64_t symbol_value = 9527;
  list.EmplaceBack(ControlVmStreamType().NewMirroredObjectSymbol(symbol_value, parallel_num));
  ASSERT_TRUE(scheduler->waiting_msg_list().empty());
  scheduler->Receive(&list);
  ASSERT_EQ(scheduler->waiting_msg_list().size(), 1);
  scheduler->Schedule();
  ASSERT_TRUE(scheduler->waiting_msg_list().empty());
  ASSERT_TRUE(scheduler->waiting_vm_instr_chain_list().empty());
  ASSERT_TRUE(scheduler->active_vm_stream_list().empty());
  ASSERT_TRUE(scheduler->vm_thread_list().empty());
  ASSERT_TRUE(scheduler->vm_stream_type_id2vm_stream_rt_desc().empty());
  ASSERT_TRUE(scheduler->zombie_logical_object_list().empty());
  ASSERT_EQ(scheduler->id2logical_object().size(), 1);
  auto* logical_object = scheduler->mut_id2logical_object()->FindPtr(symbol_value);
  ASSERT_NE(logical_object, nullptr);
  ASSERT_EQ(logical_object->parallel_id2mirrored_object().size(), parallel_num);
}

TEST(ControlVmStreamType, delete_symbol) {
  auto vm_desc = ObjectMsgPtr<VmDesc>::New();
  auto scheduler = ObjectMsgPtr<VmScheduler>::New(vm_desc.Get());
  VmInstructionMsgList list;
  int64_t parallel_num = 8;
  uint64_t symbol_value = 9527;
  list.EmplaceBack(ControlVmStreamType().NewMirroredObjectSymbol(symbol_value, parallel_num));
  list.EmplaceBack(ControlVmStreamType().DeleteMirroredObjectSymbol(symbol_value));
  ASSERT_TRUE(scheduler->waiting_msg_list().empty());
  scheduler->Receive(&list);
  ASSERT_EQ(scheduler->waiting_msg_list().size(), 2);
  scheduler->Schedule();
  ASSERT_TRUE(scheduler->waiting_msg_list().empty());
  ASSERT_TRUE(scheduler->waiting_vm_instr_chain_list().empty());
  ASSERT_TRUE(scheduler->active_vm_stream_list().empty());
  ASSERT_TRUE(scheduler->vm_thread_list().empty());
  ASSERT_TRUE(scheduler->vm_stream_type_id2vm_stream_rt_desc().empty());
  ASSERT_TRUE(scheduler->zombie_logical_object_list().empty());
  ASSERT_TRUE(scheduler->id2logical_object().empty());
}

}  // namespace

}  // namespace test

}  // namespace oneflow
