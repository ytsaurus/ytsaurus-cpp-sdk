#include "cpu_monitor.h"
#include "job_proxy.h"

#include <yt/yt/core/misc/statistic_path.h>

#include <yt/yt/core/concurrency/periodic_executor.h>

namespace NYT::NJobProxy {

////////////////////////////////////////////////////////////////////////////////

TCpuMonitor::TCpuMonitor(
    NScheduler::TJobCpuMonitorConfigPtr config,
    IInvokerPtr invoker,
    TJobProxy* jobProxy,
    const double initialCpuGuarantee)
    : Config_(std::move(config))
    , MonitoringExecutor_(New<NConcurrency::TPeriodicExecutor>(
        std::move(invoker),
        BIND(&TCpuMonitor::DoCheck, MakeWeak(this)),
        Config_->CheckPeriod))
    , JobProxy_(jobProxy)
    , HardLimit_(initialCpuGuarantee)
    , SoftLimit_(initialCpuGuarantee)
    , MinCpuLimit_(std::min(initialCpuGuarantee, Config_->MinCpuLimit))
    , Logger("CpuMonitor")
{ }

void TCpuMonitor::Start()
{
    StartInstant = TInstant::Now() + Config_->StartDelay;
    MonitoringExecutor_->Start();
}

TFuture<void> TCpuMonitor::Stop()
{
    return MonitoringExecutor_->Stop();
}

void TCpuMonitor::FillStatistics(TStatistics& statistics) const
{
    using namespace NStatisticPath;

    if (SmoothedUsage_) {
        statistics.AddSample("/job_proxy/smoothed_cpu_usage_x100"_SP, static_cast<i64>(*SmoothedUsage_ * 100));
        statistics.AddSample("/job_proxy/preemptible_cpu_x100"_SP, static_cast<i64>((HardLimit_ - SoftLimit_) * 100));

        statistics.AddSample("/job_proxy/aggregated_smoothed_cpu_usage_x100"_SP, static_cast<i64>(AggregatedSmoothedCpuUsage_ * 100));
        statistics.AddSample("/job_proxy/aggregated_max_cpu_usage_x100"_SP, static_cast<i64>(AggregatedMaxCpuUsage_ * 100));
        const auto preemptibleCpu = static_cast<i64>(AggregatedPreemptibleCpu_ * 100);
        statistics.AddSample("/job_proxy/aggregated_preemptible_cpu_x100"_SP, preemptibleCpu);
        statistics.AddSample("/job_proxy/aggregated_preempted_cpu_x100"_SP, Config_->EnableCpuReclaim ? preemptibleCpu : 0);
    }
}

void TCpuMonitor::DoCheck()
{
    if (!CheckStarted()) {
        return;
    }

    if (!TryUpdateSmoothedValue()) {
        return;
    }
    UpdateVotes();

    auto decision = TryMakeDecision();
    if (decision) {
        if (!Config_->EnableCpuReclaim || JobProxy_->TrySetCpuGuarantee(*decision)) {
            YT_LOG_DEBUG("Soft limit changed (OldValue: %v, NewValue: %v)",
                SoftLimit_,
                *decision);
            SoftLimit_ = *decision;
        } else {
            YT_LOG_DEBUG("Unable to change soft limit: job proxy refused to change CPU share");
        }
    }

    UpdateAggregates();
}

bool TCpuMonitor::TryUpdateSmoothedValue()
{
    TDuration totalCpu;
    try {
        totalCpu = JobProxy_->GetSpentCpuTime();
    } catch (const std::exception& ex) {
        YT_LOG_ERROR(ex, "Failed to get CPU statistics");
        return false;
    }

    auto now = TInstant::Now();
    bool canCalcSmoothedUsage = LastCheckTime_ && LastTotalCpu_;
    if (canCalcSmoothedUsage) {
        CheckedTimeInterval_ = (now - *LastCheckTime_);
        auto deltaCpu = totalCpu - *LastTotalCpu_;
        auto cpuUsage = deltaCpu / *CheckedTimeInterval_;
        auto newSmoothedUsage = SmoothedUsage_
            ? Config_->SmoothingFactor * cpuUsage + (1 - Config_->SmoothingFactor) * (*SmoothedUsage_)
            : HardLimit_;
        YT_LOG_DEBUG("Smoothed CPU usage updated (OldValue: %v, NewValue: %v)",
            SmoothedUsage_,
            newSmoothedUsage);
        SmoothedUsage_ = newSmoothedUsage;
    }
    LastCheckTime_ = now;
    LastTotalCpu_ = totalCpu;
    return canCalcSmoothedUsage;
}

void TCpuMonitor::UpdateVotes()
{
    double ratio = *SmoothedUsage_ / SoftLimit_;

    if (ratio < Config_->RelativeLowerBound) {
        Votes_.emplace_back(ECpuMonitorVote::Decrease);
    } else if (ratio > Config_->RelativeUpperBound) {
        Votes_.emplace_back(ECpuMonitorVote::Increase);
    } else {
        Votes_.emplace_back(ECpuMonitorVote::Keep);
    }
}

std::optional<double> TCpuMonitor::TryMakeDecision()
{
    std::optional<double> result;
    if (std::ssize(Votes_) >= Config_->VoteWindowSize) {
        auto voteSum = 0;
        for (const auto vote : Votes_) {
            if (vote == ECpuMonitorVote::Increase) {
                ++voteSum;
            } else if (vote == ECpuMonitorVote::Decrease) {
                --voteSum;
            }
        }
        if (voteSum > Config_->VoteDecisionThreshold) {
            auto softLimit = std::min(SoftLimit_ * Config_->IncreaseCoefficient, HardLimit_);
            if (softLimit != SoftLimit_) {
                result = softLimit;
            }
            Votes_.clear();
        } else if (voteSum < -Config_->VoteDecisionThreshold) {
            auto softLimit = std::max(SoftLimit_ * Config_->DecreaseCoefficient, MinCpuLimit_);
            if (softLimit != SoftLimit_) {
                result = softLimit;
            }
            Votes_.clear();
        } else {
            Votes_.pop_front();
        }
    }
    return result;
}

void TCpuMonitor::UpdateAggregates()
{
    if (!CheckedTimeInterval_ || !SmoothedUsage_) {
        return;
    }
    double seconds = static_cast<double>(CheckedTimeInterval_->MicroSeconds()) / 1000 / 1000;
    AggregatedSmoothedCpuUsage_ += *SmoothedUsage_ * seconds;
    AggregatedPreemptibleCpu_ += (HardLimit_ - SoftLimit_) * seconds;
    AggregatedMaxCpuUsage_ += HardLimit_ * seconds;
}

bool TCpuMonitor::CheckStarted()
{
    YT_VERIFY(StartInstant);

    if (Started) {
        return true;
    }
    if (TInstant::Now() >= StartInstant.value()) {
        Started = true;
        YT_LOG_DEBUG("Job CPU monitor is started");
    }
    return Started;
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NJobProxy
