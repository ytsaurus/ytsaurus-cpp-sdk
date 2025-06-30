#include "job_info.h"

namespace NYT::NDataNode {

using namespace NJobAgent;
using namespace NClusterNode;

////////////////////////////////////////////////////////////////////////////////

TBriefJobInfo::TBriefJobInfo(
    NChunkServer::TJobId jobId,
    NJobAgent::EJobState jobState,
    NJobAgent::EJobType jobType,
    const std::string& jobTrackerAddress,
    TInstant jobStartTime,
    TDuration jobDuration,
    const NClusterNode::TJobResources& baseResourceUsage,
    const NClusterNode::TJobResources& additionalResourceUsage)
    : JobId_(jobId)
    , JobState_(jobState)
    , JobType_(jobType)
    , JobTrackerAddress_(jobTrackerAddress)
    , JobStartTime_(jobStartTime)
    , JobDuration_(jobDuration)
    , BaseResourceUsage_(baseResourceUsage)
    , AdditionalResourceUsage_(additionalResourceUsage)
{ }

void TBriefJobInfo::BuildOrchid(NYTree::TFluentMap fluent) const
{
    fluent.Item(ToString(JobId_)).BeginMap()
        .Item("job_state").Value(JobState_)
        .Item("job_type").Value(JobType_)
        .Item("job_tracker_address").Value(JobTrackerAddress_)
        .Item("start_time").Value(JobStartTime_)
        .Item("duration").Value(JobDuration_)
        .Item("base_resource_usage").Value(BaseResourceUsage_)
        .Item("additional_resource_usage").Value(AdditionalResourceUsage_)
    .EndMap();
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NDataNode
