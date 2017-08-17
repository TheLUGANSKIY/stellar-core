// Copyright 2014 Stellar Development Foundation and contributors. Licensed
// under the Apache License, Version 2.0. See the COPYING file at the root
// of this distribution or at http://www.apache.org/licenses/LICENSE-2.0

#include "util/asio.h"
#include "transactions/CreateAccountOpFrame.h"
#include "OfferExchange.h"
#include "database/Database.h"
#include "ledger/LedgerDelta.h"
#include "ledger/OfferFrame.h"
#include "ledger/TrustFrame.h"
#include "util/Logging.h"
#include <algorithm>
#include "main/Application.h"
#include "medida/meter.h"
#include "medida/metrics_registry.h"

namespace stellar
{

using namespace std;
using xdr::operator==;

CreateAccountOpFrame::CreateAccountOpFrame(Operation const& op,
                                           OperationResult& res,
                                           TransactionFrame& parentTx)
    : OperationFrame(op, res, parentTx)
    , mCreateAccount(mOperation.body.createAccountOp())
{
}

bool
CreateAccountOpFrame::doApply(Application& app, LedgerDelta& delta,
                              LedgerManager& ledgerManager)
{
    AccountFrame::pointer destAccount;

    Database& db = ledgerManager.getDatabase();

    destAccount =
        AccountFrame::loadAccount(delta, mCreateAccount.destination, db);
    if (!destAccount)
    {
            mSourceAccount->storeChange(delta, db);

            destAccount = make_shared<AccountFrame>(mCreateAccount.destination);
            destAccount->getAccount().seqNum =
                delta.getHeaderFrame().getStartingSequenceNumber();

            destAccount->storeAdd(delta, db);

            app.getMetrics()
                .NewMeter({"op-create-account", "success", "apply"},
                          "operation")
                .Mark();
            innerResult().code(CREATE_ACCOUNT_SUCCESS);
            return true;
    }
    else
    {
        app.getMetrics()
            .NewMeter({"op-create-account", "failure", "already-exist"},
                      "operation")
            .Mark();
        innerResult().code(CREATE_ACCOUNT_ALREADY_EXIST);
        return false;
    }
}

bool
CreateAccountOpFrame::doCheckValid(Application& app)
{
    if (mCreateAccount.destination == getSourceID())
    {
        app.getMetrics()
            .NewMeter({"op-create-account", "invalid",
                       "malformed-destination-equals-source"},
                      "operation")
            .Mark();
        innerResult().code(CREATE_ACCOUNT_MALFORMED);
        return false;
    }

    return true;
}
}
