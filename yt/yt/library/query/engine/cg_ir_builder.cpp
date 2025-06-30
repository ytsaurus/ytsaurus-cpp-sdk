#include "cg_ir_builder.h"
#include "cg_helpers.h"

#include <llvm/IR/Intrinsics.h>
#include <llvm/IR/Module.h>

#include <yt/yt/client/table_client/llvm_types.h>

#include <yt/yt/library/codegen/llvm_migrate_helpers.h>
#include <yt/yt/library/codegen/type_builder.h>

#include <library/cpp/yt/assert/assert.h>

namespace NYT::NQueryClient {

using llvm::Function;
using llvm::BasicBlock;
using llvm::Value;
using llvm::Type;
using llvm::Twine;
using llvm::Module;

using NCodegen::TTypeBuilder;

////////////////////////////////////////////////////////////////////////////////

static const unsigned int MaxClosureSize = 32;

////////////////////////////////////////////////////////////////////////////////

TCGIRBuilder::TCGIRBuilder(
    llvm::Function* function,
    TCGIRBuilder* parent,
    Value* closurePtr)
    : TBase(BasicBlock::Create(function->getContext(), "entry", function))
    , Parent_(parent)
    , ClosurePtr_(closurePtr)
{
    for (auto it = function->arg_begin(); it != function->arg_end(); ++it) {
        getInserter().ValuesInContext.insert(ConvertToPointer(it));
    }
    EntryBlock_ = GetInsertBlock();
}

TCGIRBuilder::TCGIRBuilder(
    Function* function)
    : TCGIRBuilder(function, nullptr, nullptr)
{ }

TCGIRBuilder::~TCGIRBuilder()
{ }

Value* TCGIRBuilder::ViaClosure(Value* value, Twine name)
{
    // If |value| belongs to the current context, then we can use it directly.
    if (getInserter().ValuesInContext.count(value) > 0) {
        return value;
    }

    auto valueName = value->getName();
    Twine resultingName(name.isTriviallyEmpty() ? valueName : name);

    // Otherwise, capture |value| in the parent context.
    YT_VERIFY(Parent_);
    YT_VERIFY(ClosurePtr_);

    Value* valueInParent = Parent_->ViaClosure(value, resultingName);

    // Check if we have already captured this value.
    auto it = Mapping_.find(valueInParent);

    if (it != Mapping_.end()) {
        return it->second.first;
    } else {
        auto indexInClosure = Mapping_.size();
        YT_VERIFY(indexInClosure < MaxClosureSize);

        InsertPoint currentIP = saveIP();
        SetInsertPoint(EntryBlock_, EntryBlock_->begin());

        Types_.push_back(value->getType());
        Type* closureType = llvm::StructType::get(Parent_->getContext(), Types_);

        // Load the value to the current context through the closure.
        Value* castedClosure = CreatePointerCast(
            ClosurePtr_,
            llvm::PointerType::getUnqual(closureType),
            "castedClosure");
        Value* elementPtr = CreateStructGEP(
            closureType,
            castedClosure,
            indexInClosure,
            resultingName + ".inParentPtr");
        Value* result = CreateLoad(
            value->getType(),
            elementPtr,
            resultingName);
        restoreIP(currentIP);

        Mapping_[valueInParent] = std::pair(result, indexInClosure);

        return result;
    }
}

Value* TCGIRBuilder::GetClosure()
{
    // Save all values into the closure in the parent context.

    Type* closureType = llvm::StructType::get(Parent_->getContext(), Types_);

    Value* closure = llvm::UndefValue::get(closureType);

    for (auto& value : Mapping_) {
        Value* valueInParent = value.first;
        int indexInClosure = value.second.second;

        closure = Parent_->CreateInsertValue(
            closure,
            valueInParent,
            indexInClosure);
    }

    Value* closurePtr = Parent_->CreateAlloca(
        closureType,
        nullptr,
        "closure");

    Parent_->CreateStore(
        closure,
        closurePtr);

    return Parent_->CreatePointerCast(
        closurePtr,
        TTypeBuilder<void**>::Get(getContext()),
        "uncastedClosure");
}

BasicBlock* TCGIRBuilder::CreateBBHere(const Twine& name)
{
    return BasicBlock::Create(getContext(), name, GetInsertBlock()->getParent());
}

Value* TCGIRBuilder::CreateStackSave(const Twine& name)
{
    return TBase::CreateStackSave(name);
}

void TCGIRBuilder::CreateStackRestore(Value* ptr)
{
    TBase::CreateStackRestore(ptr);
}

Type* TCGIRBuilder::getSizeType() const
{
    return TTypeBuilder<size_t>::Get(getContext());
}

llvm::AllocaInst* TCGIRBuilder::CreateAlignedAlloca(
    Type *type,
    unsigned align,
    Value* arraySize,
    const llvm::Twine& name)
{
#if LLVM_VERSION_GE(5, 0)
    const llvm::DataLayout &DL = BB->getParent()->getParent()->getDataLayout();
#if LLVM_VERSION_GE(11, 0)
    return Insert(new llvm::AllocaInst(type, DL.getAllocaAddrSpace(), arraySize, llvm::Align(align), name));
#else
    return Insert(new llvm::AllocaInst(type, DL.getAllocaAddrSpace(), arraySize, align), name);
#endif
#else
    return Insert(new llvm::AllocaInst(type, arraySize, align), name);
#endif
}

llvm::Value* TCGIRBuilder::CreateOr(llvm::Value* lhs, llvm::Value* rhs, const llvm::Twine& name)
{
    if (llvm::Constant* lc = llvm::dyn_cast<llvm::Constant>(lhs)) {
        if (lc->isNullValue()) {
            return rhs;
        }

        if (lc->isAllOnesValue()) {
            return lhs;
        }
    }

    if (llvm::Constant *rc = llvm::dyn_cast<llvm::Constant>(rhs)) {
        if (rc->isNullValue()) {
            return lhs;
        }

        if (rc->isAllOnesValue()) {
            return rhs;
        }
    }
    return TBase::CreateOr(lhs, rhs, name);
}

llvm::Value* TCGIRBuilder::CreateAnd(llvm::Value* lhs, llvm::Value* rhs, const llvm::Twine& name)
{
    if (llvm::Constant* lc = llvm::dyn_cast<llvm::Constant>(lhs)) {
        if (lc->isNullValue()) {
            return lhs;
        }

        if (lc->isAllOnesValue()) {
            return rhs;
        }
    }

    if (llvm::Constant *rc = llvm::dyn_cast<llvm::Constant>(rhs)) {
        if (rc->isNullValue()) {
            return rhs;
        }

        if (rc->isAllOnesValue()) {
            return lhs;
        }
    }
    return TBase::CreateAnd(lhs, rhs, name);
}

llvm::Value* TCGIRBuilder::CreateSelect(
    llvm::Value* condition,
    llvm::Value* trueValue,
    llvm::Value* falseValue,
    const llvm::Twine& name)
{
    if (llvm::Constant* constantCondition = llvm::dyn_cast<llvm::Constant>(condition)) {
        if (constantCondition->isNullValue()) {
            return falseValue;
        }

        if (constantCondition->isAllOnesValue()) {
            return trueValue;
        }
    }

    return TBase::CreateSelect(condition, trueValue, falseValue, name);
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYT::NQueryClient
