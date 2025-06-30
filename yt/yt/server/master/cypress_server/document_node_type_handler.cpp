#include "document_node_type_handler.h"
#include "document_node.h"
#include "document_node_proxy.h"

namespace NYT::NCypressServer {

using namespace NYTree;
using namespace NYson;
using namespace NObjectClient;
using namespace NObjectServer;
using namespace NCellMaster;
using namespace NTransactionServer;
using namespace NSecurityServer;

////////////////////////////////////////////////////////////////////////////////

class TDocumentNodeTypeHandler
    : public TCypressNodeTypeHandlerBase<TDocumentNode>
{
private:
    using TBase = TCypressNodeTypeHandlerBase<TDocumentNode>;

public:
    using TBase::TBase;

    NObjectClient::EObjectType GetObjectType() const override
    {
        return EObjectType::Document;
    }

    NYTree::ENodeType GetNodeType() const override
    {
        return ENodeType::Entity;
    }

private:
    ICypressNodeProxyPtr DoGetProxy(
        TDocumentNode* trunkNode,
        TTransaction* transaction) override
    {
        return CreateDocumentNodeProxy(
            GetBootstrap(),
            &Metadata_,
            transaction,
            trunkNode);
    }

    void DoBranch(
        const TDocumentNode* originatingNode,
        TDocumentNode* branchedNode,
        const TLockRequest& lockRequest) override
    {
        TBase::DoBranch(originatingNode, branchedNode, lockRequest);

        branchedNode->SetValue(CloneNode(originatingNode->GetValue()));
    }

    void DoMerge(
        TDocumentNode* originatingNode,
        TDocumentNode* branchedNode) override
    {
        TBase::DoMerge(originatingNode, branchedNode);

        originatingNode->SetValue(branchedNode->GetValue());
    }

    void DoClone(
        TDocumentNode* sourceNode,
        TDocumentNode* clonedTrunkNode,
        IAttributeDictionary* inheritedAttributes,
        ICypressNodeFactory* factory,
        ENodeCloneMode mode,
        TAccount* account) override
    {
        TBase::DoClone(sourceNode, clonedTrunkNode, inheritedAttributes, factory, mode, account);

        clonedTrunkNode->SetValue(CloneNode(sourceNode->GetValue()));
    }

    bool HasBranchedChangesImpl(
        TDocumentNode* originatingNode,
        TDocumentNode* branchedNode) override
    {
        if (TBase::HasBranchedChangesImpl(originatingNode, branchedNode)) {
            return true;
        }

        return !AreNodesEqual(branchedNode->GetValue(), originatingNode->GetValue());
    }

    void DoSerializeNode(
        TDocumentNode* node,
        TSerializeNodeContext* context) override
    {
        TBase::DoSerializeNode(node, context);

        using NYT::Save;
        Save(*context, ConvertToYsonString(node->GetValue()));
    }

    void DoMaterializeNode(
        TDocumentNode* trunkNode,
        TMaterializeNodeContext* context) override
    {
        TBase::DoMaterializeNode(trunkNode, context);

        using NYT::Load;
        trunkNode->SetValue(ConvertToNode(Load<TYsonString>(*context)));
    }
};

////////////////////////////////////////////////////////////////////////////////

INodeTypeHandlerPtr CreateDocumentNodeTypeHandler(TBootstrap* bootstrap)
{
    return New<TDocumentNodeTypeHandler>(bootstrap);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NCypressServer
