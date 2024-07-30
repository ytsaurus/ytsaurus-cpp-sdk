#include "disk_info_provider.h"

#include <yt/yt/library/containers/disk_manager/disk_manager_proxy.h>

#include <yt/yt/core/actions/future.h>
#include <yt/yt/core/actions/invoker_util.h>

#include <yt/yt/core/concurrency/public.h>

namespace NYT::NContainers {

////////////////////////////////////////////////////////////////////////////////

TDiskInfoProvider::TDiskInfoProvider(
    IDiskManagerProxyPtr diskManagerProxy,
    TDiskInfoProviderConfigPtr config)
    : DiskManagerProxy_(std::move(diskManagerProxy))
    , Config_(std::move(config))
{ }

const std::vector<TString>& TDiskInfoProvider::GetConfigDiskIds() const
{
    return Config_->DiskIds;
}

TFuture<void> TDiskInfoProvider::UpdateDiskCache()
{
    return DiskManagerProxy_->UpdateDiskCache();
}

TFuture<std::vector<TDiskInfo>> TDiskInfoProvider::GetYTDiskInfos()
{
    auto diskInfosFuture = DiskManagerProxy_->GetDisks();
    auto ytDiskPathsFuture = DiskManagerProxy_->GetYtDiskDevicePaths();
    auto diskYtPrefix = Config_->YtDiskPrefix;

    // Merge two futures and filter disks placed in /yt.
    return diskInfosFuture.Apply(BIND([=] (const std::vector<TDiskInfo>& diskInfos) {
        return ytDiskPathsFuture.Apply(BIND([=] (const THashSet<TString>& diskPaths) {
            std::vector<TDiskInfo> disks;

            for (const auto& diskInfo : diskInfos) {
                auto isYtDisk = false;
                for (const auto& path : diskPaths) {
                    if (path.StartsWith(diskInfo.DevicePath)) {
                        isYtDisk = true;
                        break;
                    }
                }

                for (const auto& path : diskInfo.PartitionFsLabels) {
                    if (path.StartsWith(diskYtPrefix)) {
                        isYtDisk = true;
                        break;
                    }
                }

                if (isYtDisk) {
                    disks.push_back(diskInfo);
                }
            }

            return disks;
        }));
    }));
}

TFuture<void> TDiskInfoProvider::RecoverDisk(const TString& diskId)
{
    return DiskManagerProxy_->RecoverDiskById(diskId, ERecoverPolicy::RecoverAuto);
}

TFuture<void> TDiskInfoProvider::FailDisk(
    const TString& diskId,
    const TString& reason)
{
    return DiskManagerProxy_->FailDiskById(diskId, reason);
}

TFuture<bool> TDiskInfoProvider::GetHotSwapEnabledFuture()
{
    return DiskManagerProxy_->GetHotSwapEnabledFuture();
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NContainers
