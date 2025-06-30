#include "file_node_type_handler.h"
#include "file_node.h"
#include "file_node_proxy.h"

#include <yt/yt/server/master/cell_master/bootstrap.h>
#include <yt/yt/server/master/cell_master/config.h>

#include <yt/yt/server/master/chunk_server/chunk_owner_type_handler.h>

#include <yt/yt/server/master/cypress_server/config.h>

#include <yt/yt/ytlib/chunk_client/helpers.h>

#include <yt/yt/core/crypto/crypto.h>

namespace NYT::NFileServer {

using namespace NCrypto;
using namespace NCellMaster;
using namespace NCypressServer;
using namespace NSecurityServer;
using namespace NChunkClient;
using namespace NChunkServer;
using namespace NTransactionServer;
using namespace NObjectServer;
using namespace NYTree;

////////////////////////////////////////////////////////////////////////////////

class TFileNodeTypeHandler
    : public TChunkOwnerTypeHandler<TFileNode>
{
private:
    using TBase = TChunkOwnerTypeHandler<TFileNode>;

public:
    explicit TFileNodeTypeHandler(TBootstrap* bootstrap)
        : TBase(bootstrap)
    {
        // NB: Due to virtual inheritance bootstrap has to be explicitly initialized.
        SetBootstrap(bootstrap);
    }

    EObjectType GetObjectType() const override
    {
        return EObjectType::File;
    }

protected:
    ICypressNodeProxyPtr DoGetProxy(
        TFileNode* trunkNode,
        TTransaction* transaction) override
    {
        return CreateFileNodeProxy(
            GetBootstrap(),
            &Metadata_,
            transaction,
            trunkNode);
    }

    std::unique_ptr<TFileNode> DoCreate(
        TVersionedNodeId id,
        const TCreateNodeContext& context) override
    {
        const auto& config = GetBootstrap()->GetDynamicConfig()->CypressManager;
        auto combinedAttributes = OverlayAttributeDictionaries(context.ExplicitAttributes, context.InheritedAttributes);
        auto replicationFactor = combinedAttributes->GetAndRemove("replication_factor", config->DefaultFileReplicationFactor);
        auto compressionCodec = combinedAttributes->GetAndRemove<NCompression::ECodec>("compression_codec", NCompression::ECodec::None);
        auto erasureCodec = combinedAttributes->GetAndRemove<NErasure::ECodec>("erasure_codec", NErasure::ECodec::None);
        auto enableStripedErasure = combinedAttributes->GetAndRemove<bool>("use_striped_erasure", false);

        ValidateReplicationFactor(replicationFactor);

        auto nodeHolder = DoCreateImpl(
            id,
            context,
            replicationFactor,
            compressionCodec,
            erasureCodec,
            enableStripedErasure);

        auto* node = nodeHolder.get();
        node->SetMD5Hasher(TMD5Hasher());
        return nodeHolder;
    }

    void DoBranch(
        const TFileNode* originatingNode,
        TFileNode* branchedNode,
        const TLockRequest& lockRequest) override
    {
        TBase::DoBranch(originatingNode, branchedNode, lockRequest);

        branchedNode->SetMD5Hasher(originatingNode->GetMD5Hasher());
    }

    void DoMerge(
        TFileNode* originatingNode,
        TFileNode* branchedNode) override
    {
        TBase::DoMerge(originatingNode, branchedNode);

        originatingNode->SetMD5Hasher(branchedNode->GetMD5Hasher());
    }

    void DoClone(
        TFileNode* sourceNode,
        TFileNode* clonedTrunkNode,
        IAttributeDictionary* inheritedAttributes,
        ICypressNodeFactory* factory,
        ENodeCloneMode mode,
        TAccount* account) override
    {
        TBase::DoClone(sourceNode, clonedTrunkNode, inheritedAttributes, factory, mode, account);

        clonedTrunkNode->SetMD5Hasher(sourceNode->GetMD5Hasher());
    }

    void DoSerializeNode(
        TFileNode* node,
        TSerializeNodeContext* context) override
    {
        TBase::DoSerializeNode(node, context);

        using NYT::Save;
        Save(*context, node->GetMD5Hasher());
    }

    void DoMaterializeNode(
        TFileNode* node,
        TMaterializeNodeContext* context) override
    {
        TBase::DoMaterializeNode(node, context);

        using NYT::Load;
        node->SetMD5Hasher(Load<std::optional<TMD5Hasher>>(*context));
    }
};

INodeTypeHandlerPtr CreateFileTypeHandler(TBootstrap* bootstrap)
{
    return New<TFileNodeTypeHandler>(bootstrap);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NFileServer

