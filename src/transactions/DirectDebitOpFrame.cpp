#include "DirectDebitOpFrame.h"
#include "database/Database.h"
#include "ledger/LedgerManager.h"
#include "ledger/DebitFrame.h"
#include "ledger/TrustFrame.h"
#include "main/Application.h"
#include "medida/meter.h"
#include "medida/metrics_registry.h"
#include "transactions/PaymentOpFrame.h"

namespace stellar
{
DirectDebitOpFrame::DirectDebitOpFrame(Operation const& op,
									   OperationResult& res,
									   TransactionFrame& parentTx)
	: OperationFrame(op, res, parentTx)
	, mDirectDebit(mOperation.body.directDebitOp())
{
}

DirectDebitResultCode
DirectDebitOpFrame::getFromPayment(medida::MetricsRegistry& metrics,
								   PaymentResultCode paymentResultCode)
{
	switch (paymentResultCode)
	{
	case PAYMENT_MALFORMED:
		metrics
			.NewMeter({ "op-direct-debit", "failure", "payment-malformed" },
						"operation")
			.Mark();
		return DIRECT_DEBIT_MALFORMED;
	case PAYMENT_UNDERFUNDED:
		metrics
			.NewMeter({ "op-direct-debit", "failure", "owner-underfunded" },
						"operation")
			.Mark();
		return DIRECT_DEBIT_OWNER_UNDERFUNDED;
	case PAYMENT_SRC_NO_TRUST:
		metrics
			.NewMeter({ "op-direct-debit", "failure", "owner-no-trust" },
						"operation")
			.Mark();
		return DIRECT_DEBIT_OWNER_NO_TRUST;
	case PAYMENT_SRC_NOT_AUTHORIZED:
		metrics
			.NewMeter({ "op-direct-debit", "failure", "owner-not-authorized" },
						"operation")
			.Mark();
		return DIRECT_DEBIT_OWNER_NOT_AUTHORIZED;
	case PAYMENT_NO_DESTINATION:
		metrics
			.NewMeter({ "op-direct-debit", "failure", "no-destination" },
						"operation")
			.Mark();
		return DIRECT_DEBIT_NO_DESTINATION;
	case PAYMENT_NO_TRUST:
		metrics
			.NewMeter({ "op-direct-debit", "failure", "destination-no-trust" },
						"operation")
			.Mark();
		return DIRECT_DEBIT_DESTINATION_NO_TRUST;
	case PAYMENT_NOT_AUTHORIZED:
		metrics
			.NewMeter({ "op-direct-debit", "failure", "destination-not-authorized" },
						"operation")
			.Mark();
		return DIRECT_DEBIT_DESTINATION_NOT_AUTHORIZED;
	case PAYMENT_LINE_FULL:
		metrics
			.NewMeter({ "op-direct-debit", "failure", "line-full" },
						"operation")
			.Mark();
		return DIRECT_DEBIT_LINE_FULL;
	case PAYMENT_NO_ISSUER:
		metrics
			.NewMeter({ "op-direct-debit", "failure", "no-issuer" },
						"operation")
			.Mark();
		return DIRECT_DEBIT_NO_ISSUER;
	default:
		throw std::runtime_error("Unexpected error code from payment");
	}
}

bool
DirectDebitOpFrame::doApply(Application& app, LedgerDelta& delta,
							LedgerManager& ledgerManager)
{
	Database& db = ledgerManager.getDatabase();

	auto& debit = DebitFrame::loadDebit(mDirectDebit.owner, getSourceID(),
										mDirectDebit.payWithDebit.asset, db);
	if (!debit)
	{
		app.getMetrics()
			.NewMeter({ "op-direct-debit", "failure", "no-debit" },
						"operation")
			.Mark();
		innerResult().code(DIRECT_DEBIT_NO_DEBIT);
		return false;
	}

	//build a paymentOp
	Operation op;
	op.sourceAccount.activate() = mDirectDebit.owner;
	op.body.type(PAYMENT);
	PaymentOp& pOp = op.body.paymentOp();
	pOp.destination = mDirectDebit.payWithDebit.destination;
	pOp.asset = mDirectDebit.payWithDebit.asset;
	pOp.amount = mDirectDebit.payWithDebit.amount;

	auto& owner = AccountFrame::loadAccount(delta,
											mDirectDebit.owner,
											db);
	if (!owner) 
	{
		throw std::runtime_error("An error occurred while loading owner");
	}

	OperationResult opRes;
	opRes.code(opINNER);
	opRes.tr().type(PAYMENT);
	PaymentOpFrame payment(op, opRes, mParentTx);
	payment.setSourceAccountPtr(owner);

	if (!payment.doCheckValid(app) ||
		!payment.doApply(app, delta, ledgerManager))
	{
		if (payment.getResultCode() != opINNER)
		{
			throw std::runtime_error("Unexpected error code from payment");
		}
		DirectDebitResultCode res = getFromPayment(app.getMetrics(), PaymentOpFrame::getInnerCode(payment.getResult()));
		innerResult().code(res);
		return false;
	}

	assert(PaymentOpFrame::getInnerCode(payment.getResult()) ==
		   PAYMENT_SUCCESS);

	app.getMetrics()
		.NewMeter({ "op-direct-debit", "success", "apply" }, "operation")
		.Mark();
	innerResult().code(DIRECT_DEBIT_SUCCESS);

	return true;
}

bool
DirectDebitOpFrame::doCheckValid(Application& app)
{
	if (!isAssetValid(mDirectDebit.payWithDebit.asset))
	{
		app.getMetrics()
			.NewMeter({ "op-direct-debit", "invalid", "malformed-invalid-asset" },
						"operation")
			.Mark();
		innerResult().code(DIRECT_DEBIT_MALFORMED);
		return false;
	}
	return true;
}
}