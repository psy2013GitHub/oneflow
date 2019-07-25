#ifndef ONEFLOW_CORE_JOB_GLOBAL_ENVIRONMENT_OBJECTS_SCOPE_H_
#define ONEFLOW_CORE_JOB_GLOBAL_ENVIRONMENT_OBJECTS_SCOPE_H_

#include "oneflow/core/common/util.h"
#include "oneflow/core/job/job_set.pb.h"
#include "oneflow/core/job/flags_and_log_scope.h"

namespace oneflow {

class GlobalEnvironmentObjectsScope final {
 public:
  OF_DISALLOW_COPY_AND_MOVE(GlobalEnvironmentObjectsScope);
  explicit GlobalEnvironmentObjectsScope(const ConfigProto& config_proto);
  ~GlobalEnvironmentObjectsScope();

 private:
  std::unique_ptr<FlagsAndLogScope> flags_and_log_scope_;
};

}  // namespace oneflow

#endif  // ONEFLOW_CORE_JOB_GLOBAL_ENVIRONMENT_OBJECTS_SCOPE_H_
