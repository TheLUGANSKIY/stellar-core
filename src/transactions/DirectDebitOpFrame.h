#pragma once

#include "transactions/OperationFrame.h"

namespace stellar
{
class DirectDebitOpFrame : public OperationFrame
{
	DirectDebitResult&
	innerResult()
	{
		return mResult.tr().directDebitResult();
	}
	DirectDebitOp const& mDirectDebit;

public:
	DirectDebitOpFrame(Operation const& op, OperationResult& res,
					   TransactionFrame& parentTx);

	static DirectDebitResultCode getFromPayment(medida::MetricsRegistry& metrics, 
												PaymentResultCode paymentResultCode);

	bool doApply(Application& app, LedgerDelta& delta,
		LedgerManager& ledgerManager) override;
	bool doCheckValid(Application& app) override;

	static DirectDebitResultCode
	getInnerCode(OperationResult const& res)
	{
		return res.tr().directDebitResult().code();
	}
};
}