#include "lib/catch.hpp"
#include "lib/json/json.h"
#include "transactions/DirectDebitOpFrame.h"
#include "main/Application.h"
#include "overlay/LoopbackPeer.h"
#include "test/TestAccount.h"
#include "test/TestExceptions.h"
#include "test/TestUtils.h"
#include "test/TxTests.h"
#include "test/test.h"
#include "util/Logging.h"
#include "util/make_unique.h"

using namespace stellar;
using namespace stellar::txtest;

typedef std::unique_ptr<Application> appPtr;

TEST_CASE("direct debit", "[tx][directdebit]")
{
	Config const& cfg = getTestConfig();

	VirtualClock clock;
	ApplicationEditableVersion app{ clock, cfg };
	Database& db = app.getDatabase();

	app.start();

	// set up world
	auto root = TestAccount::createRoot(app);
	auto const minBalance2 = app.getLedgerManager().getMinBalance(2);
	auto const minBalance3 = app.getLedgerManager().getMinBalance(3);
	auto debitor = root.create("deb", minBalance2);
	auto destination = root.create("dest", minBalance3);

	auto gateway = root.create("gw", minBalance3);
	Asset idrCur = makeAsset(gateway, "IDR");
	Asset usdCur = makeAsset(gateway, "USD");

	root.changeTrust(idrCur, 5000);
	gateway.pay(root, idrCur, 4000);
	destination.changeTrust(idrCur, 4000);
	gateway.pay(destination, idrCur, 3000);

	root.manageDebit(debitor, idrCur, false);

	SECTION("basic test")
	{
		for_all_versions(app, [&] {
			debitor.directDebit(root, destination, idrCur, 500);
		});
	}
	SECTION("result codes check")
	{
		auto paymentResultCodes = xdr::xdr_traits<PaymentResultCode>::enum_values();

		for (int i = 0; i < paymentResultCodes.size(); ++i)
		{
			if (xdr::xdr_traits<PaymentResultCode>::from_uint(paymentResultCodes[i]) == DIRECT_DEBIT_SUCCESS)
			{
				continue;
			}
			try
			{
				DirectDebitResultCode res = DirectDebitOpFrame::getFromPayment(app.getMetrics(), 
						  xdr::xdr_traits<PaymentResultCode>::from_uint(paymentResultCodes[i]));
			}
			catch (std::runtime_error)
			{
				throw std::runtime_error("Not all payment result codes are handled by directDebitFrame");
			}
		}
	}
	SECTION("debit does not exist")
	{
		for_all_versions(app, [&] {
			REQUIRE_THROWS_AS(debitor.directDebit(gateway,
				destination, idrCur, 500),
				ex_DIRECT_DEBIT_NO_DEBIT);
		});
	}
	SECTION("owner has no trust to asset")
	{
		root.changeTrust(usdCur, 1000);
		destination.changeTrust(usdCur, 1000);
		gateway.pay(root, usdCur, 500);
		gateway.pay(destination, usdCur, 500);
		root.manageDebit(debitor, usdCur, false);
		root.pay(gateway, usdCur, 500);
		root.changeTrust(usdCur, 0);
		for_all_versions(app, [&] {
			REQUIRE_THROWS_AS(debitor.directDebit(root,
				destination, usdCur, 500),
				ex_DIRECT_DEBIT_OWNER_NO_TRUST);
		});
	}
	SECTION("destination does not exist")
	{
		for_all_versions(app, [&] {
			REQUIRE_THROWS_AS(debitor.directDebit(root,
				getAccount("B").getPublicKey(), idrCur, 500),
				ex_DIRECT_DEBIT_NO_DESTINATION);
		});
	}
	SECTION("destination has no trust to asset")
	{
		root.changeTrust(usdCur, 1000);
		destination.changeTrust(usdCur, 1000);
		gateway.pay(root, usdCur, 500);
		gateway.pay(destination, usdCur, 500);
		root.manageDebit(debitor, usdCur, false);
		destination.pay(gateway, usdCur, 500);
		destination.changeTrust(usdCur, 0);
		for_all_versions(app, [&] {
			REQUIRE_THROWS_AS(debitor.directDebit(root,
				destination, usdCur, 500),
				ex_DIRECT_DEBIT_DESTINATION_NO_TRUST);
		});
	}
	SECTION("destination line-full")
	{
		root.changeTrust(usdCur, 1000);
		destination.changeTrust(usdCur, 500);
		gateway.pay(root, usdCur, 500);
		gateway.pay(destination, usdCur, 400);
		root.manageDebit(debitor, usdCur, false);
		for_all_versions(app, [&] {
			REQUIRE_THROWS_AS(debitor.directDebit(root, destination, usdCur, 250),
				ex_DIRECT_DEBIT_LINE_FULL);
		});
	}
	SECTION("issuer does not exist")
	{
		for_all_versions(app, [&] {
			gateway.merge(root);
			REQUIRE_THROWS_AS(debitor.directDebit(root, destination, idrCur, 500),
				ex_DIRECT_DEBIT_NO_ISSUER);
		});
	}
}