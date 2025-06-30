#pragma once

#include "public.h"

#include <yt/yt/core/actions/invoker.h>
#include <yt/yt/core/logging/log.h>

#include <util/datetime/base.h>

namespace NYT::NJobProxy {

////////////////////////////////////////////////////////////////////////////////

DEFINE_ENUM(ECpuMonitorVote,
    (Increase)
    (Decrease)
    (Keep)
);

class TCpuMonitor
    : public TRefCounted
{
public:
    TCpuMonitor(
        NScheduler::TJobCpuMonitorConfigPtr config,
        IInvokerPtr invoker,
        TJobProxy* jobProxy,
        double initialCpuGuarantee);

    void Start();
    TFuture<void> Stop();
    void FillStatistics(TStatistics& statistics) const;

private:
    NScheduler::TJobCpuMonitorConfigPtr Config_;

    NConcurrency::TPeriodicExecutorPtr MonitoringExecutor_;

    TJobProxy* JobProxy_;

    bool Started = false;
    std::optional<TInstant> StartInstant;
    std::optional<TDuration> CheckedTimeInterval_;

    double AggregatedSmoothedCpuUsage_ = 0;
    double AggregatedMaxCpuUsage_ = 0;
    double AggregatedPreemptibleCpu_ = 0;

    const double HardLimit_;
    double SoftLimit_;
    double MinCpuLimit_;
    std::optional<double> SmoothedUsage_;

    std::optional<TInstant> LastCheckTime_;
    std::optional<TDuration> LastTotalCpu_;

    std::deque<ECpuMonitorVote> Votes_;

    NLogging::TLogger Logger;

    bool TryUpdateSmoothedValue();
    void UpdateVotes();
    std::optional<double> TryMakeDecision();
    void UpdateAggregates();
    bool CheckStarted();

    void DoCheck();
};

DEFINE_REFCOUNTED_TYPE(TCpuMonitor)

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NJobProxy
