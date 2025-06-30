#pragma once

#include "job_info.h"
#include "public.h"

#include <yt/yt/server/node/job_agent/job_resource_manager.h>

#include <yt/yt/ytlib/scheduler/helpers.h>

#include <yt/yt/server/lib/chunk_server/proto/job_common.pb.h>

#include <yt/yt_proto/yt/client/node_tracker_client/proto/node.pb.h>

#include <yt/yt/core/logging/log.h>

#include <yt/yt/core/ytree/fluent.h>

namespace NYT::NDataNode {

////////////////////////////////////////////////////////////////////////////////

struct TMasterJobSensors
{
    NProfiling::TCounter AdaptivelyRepairedChunksCounter;
    NProfiling::TCounter TotalRepairedChunksCounter;
    NProfiling::TCounter FailedRepairChunksCounter;
};

////////////////////////////////////////////////////////////////////////////////

class TMasterJobBase
    : public TRefCounted
{
public:
    DEFINE_SIGNAL(void(const NClusterNode::TJobResources& resourcesDelta), ResourcesUpdated);
    DEFINE_SIGNAL(void(), JobPrepared);
    DEFINE_SIGNAL(void(), JobFinished);

public:
    TMasterJobBase(
        NChunkServer::TJobId jobId,
        const NChunkServer::NProto::TJobSpec& jobSpec,
        const std::string& jobTrackerAddress,
        const NClusterNode::TJobResources& resourceLimits,
        IBootstrap* bootstrap);

    NChunkServer::TJobId GetId() const noexcept;
    NJobAgent::EJobType GetType() const;
    bool IsUrgent() const;
    const std::string& GetJobTrackerAddress() const;

    bool IsStarted() const noexcept;
    NJobAgent::EJobState GetState() const;
    TInstant GetStartTime() const;
    NClusterNode::TJobResources GetResourceUsage() const;
    NChunkServer::NProto::TJobResult GetResult() const;
    TBriefJobInfo GetBriefInfo() const;

    void Start();
    void Abort(const TError& error);

    const NJobAgent::TResourceHolderPtr& GetResourceHolder() const noexcept;

protected:
    IBootstrap* const Bootstrap_;

    const TDataNodeConfigPtr Config_;

    const NChunkServer::TJobId JobId_;
    const NChunkServer::NProto::TJobSpec JobSpec_;
    const std::string JobTrackerAddress_;

    NLogging::TLogger Logger;

    NJobAgent::TResourceHolderPtr ResourceHolder_;

    const NNodeTrackerClient::TNodeDirectoryPtr NodeDirectory_;
    const IMemoryUsageTrackerPtr MemoryUsageTracker_;

    const TInstant StartTime_;
    bool Started_ = false;
    NJobAgent::EJobState JobState_ = NJobAgent::EJobState::Waiting;

    TFuture<void> JobFuture_;
    NChunkServer::NProto::TJobResult Result_;

    DECLARE_THREAD_AFFINITY_SLOT(JobThread);

    virtual TFuture<void> DoRun() = 0;
    TFuture<void> GuardedRun();

    void SetCompleted();
    void SetFailed(const TError& error);
    void SetAborted(const TError& error);

    IChunkPtr FindLocalChunk(TChunkId chunkId, int mediumIndex);
    IChunkPtr FindLocalChunk(TChunkId chunkId, TChunkLocationUuid locationUuid);

    IChunkPtr GetLocalChunkOrThrow(TChunkId chunkId, int mediumIndex);
    IChunkPtr GetLocalChunkOrThrow(TChunkId chunkId, TChunkLocationUuid locationUuid);

private:
    void DoSetFinished(NJobAgent::EJobState finalState, const TError& error);
};

DEFINE_REFCOUNTED_TYPE(TMasterJobBase)

////////////////////////////////////////////////////////////////////////////////

TMasterJobBasePtr CreateJob(
    NChunkServer::TJobId jobId,
    NChunkServer::NProto::TJobSpec&& jobSpec,
    const std::string& jobTrackerAddress,
    const NClusterNode::TJobResources& resourceLimits,
    IBootstrap* bootstrap,
    const TMasterJobSensors& sensors);

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NDataNode
