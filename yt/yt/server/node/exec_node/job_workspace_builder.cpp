#include "job_workspace_builder.h"
#include "allocation.h"
#include "slot.h"

#include "job_gpu_checker.h"

#include <yt/yt/server/lib/exec_node/helpers.h>

#include <yt/yt/library/containers/cri/image_cache.h>

#include <yt/yt/core/actions/cancelable_context.h>

#include <yt/yt/core/concurrency/thread_affinity.h>
#include <yt/yt/core/concurrency/delayed_executor.h>

#include <yt/yt/core/misc/fs.h>

namespace NYT::NExecNode {

using namespace NContainers::NCri;
using namespace NConcurrency;
using namespace NContainers;
using namespace NJobAgent;
using namespace NFS;

static const std::string SetupCommandsTag = "setup";

////////////////////////////////////////////////////////////////////////////////

TJobWorkspaceBuilder::TJobWorkspaceBuilder(
    IInvokerPtr invoker,
    TJobWorkspaceBuildingContext context,
    IJobDirectoryManagerPtr directoryManager)
    : Invoker_(std::move(invoker))
    , Context_(std::move(context))
    , DirectoryManager_(std::move(directoryManager))
    , Logger(Context_.Logger)
{
    YT_VERIFY(Context_.Slot);
    YT_VERIFY(Context_.Job);
    YT_VERIFY(DirectoryManager_);
}

template<TFuture<void>(TJobWorkspaceBuilder::*Step)()>
TFuture<void> TJobWorkspaceBuilder::GuardedAction()
{
    YT_ASSERT_THREAD_AFFINITY(JobThread);

    auto jobPhase = Context_.Job->GetPhase();

    switch (jobPhase) {
        case EJobPhase::WaitingForCleanup:
        case EJobPhase::Cleanup:
        case EJobPhase::Finished:
            YT_LOG_DEBUG(
                "Skip workspace building action (JobPhase: %v, ActionName: %v)",
                jobPhase,
                GetStepName<Step>());
            return VoidFuture;

        case EJobPhase::Created:
            YT_VERIFY(Context_.Job->GetState() == EJobState::Waiting);
            break;

        default:
            YT_VERIFY(Context_.Job->GetState() == EJobState::Running);
            break;
    }

    TForbidContextSwitchGuard contextSwitchGuard;

    YT_LOG_DEBUG(
        "Run guarded workspace building action (JobPhase: %v, ActionName: %v)",
        jobPhase,
        GetStepName<Step>());

    return (*this.*Step)();
}

template<TFuture<void>(TJobWorkspaceBuilder::*Step)()>
constexpr const char* TJobWorkspaceBuilder::GetStepName()
{
    if (Step == &TJobWorkspaceBuilder::DoPrepareRootVolume) {
        return "DoPrepareRootVolume";
    } else if (Step == &TJobWorkspaceBuilder::DoPrepareGpuCheckVolume) {
        return "DoPrepareGpuCheckVolume";
    } else if (Step == &TJobWorkspaceBuilder::DoPrepareSandboxDirectories) {
        return "DoPrepareSandboxDirectories";
    } else if (Step == &TJobWorkspaceBuilder::DoRunSetupCommand) {
        return "DoRunSetupCommand";
    } else if (Step == &TJobWorkspaceBuilder::DoRunCustomPreparations) {
        return "DoRunCustomPreparations";
    } else if (Step == &TJobWorkspaceBuilder::DoRunGpuCheckCommand) {
        return "DoRunGpuCheckCommand";
    }
}

template<TFuture<void>(TJobWorkspaceBuilder::*Method)()>
TCallback<TFuture<void>()> TJobWorkspaceBuilder::MakeStep()
{
    YT_ASSERT_THREAD_AFFINITY(JobThread);

    return BIND([this, this_ = MakeStrong(this)] {
        return GuardedAction<Method>();
    }).AsyncVia(Invoker_);
}

void TJobWorkspaceBuilder::ValidateJobPhase(EJobPhase expectedPhase) const
{
    YT_ASSERT_THREAD_AFFINITY(JobThread);

    auto jobPhase = Context_.Job->GetPhase();
    if (jobPhase != expectedPhase) {
        YT_LOG_DEBUG(
            "Unexpected job phase during workspace preparation (Actual: %v, Expected: %v)",
            jobPhase,
            expectedPhase);

        THROW_ERROR_EXCEPTION("Unexpected job phase")
            << TErrorAttribute("expected_phase", expectedPhase)
            << TErrorAttribute("actual_phase", jobPhase);
    }
}

void TJobWorkspaceBuilder::SetJobPhase(EJobPhase phase)
{
    YT_ASSERT_THREAD_AFFINITY(JobThread);

    UpdateBuilderPhase_.Fire(phase);
}

void TJobWorkspaceBuilder::UpdateArtifactStatistics(i64 compressedDataSize, bool cacheHit)
{
    YT_ASSERT_THREAD_AFFINITY(JobThread);

    UpdateArtifactStatistics_.Fire(compressedDataSize, cacheHit);
}

void TJobWorkspaceBuilder::MakeArtifactSymlinks()
{
    const auto& slot = Context_.Slot;

    YT_LOG_INFO(
        "Making artifact symlinks (ArtifactCount: %v)",
        std::size(Context_.Artifacts));

    for (const auto& artifact : Context_.Artifacts) {
        // Artifact is passed into the job via symlink.
        if (!artifact.BypassArtifactCache && !artifact.CopyFile) {
            YT_VERIFY(artifact.Chunk);

            YT_LOG_INFO(
                "Making symlink for artifact (FileName: %v, Executable: "
                "%v, SandboxKind: %v, CompressedDataSize: %v)",
                artifact.Name,
                artifact.Executable,
                artifact.SandboxKind,
                artifact.Key.GetCompressedDataSize());

            auto sandboxPath = slot->GetSandboxPath(artifact.SandboxKind);
            auto symlinkPath =
                CombinePaths(sandboxPath, artifact.Name);

            WaitFor(slot->MakeLink(
                Context_.Job->GetId(),
                artifact.Name,
                artifact.SandboxKind,
                artifact.Chunk->GetFileName(),
                symlinkPath,
                artifact.Executable))
                .ThrowOnError();

            YT_LOG_INFO(
                "Symlink for artifact is successfully made (FileName: %v, Executable: %v,"
                " SandboxKind: %v, CompressedDataSize: %v)",
                artifact.Name,
                artifact.Executable,
                artifact.SandboxKind,
                artifact.Key.GetCompressedDataSize());
        } else {
            YT_VERIFY(artifact.SandboxKind == ESandboxKind::User);
        }
    }

    YT_LOG_INFO("Artifact symlinks are made");
}

void TJobWorkspaceBuilder::PrepareArtifactBinds()
{
    const auto& slot = Context_.Slot;

    YT_LOG_INFO(
        "Setting permissions for artifacts (ArtifactCount: %v)",
        std::size(Context_.Artifacts));

    std::vector<TFuture<void>> ioOperationFutures;
    ioOperationFutures.reserve(Context_.Artifacts.size());

    for (const auto& artifact : Context_.Artifacts) {
        if (artifact.AccessedViaBind) {
            YT_VERIFY(artifact.Chunk);

            auto sandboxPath = slot->GetSandboxPath(artifact.SandboxKind);
            auto artifactPath = CombinePaths(sandboxPath, artifact.Name);

            YT_LOG_INFO(
                "Set permissions for artifact (FileName: %v, Executable: "
                "%v, SandboxKind: %v, CompressedDataSize: %v)",
                artifact.Name,
                artifact.Executable,
                artifact.SandboxKind,
                artifact.Key.GetCompressedDataSize());

            ioOperationFutures.push_back(slot->MakeSandboxBind(
                Context_.Job->GetId(),
                artifact.Name,
                artifact.SandboxKind,
                artifact.Chunk->GetFileName(),
                artifactPath,
                artifact.Executable));
        } else {
            YT_VERIFY(artifact.SandboxKind == ESandboxKind::User);
        }
    }

    WaitFor(AllSucceeded(ioOperationFutures))
        .ThrowOnError();
    YT_LOG_INFO("Permissions for artifacts set");
}

void TJobWorkspaceBuilder::SetNowTime(std::optional<TInstant>& timeField)
{
    timeField = TInstant::Now();
    UpdateTimePoints_.Fire(TimePoints_);
}

TFuture<TJobWorkspaceBuildingResult> TJobWorkspaceBuilder::Run()
{
    YT_ASSERT_THREAD_AFFINITY(JobThread);

    auto future = MakeStep<&TJobWorkspaceBuilder::DoPrepareRootVolume>()
        .Run()
        .Apply(MakeStep<&TJobWorkspaceBuilder::DoPrepareGpuCheckVolume>())
        .Apply(MakeStep<&TJobWorkspaceBuilder::DoPrepareSandboxDirectories>())
        .Apply(MakeStep<&TJobWorkspaceBuilder::DoRunSetupCommand>())
        .Apply(MakeStep<&TJobWorkspaceBuilder::DoRunCustomPreparations>())
        .Apply(MakeStep<&TJobWorkspaceBuilder::DoRunGpuCheckCommand>())
        .Apply(BIND([this, this_ = MakeStrong(this)] (const TError& result) -> TJobWorkspaceBuildingResult {
            YT_LOG_INFO(result, "Job workspace building finished");

            ResultHolder_.LastBuildError = result;
            return std::move(ResultHolder_);
        }).AsyncVia(Invoker_));

    future.Subscribe(BIND([this, this_ = MakeStrong(this)] (const TErrorOr<TJobWorkspaceBuildingResult>&) {
        // Drop reference to close race with check in TJob::Cleanup() on cancellation.
        Context_.Slot.Reset();
    }).Via(Invoker_));

    return future;
}

////////////////////////////////////////////////////////////////////////////////

class TSimpleJobWorkspaceBuilder
    : public TJobWorkspaceBuilder
{
public:
    TSimpleJobWorkspaceBuilder(
        IInvokerPtr invoker,
        TJobWorkspaceBuildingContext context,
        IJobDirectoryManagerPtr directoryManager)
        : TJobWorkspaceBuilder(
            std::move(invoker),
            std::move(context),
            std::move(directoryManager))
    { }

private:
    TRootFS MakeWritableRootFS()
    {
        YT_ASSERT_THREAD_AFFINITY(JobThread);

        YT_VERIFY(ResultHolder_.RootVolume);

        auto binds = Context_.Binds;

        for (const auto& bind : ResultHolder_.RootBinds) {
            binds.push_back(bind);
        }

        return TRootFS{
            .RootPath = ResultHolder_.RootVolume->GetPath(),
            .IsRootReadOnly = false,
            .Binds = std::move(binds),
        };
    }

    TFuture<void> DoPrepareRootVolume() override
    {
        YT_ASSERT_THREAD_AFFINITY(JobThread);

        YT_LOG_DEBUG("Root volume preparation is not supported in simple workspace");

        ValidateJobPhase(EJobPhase::DownloadingArtifacts);
        SetJobPhase(EJobPhase::PreparingRootVolume);

        if (!Context_.RootVolumeLayerArtifactKeys.empty()) {
            return MakeFuture(TError(
                NExecNode::EErrorCode::LayerUnpackingFailed,
                "Porto layers are not supported in simple job environment"));
        }

        if (Context_.DockerImage) {
            return MakeFuture(TError(
                NExecNode::EErrorCode::DockerImagePullingFailed,
                "External docker image is not supported in simple job environment"));
        }

        return VoidFuture;
    }

    TFuture<void> DoPrepareGpuCheckVolume() override
    {
        YT_ASSERT_THREAD_AFFINITY(JobThread);

        YT_LOG_DEBUG("GPU check volume preparation is not supported in simple workspace");

        ValidateJobPhase(EJobPhase::PreparingRootVolume);
        SetJobPhase(EJobPhase::PreparingGpuCheckVolume);

        return VoidFuture;
    }

    TFuture<void> DoPrepareSandboxDirectories() override
    {
        YT_ASSERT_THREAD_AFFINITY(JobThread);

        ValidateJobPhase(EJobPhase::PreparingGpuCheckVolume);
        SetJobPhase(EJobPhase::PreparingSandboxDirectories);

        YT_LOG_INFO("Started preparing sandbox directories");

        return Context_.Slot->PrepareSandboxDirectories(Context_.UserSandboxOptions)
            .Apply(BIND([this, this_ = MakeStrong(this)] (std::vector<TString> tmpfsPaths) mutable {
                ResultHolder_.TmpfsPaths = std::move(tmpfsPaths);

                MakeArtifactSymlinks();

                YT_LOG_INFO("Finished preparing sandbox directories");
            }).AsyncVia(Invoker_));
    }

    TFuture<void> DoRunSetupCommand() override
    {
        YT_ASSERT_THREAD_AFFINITY(JobThread);

        YT_LOG_DEBUG("Setup command is not supported in simple workspace");

        ValidateJobPhase(EJobPhase::PreparingSandboxDirectories);
        SetJobPhase(EJobPhase::RunningSetupCommands);

        return VoidFuture;
    }

    TFuture<void> DoRunCustomPreparations() override
    {
        YT_ASSERT_THREAD_AFFINITY(JobThread);

        YT_LOG_DEBUG("There are no custom preparations in simple workspace");

        ValidateJobPhase(EJobPhase::RunningSetupCommands);
        SetJobPhase(EJobPhase::RunningCustomPreparations);

        return VoidFuture;
    }

    TFuture<void> DoRunGpuCheckCommand() override
    {
        YT_ASSERT_THREAD_AFFINITY(JobThread);

        YT_LOG_DEBUG("GPU check is not supported in simple workspace");

        ValidateJobPhase(EJobPhase::RunningCustomPreparations);
        // NB: we intentionally not set running_gpu_check_command phase, since this phase is empty.

        return VoidFuture;
    }
};

////////////////////////////////////////////////////////////////////////////////

TJobWorkspaceBuilderPtr CreateSimpleJobWorkspaceBuilder(
    IInvokerPtr invoker,
    TJobWorkspaceBuildingContext context,
    IJobDirectoryManagerPtr directoryManager)
{
    return New<TSimpleJobWorkspaceBuilder>(
        std::move(invoker),
        std::move(context),
        std::move(directoryManager));
}

////////////////////////////////////////////////////////////////////////////////

#ifdef _linux_

class TPortoJobWorkspaceBuilder
    : public TJobWorkspaceBuilder
{
public:
    TPortoJobWorkspaceBuilder(
        IInvokerPtr invoker,
        TJobWorkspaceBuildingContext context,
        IJobDirectoryManagerPtr directoryManager,
        TGpuManagerPtr gpuManager)
        : TJobWorkspaceBuilder(
            std::move(invoker),
            std::move(context),
            std::move(directoryManager))
        , GpuManager_(std::move(gpuManager))
    { }

private:
    const TGpuManagerPtr GpuManager_;

    TFuture<void> DoPrepareRootVolume() override
    {
        YT_ASSERT_THREAD_AFFINITY(JobThread);

        ValidateJobPhase(EJobPhase::DownloadingArtifacts);
        SetJobPhase(EJobPhase::PreparingRootVolume);

        const auto& slot = Context_.Slot;
        const auto& layerArtifactKeys = Context_.RootVolumeLayerArtifactKeys;

        if (Context_.DockerImage && layerArtifactKeys.empty()) {
            return MakeFuture(TError(
                NExecNode::EErrorCode::DockerImagePullingFailed,
                "External docker image is not supported in Porto job environment"));
        }

        if (!layerArtifactKeys.empty()) {
            SetNowTime(TimePoints_.PrepareRootVolumeStartTime);

            YT_LOG_INFO("Preparing root volume (LayerCount: %v, HasVirtualSandbox: %v)",
                layerArtifactKeys.size(),
                Context_.UserSandboxOptions.VirtualSandboxData.has_value());

            for (const auto& layer : layerArtifactKeys) {
                i64 layerSize = layer.GetCompressedDataSize();
                UpdateArtifactStatistics(layerSize, slot->IsLayerCached(layer));
            }

            TVolumePreparationOptions options;
            options.JobId = Context_.Job->GetId();
            options.ArtifactDownloadOptions = Context_.ArtifactDownloadOptions;
            options.UserSandboxOptions = Context_.UserSandboxOptions;

            return slot->PrepareRootVolume(
                layerArtifactKeys,
                options)
                .Apply(BIND([this, this_ = MakeStrong(this)] (const TErrorOr<IVolumePtr>& volumeOrError) {
                    if (!volumeOrError.IsOK()) {
                        YT_LOG_WARNING(volumeOrError, "Failed to prepare root volume");

                        THROW_ERROR_EXCEPTION(NExecNode::EErrorCode::RootVolumePreparationFailed, "Failed to prepare root volume")
                            << volumeOrError;
                    }

                    YT_LOG_DEBUG("Root volume prepared");

                    ResultHolder_.RootVolume = volumeOrError.Value();

                    SetNowTime(TimePoints_.PrepareRootVolumeFinishTime);
                }));
        } else {
            YT_LOG_DEBUG("Root volume preparation is not needed");
            return VoidFuture;
        }
    }

    TFuture<void> DoPrepareGpuCheckVolume() override
    {
        YT_ASSERT_THREAD_AFFINITY(JobThread);

        ValidateJobPhase(EJobPhase::PreparingRootVolume);
        SetJobPhase(EJobPhase::PreparingGpuCheckVolume);

        const auto& slot = Context_.Slot;
        const auto& layerArtifactKeys = Context_.GpuCheckVolumeLayerArtifactKeys;

        if (!layerArtifactKeys.empty()) {
            SetNowTime(TimePoints_.PrepareGpuCheckVolumeStartTime);

            YT_LOG_INFO("Preparing GPU check volume (LayerCount: %v)",
                layerArtifactKeys.size());

            for (const auto& layer : layerArtifactKeys) {
                i64 layerSize = layer.GetCompressedDataSize();
                UpdateArtifactStatistics(layerSize, slot->IsLayerCached(layer));
            }

            TVolumePreparationOptions options;
            options.JobId = Context_.Job->GetId();
            options.ArtifactDownloadOptions = Context_.ArtifactDownloadOptions;

            return slot->PrepareGpuCheckVolume(
                layerArtifactKeys,
                options)
                .Apply(BIND([this, this_ = MakeStrong(this)] (const TErrorOr<IVolumePtr>& volumeOrError) {
                    if (!volumeOrError.IsOK()) {
                        YT_LOG_WARNING(volumeOrError, "Failed to prepare GPU check volume");

                        THROW_ERROR_EXCEPTION(NExecNode::EErrorCode::RootVolumePreparationFailed, "Failed to prepare GPU check volume")
                            << volumeOrError;
                    }

                    YT_LOG_DEBUG("GPU check volume prepared");

                    ResultHolder_.GpuCheckVolume = volumeOrError.Value();

                    SetNowTime(TimePoints_.PrepareGpuCheckVolumeFinishTime);
                }));
        } else {
            YT_LOG_DEBUG("GPU check volume preparation is not needed");
            return VoidFuture;
        }

        return VoidFuture;
    }

    TFuture<void> DoPrepareSandboxDirectories() override
    {
        YT_ASSERT_THREAD_AFFINITY(JobThread);

        ValidateJobPhase(EJobPhase::PreparingGpuCheckVolume);
        SetJobPhase(EJobPhase::PreparingSandboxDirectories);

        YT_LOG_INFO("Started preparing sandbox directories");

        // NB: If EnableRootVolumeDiskQuota is set and we have RootVolume, then we have already
        // applied a quota to root volume and should not set it again within sandbox preparation.
        bool ignoreQuota = Context_.UserSandboxOptions.EnableRootVolumeDiskQuota && ResultHolder_.RootVolume;

        return Context_.Slot->PrepareSandboxDirectories(Context_.UserSandboxOptions, ignoreQuota)
            .Apply(BIND([this, this_ = MakeStrong(this)] (std::vector<TString> tmpfsPaths) mutable {
                ResultHolder_.TmpfsPaths = std::move(tmpfsPaths);

                if (ResultHolder_.RootVolume) {
                    PrepareArtifactBinds();
                } else {
                    MakeArtifactSymlinks();
                }

                YT_LOG_INFO("Finished preparing sandbox directories");
            }).AsyncVia(Invoker_));
    }

    TRootFS MakeWritableRootFS()
    {
        YT_ASSERT_THREAD_AFFINITY(JobThread);

        YT_VERIFY(ResultHolder_.RootVolume);

        auto binds = Context_.Binds;

        for (const auto& bind : ResultHolder_.RootBinds) {
            binds.push_back(bind);
        }

        return TRootFS{
            .RootPath = ResultHolder_.RootVolume->GetPath(),
            .IsRootReadOnly = false,
            .Binds = std::move(binds),
        };
    }

    TRootFS MakeWritableGpuCheckRootFS()
    {
        YT_ASSERT_THREAD_AFFINITY(JobThread);

        YT_VERIFY(ResultHolder_.GpuCheckVolume);

        return TRootFS{
            .RootPath = ResultHolder_.GpuCheckVolume->GetPath(),
            .IsRootReadOnly = false,
            .Binds = {},
        };
    }

    TFuture<void> DoRunSetupCommand() override
    {
        YT_ASSERT_THREAD_AFFINITY(JobThread);

        ValidateJobPhase(EJobPhase::PreparingSandboxDirectories);
        SetJobPhase(EJobPhase::RunningSetupCommands);

        if (!ResultHolder_.RootVolume) {
            return VoidFuture;
        }

        const auto& slot = Context_.Slot;

        const auto& commands = Context_.SetupCommands;
        ResultHolder_.SetupCommandCount = commands.size();

        if (commands.empty()) {
            YT_LOG_DEBUG("No setup command is needed");

            return VoidFuture;
        }

        YT_LOG_INFO("Running setup commands");

        return slot->RunPreparationCommands(
            Context_.Job->GetId(),
            commands,
            MakeWritableRootFS(),
            Context_.CommandUser,
            /*devices*/ std::nullopt,
            /*hostName*/ std::nullopt,
            /*ipAddresses*/ {},
            /*tag*/ SetupCommandsTag)
            .AsVoid();
    }

    TFuture<void> DoRunCustomPreparations() override
    {
        YT_ASSERT_THREAD_AFFINITY(JobThread);

        YT_LOG_DEBUG("Running custom preparations");

        ValidateJobPhase(EJobPhase::RunningSetupCommands);
        SetJobPhase(EJobPhase::RunningCustomPreparations);

        if (!Context_.NeedGpu) {
            return VoidFuture;
        }

        auto networkPriority = Context_.Job->GetAllocation()->GetNetworkPriority();

        return BIND([this, this_ = MakeStrong(this), networkPriority] {
            GpuManager_->ApplyNetworkPriority(networkPriority);
        })
            .AsyncVia(Invoker_)
            .Run();
    }

    TFuture<void> DoRunGpuCheckCommand() override
    {
        YT_ASSERT_THREAD_AFFINITY(JobThread);

        ValidateJobPhase(EJobPhase::RunningCustomPreparations);

        if (Context_.GpuCheckOptions) {
            SetJobPhase(EJobPhase::RunningGpuCheckCommand);

            auto options = *Context_.GpuCheckOptions;
            // COMPAT(ignat): do not perform setup commands in case of running GPU check in user job volume.
            if (!ResultHolder_.GpuCheckVolume) {
                options.SetupCommands.clear();
            }

            auto context = TJobGpuCheckerContext{
                .Slot = Context_.Slot,
                .Job = Context_.Job,
                .RootFS = ResultHolder_.GpuCheckVolume
                    ? MakeWritableGpuCheckRootFS()
                    // COMPAT(ignat)
                    : MakeWritableRootFS(),
                .CommandUser = Context_.CommandUser,
                .Type = EGpuCheckType::Preliminary,
                .Options = options,
                .CurrentStartIndex = ResultHolder_.SetupCommandCount,
                // It is preliminary (not extra) GPU check.
                .TestExtraGpuCheckCommandFailure = false,
            };

            auto checker = New<TJobGpuChecker>(std::move(context), Logger);

            checker->SubscribeRunCheck(BIND_NO_PROPAGATE([this, this_ = MakeStrong(this)] {
                SetNowTime(TimePoints_.GpuCheckStartTime);
            }));

            checker->SubscribeFinishCheck(BIND_NO_PROPAGATE([this, this_ = MakeStrong(this)] {
                SetNowTime(TimePoints_.GpuCheckFinishTime);
            }));

            YT_LOG_INFO("Starting preliminary GPU check");

            return BIND(&TJobGpuChecker::RunGpuCheck, std::move(checker))
                .AsyncVia(Invoker_)
                .Run()
                .Apply(BIND([this, this_ = MakeStrong(this)] (const TError& result) {
                    ValidateJobPhase(EJobPhase::RunningGpuCheckCommand);
                    if (!result.IsOK()) {
                        auto checkError = TError(NExecNode::EErrorCode::GpuCheckCommandFailed, "Preliminary GPU check command failed")
                            << std::move(result);
                        THROW_ERROR checkError;
                    }

                    YT_LOG_INFO("Preliminary GPU check command finished");
                }).AsyncVia(Invoker_));
        } else {
            // NB: we intentionally not set running_gpu_check_command phase, since this phase is empty.
            YT_LOG_INFO("No preliminary GPU check is needed");

            return VoidFuture;
        }
    }
};

TJobWorkspaceBuilderPtr CreatePortoJobWorkspaceBuilder(
    IInvokerPtr invoker,
    TJobWorkspaceBuildingContext context,
    IJobDirectoryManagerPtr directoryManager,
    TGpuManagerPtr gpuManager)
{
    return New<TPortoJobWorkspaceBuilder>(
        std::move(invoker),
        std::move(context),
        std::move(directoryManager),
        std::move(gpuManager));
}

#endif

////////////////////////////////////////////////////////////////////////////////

class TCriJobWorkspaceBuilder
    : public TJobWorkspaceBuilder
{
public:
    TCriJobWorkspaceBuilder(
        IInvokerPtr invoker,
        TJobWorkspaceBuildingContext context,
        IJobDirectoryManagerPtr directoryManager,
        ICriImageCachePtr imageCache)
        : TJobWorkspaceBuilder(
            std::move(invoker),
            std::move(context),
            std::move(directoryManager))
        , ImageCache_(std::move(imageCache))
    { }

private:
    TFuture<void> DoPrepareRootVolume() override
    {
        YT_ASSERT_THREAD_AFFINITY(JobThread);

        ValidateJobPhase(EJobPhase::DownloadingArtifacts);
        SetJobPhase(EJobPhase::PreparingRootVolume);

        const auto& dockerImage = Context_.DockerImage;

        if (!dockerImage && !Context_.RootVolumeLayerArtifactKeys.empty()) {
            return MakeFuture(TError(
                NExecNode::EErrorCode::LayerUnpackingFailed,
                "Porto layers are not supported in CRI job environment"));
        }

        if (dockerImage) {
            SetNowTime(TimePoints_.PrepareRootVolumeStartTime);

            TCriImageDescriptor imageDescriptor {
                .Image = *dockerImage,
            };

            YT_LOG_INFO("Preparing root volume (Image: %v)", imageDescriptor);

            return ImageCache_->PullImage(
                imageDescriptor,
                Context_.DockerAuth)
                .Apply(BIND([
                    =,
                    this,
                    this_ = MakeStrong(this),
                    authenticated = bool(Context_.DockerAuth)
                ] (const TErrorOr<TCriImageCacheEntryPtr>& imageOrError) {
                    if (!imageOrError.IsOK()) {
                        YT_LOG_WARNING(imageOrError, "Failed to prepare root volume (Image: %v)", imageDescriptor);

                        THROW_ERROR_EXCEPTION(NExecNode::EErrorCode::DockerImagePullingFailed, "Failed to pull docker image")
                            << TErrorAttribute("docker_image", imageDescriptor.Image)
                            << TErrorAttribute("authenticated", authenticated)
                            << imageOrError;
                    }

                    const auto& cachedImage = imageOrError.Value()->Image();
                    YT_LOG_INFO("Root volume prepared (Image: %v)", cachedImage);

                    ResultHolder_.DockerImage = cachedImage.Image;
                    ResultHolder_.DockerImageId = cachedImage.Id;

                    SetNowTime(TimePoints_.PrepareRootVolumeFinishTime);
                }));
        } else {
            YT_LOG_DEBUG("Root volume preparation is not needed");
            return VoidFuture;
        }
    }

    TFuture<void> DoPrepareGpuCheckVolume() override
    {
        YT_ASSERT_THREAD_AFFINITY(JobThread);

        YT_LOG_DEBUG_IF(Context_.GpuCheckOptions, "Skip preparing GPU check volume since GPU check is not support in CRI environment");

        ValidateJobPhase(EJobPhase::PreparingRootVolume);
        SetJobPhase(EJobPhase::PreparingGpuCheckVolume);

        return VoidFuture;
    }

    TFuture<void> DoPrepareSandboxDirectories() override
    {
        YT_ASSERT_THREAD_AFFINITY(JobThread);

        ValidateJobPhase(EJobPhase::PreparingGpuCheckVolume);
        SetJobPhase(EJobPhase::PreparingSandboxDirectories);

        YT_LOG_INFO("Started preparing sandbox directories");

        return Context_.Slot->PrepareSandboxDirectories(Context_.UserSandboxOptions)
            .Apply(BIND([this, this_ = MakeStrong(this)] (std::vector<TString> tmpfsPaths) mutable {
                ResultHolder_.TmpfsPaths = std::move(tmpfsPaths);

                PrepareArtifactBinds();

                YT_LOG_INFO("Finished preparing sandbox directories");
            }).AsyncVia(Invoker_));
    }

    TFuture<void> DoRunSetupCommand() override
    {
        YT_ASSERT_THREAD_AFFINITY(JobThread);

        ValidateJobPhase(EJobPhase::PreparingSandboxDirectories);
        SetJobPhase(EJobPhase::RunningSetupCommands);

        if (Context_.SetupCommands.empty()) {
            YT_LOG_DEBUG("No setup command is needed");
            return VoidFuture;
        }

        YT_LOG_INFO("Running setup commands");

        TRootFS rootFS{
            .Binds = Context_.Binds,
        };

        rootFS.Binds.push_back(TBind{
            .SourcePath = Context_.Slot->GetSlotPath(),
            .TargetPath = "/slot",
            .ReadOnly = false,
        });

        ResultHolder_.SetupCommandCount = Context_.SetupCommands.size();
        return Context_.Slot->RunPreparationCommands(
            Context_.Job->GetId(),
            Context_.SetupCommands,
            rootFS,
            Context_.CommandUser,
            /*devices*/ std::nullopt,
            /*hostName*/ std::nullopt,
            /*ipAddresses*/ {},
            /*tag*/ SetupCommandsTag)
            .AsVoid();
    }

    TFuture<void> DoRunCustomPreparations() override
    {
        YT_ASSERT_THREAD_AFFINITY(JobThread);

        YT_LOG_DEBUG("There are no custom preparations in CRI workspace");

        ValidateJobPhase(EJobPhase::RunningSetupCommands);
        SetJobPhase(EJobPhase::RunningCustomPreparations);

        return VoidFuture;
    }

    TFuture<void> DoRunGpuCheckCommand() override
    {
        YT_ASSERT_THREAD_AFFINITY(JobThread);

        YT_LOG_DEBUG_IF(Context_.GpuCheckOptions, "GPU check is not supported in CRI workspace");

        ValidateJobPhase(EJobPhase::RunningCustomPreparations);
        // NB: we intentionally not set running_gpu_check_command phase, since this phase is empty.

        return VoidFuture;
    }

private:
    const ICriImageCachePtr ImageCache_;
};

////////////////////////////////////////////////////////////////////////////////

TJobWorkspaceBuilderPtr CreateCriJobWorkspaceBuilder(
    IInvokerPtr invoker,
    TJobWorkspaceBuildingContext context,
    IJobDirectoryManagerPtr directoryManager,
    ICriImageCachePtr imageCache)
{
    return New<TCriJobWorkspaceBuilder>(
        std::move(invoker),
        std::move(context),
        std::move(directoryManager),
        std::move(imageCache));
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NExecNode
