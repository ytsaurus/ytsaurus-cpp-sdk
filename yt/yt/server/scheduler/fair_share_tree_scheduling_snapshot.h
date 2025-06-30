#pragma once

#include "private.h"
#include "fair_share_tree_allocation_scheduler_structs.h"
#include "fair_share_tree_allocation_scheduler_operation_shared_state.h"

namespace NYT::NScheduler {

////////////////////////////////////////////////////////////////////////////////

constexpr int EmptySchedulingTagFilterIndex = -1;

////////////////////////////////////////////////////////////////////////////////

struct TStaticAttributes
{
    int SchedulingIndex = UndefinedSchedulingIndex;
    int SchedulingTagFilterIndex = EmptySchedulingTagFilterIndex;
    bool EffectiveAggressivePreemptionAllowed = true;
    bool EffectivePrioritySchedulingSegmentModuleAssignmentEnabled = false;
    // Used for checking if operation is hung.
    bool IsAliveAtUpdate = false;

    // Only for operations.
    EOperationSchedulingPriority SchedulingPriority = EOperationSchedulingPriority::Medium;
    TFairShareTreeAllocationSchedulerOperationStatePtr OperationState;
    TFairShareTreeAllocationSchedulerOperationSharedStatePtr OperationSharedState;
    bool AreRegularAllocationsOnSsdNodesAllowed = true;
};

////////////////////////////////////////////////////////////////////////////////

class TStaticAttributesList final
    : public std::vector<TStaticAttributes>
{
public:
    TStaticAttributes& AttributesOf(const TSchedulerElement* element);
    const TStaticAttributes& AttributesOf(const TSchedulerElement* element) const;
};

////////////////////////////////////////////////////////////////////////////////

class TFairShareTreeSchedulingSnapshot
    : public TRefCounted
{
public:
    DEFINE_BYREF_RO_PROPERTY(TStaticAttributesList, StaticAttributesList);
    DEFINE_BYREF_RO_PROPERTY(TOperationElementsBySchedulingPriority, SchedulableOperationsPerPriority);
    DEFINE_BYREF_RO_PROPERTY(THashSet<int>, SsdPriorityPreemptionMedia);
    DEFINE_BYREF_RO_PROPERTY(TCachedAllocationPreemptionStatuses, CachedAllocationPreemptionStatuses);
    DEFINE_BYREF_RO_PROPERTY(std::vector<TSchedulingTagFilter>, KnownSchedulingTagFilters);
    DEFINE_BYREF_RO_PROPERTY(TOperationCountsByPreemptionPriorityParameters, OperationCountsByPreemptionPriorityParameters);

public:
    TFairShareTreeSchedulingSnapshot(
        TStaticAttributesList staticAttributesList,
        TOperationElementsBySchedulingPriority schedulableOperationsPerPriority,
        THashSet<int> ssdPriorityPreemptionMedia,
        TCachedAllocationPreemptionStatuses cachedAllocationPreemptionStatuses,
        std::vector<TSchedulingTagFilter> knownSchedulingTagFilters,
        TOperationCountsByPreemptionPriorityParameters operationCountsByPreemptionPriorityParameters,
        TFairShareTreeAllocationSchedulerOperationStateMap operationIdToState,
        TFairShareTreeAllocationSchedulerSharedOperationStateMap operationIdToSharedState);

    const TFairShareTreeAllocationSchedulerOperationStatePtr& GetOperationState(const TSchedulerOperationElement* element) const;
    const TFairShareTreeAllocationSchedulerOperationSharedStatePtr& GetOperationSharedState(const TSchedulerOperationElement* element) const;

    //! Faster versions of |GetOperationState| and |GetOperationSharedState| which do not do an extra hashmap lookup and rely on tree indices instead.
    const TFairShareTreeAllocationSchedulerOperationStatePtr& GetEnabledOperationState(const TSchedulerOperationElement* element) const;
    const TFairShareTreeAllocationSchedulerOperationSharedStatePtr& GetEnabledOperationSharedState(const TSchedulerOperationElement* element) const;

    //! NB(eshcherbin): This is the only part of the snapshot, that isn't constant.
    // It's just much more convenient to store dynamic attributes list snapshot together with the tree snapshot.
    TDynamicAttributesListSnapshotPtr GetDynamicAttributesListSnapshot() const;

private:
    // NB(eshcherbin): Enabled operations' states are also stored in static attributes to eliminate a hashmap lookup during scheduling.
    TFairShareTreeAllocationSchedulerOperationStateMap OperationIdToState_;
    TFairShareTreeAllocationSchedulerSharedOperationStateMap OperationIdToSharedState_;
    TAtomicIntrusivePtr<TDynamicAttributesListSnapshot> DynamicAttributesListSnapshot_;

    void SetDynamicAttributesListSnapshot(const TDynamicAttributesListSnapshotPtr& dynamicAttributesListSnapshotPtr);
    void ResetDynamicAttributesListSnapshot();

    friend class TFairShareTreeAllocationScheduler;
};

DEFINE_REFCOUNTED_TYPE(TFairShareTreeSchedulingSnapshot)

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NScheduler
