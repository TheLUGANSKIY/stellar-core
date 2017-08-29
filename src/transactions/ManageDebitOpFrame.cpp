#include "ManageDebitOpFrame.h"
#include "database/Database.h"
#include "ledger/LedgerManager.h"
#include "ledger/DebitFrame.h"
#include "ledger/TrustFrame.h"
#include "main/Application.h"
#include "medida/meter.h"
#include "medida/metrics_registry.h"

namespace stellar
{

ManageDebitOpFrame::ManageDebitOpFrame(Operation const& op,
									   OperationResult& res,
									   TransactionFrame& parentTx)
	: OperationFrame(op, res, parentTx)
	, mManageDebit(mOperation.body.manageDebitOp())
{
}

bool
ManageDebitOpFrame::doApply(Application& app, LedgerDelta& delta,
							LedgerManager& ledgerManager)
{
	Database& db = ledgerManager.getDatabase();

	auto& debit = DebitFrame::loadDebit(getSourceID(),
										mManageDebit.debitor,
										mManageDebit.asset,
										db, &delta);

	if (mManageDebit.toDelete)
	{
		if (!debit)
		{
			app.getMetrics()
				.NewMeter({ "op-manage-debit", "failure", "not-found" },
							"operation")
				.Mark();
			innerResult().code(MANAGE_DEBIT_NOT_FOUND);
			return false;
		}

		if (!mSourceAccount->addNumEntries(-1, ledgerManager))
		{
			app.getMetrics()
				.NewMeter({ "op-manage-debit", "failure", "low-reserve" },
							"operation")
				.Mark();
			innerResult().code(MANAGE_DEBIT_LOW_RESERVE);
			return false;
		}

		mSourceAccount->storeChange(delta, db);
		debit->storeDelete(delta, db);
		
		app.getMetrics()
			.NewMeter({ "op-manage-debit", "success", "apply" }, "operation")
			.Mark();
		innerResult().code(MANAGE_DEBIT_SUCCESS);
		return true;
	}

	if (debit)
	{
		app.getMetrics()
			.NewMeter({ "op-manage-debit", "failure", "debit-already-exists" },
						"operation")
			.Mark();
		innerResult().code(MANAGE_DEBIT_ALREADY_EXISTS);
		return false;
	}

	auto& debitor = AccountFrame::loadAccount(delta,
											  mManageDebit.debitor,
											  db);
	if (!debitor)
	{
		app.getMetrics()
			.NewMeter({ "op-manage-debit", "failure", "no-debitor" },
						"operation")
			.Mark();
		innerResult().code(MANAGE_DEBIT_NO_DEBITOR);
		return false;
	}

	auto& trustLine = TrustFrame::loadTrustLine(getSourceID(),
												mManageDebit.asset, 
												db, &delta);
	if (!trustLine)
	{
		app.getMetrics()
			.NewMeter({ "op-manage-debit", "failure", "no-trust" },
						"operation")
			.Mark();
		innerResult().code(MANAGE_DEBIT_NO_TRUST);
		return false;
	}

	debit = std::make_shared<DebitFrame>();

	auto& deb = debit->getDebit();
	deb.owner = getSourceID();
	deb.debitor = mManageDebit.debitor;
	deb.asset = mManageDebit.asset;

	if (!mSourceAccount->addNumEntries(1, ledgerManager))
	{
		app.getMetrics()
			.NewMeter({ "op-manage-debit", "failure", "low-reserve" },
						"operation")
			.Mark();
		innerResult().code(MANAGE_DEBIT_LOW_RESERVE);
		return false;
	}

	mSourceAccount->storeChange(delta, db);
	debit->storeAdd(delta, db);

	app.getMetrics()
		.NewMeter({ "op-manage-debit", "success", "apply" }, "operation")
		.Mark();
	innerResult().code(MANAGE_DEBIT_SUCCESS);
	return true;
}

bool
ManageDebitOpFrame::doCheckValid(Application& app)
{
	if (!isAssetValid(mManageDebit.asset))
	{
		app.getMetrics()
			.NewMeter({ "op-manage-debit", "invalid", "malformed-invalid-asset" },
						"operation")
			.Mark();
		innerResult().code(MANAGE_DEBIT_MALFORMED);
		return false;
	}

	if (getSourceID() == mManageDebit.debitor)
	{
		app.getMetrics()
			.NewMeter({ "op-manage-debit", "invalid", "malformed-self-debit" },
						"operation")
			.Mark();
		innerResult().code(MANAGE_DEBIT_MALFORMED);
		return false;
	}
	return true;
}
}