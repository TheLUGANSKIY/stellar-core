#pragma once

#include "transactions/OperationFrame.h"

namespace stellar
{
class ManageDebitOpFrame : public OperationFrame
{
	ManageDebitResult&
	innerResult()
	{
		return mResult.tr().manageDebitResult();
	}
	ManageDebitOp const& mManageDebit;

public:
	ManageDebitOpFrame(Operation const& op, OperationResult& res,
					   TransactionFrame& parentTx);

	bool doApply(Application& app, LedgerDelta& delta,
		LedgerManager& ledgerManager) override;
	bool doCheckValid(Application& app) override;

	static ManageDebitResultCode
	getInnerCode(OperationResult const& res)
	{
		return res.tr().manageDebitResult().code();
	}
};
}