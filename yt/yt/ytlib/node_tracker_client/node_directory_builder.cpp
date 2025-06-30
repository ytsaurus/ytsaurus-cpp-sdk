#include "node_directory_builder.h"

#include <yt/yt/client/node_tracker_client/node_directory.h>

#include <yt/yt_proto/yt/client/node_tracker_client/proto/node_directory.pb.h>

#include <yt/yt/core/misc/protobuf_helpers.h>

namespace NYT::NNodeTrackerClient {

using namespace NChunkClient;

using NYT::ToProto;

////////////////////////////////////////////////////////////////////////////////

TNodeDirectoryBuilder::TNodeDirectoryBuilder(
    TNodeDirectoryPtr directory,
    NProto::TNodeDirectory* protoDirectory)
    : Directory_(directory)
    , ProtoDirectory_(protoDirectory)
{ }

void TNodeDirectoryBuilder::Add(TChunkReplica replica)
{
    auto nodeId = replica.GetNodeId();
    if (!ListedNodeIds_.insert(nodeId).second) {
        return;
    }

    const auto& descriptor = Directory_->GetDescriptor(replica);
    auto* item = ProtoDirectory_->add_items();
    item->set_node_id(ToProto(nodeId));
    ToProto(item->mutable_node_descriptor(), descriptor);
}

void TNodeDirectoryBuilder::Add(const TChunkReplicaList& replicas)
{
    for (auto replica : replicas) {
        Add(replica);
    }
}

void TNodeDirectoryBuilder::Add(const TChunkReplicaWithMediumList& replicas)
{
    for (auto replica : replicas) {
        Add(replica);
    }
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NNodeTrackerClient

