#include "node_statistics.h"

#include <yt/yt_proto/yt/client/node_tracker_client/proto/node.pb.h>

namespace NYT::NNodeTrackerClient::NProto {

////////////////////////////////////////////////////////////////////////////////

void FormatValue(TStringBuilderBase* builder, const TNodeStatistics& statistics, TStringBuf /*spec*/)
{
    builder->AppendFormat(
        "Space: %v/%v, TotalStoredChunks: %v, TotalCachedChunks: %v, UserSessions: %v, "
        "ReplicationSessions: %v, RepairSessions: %v, TabletSlots: %v/%v",
        statistics.total_used_space(),
        statistics.total_available_space() + statistics.total_used_space(),
        statistics.total_stored_chunk_count(),
        statistics.total_cached_chunk_count(),
        statistics.total_user_session_count(),
        statistics.total_replication_session_count(),
        statistics.total_repair_session_count(),
        statistics.used_tablet_slots(),
        statistics.available_tablet_slots() + statistics.used_tablet_slots());
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NNodeTrackerClient::NProto
