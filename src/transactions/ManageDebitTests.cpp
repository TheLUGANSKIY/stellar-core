#include "lib/catch.hpp"
#include "lib/json/json.h"
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

TEST_CASE("manage debit", "[tx][managedebit]")
{
	Config const& cfg = getTestConfig();

	VirtualClock clock;
	ApplicationEditableVersion app{ clock, cfg };
	Database& db = app.getDatabase();

	app.start();

	// set up world
	auto root = TestAccount::createRoot(app);

	auto const minBalance2 = app.getLedgerManager().getMinBalance(2);

	auto gateway = root.create("gw", minBalance2);
	Asset idrCur = makeAsset(gateway, "IDR");
	auto gateway2 = root.create("gw2", minBalance2);
	Asset usdCur = makeAsset(gateway2, "USD");
	
	root.changeTrust(idrCur, 1000);

	auto debitor = root.create("deb", minBalance2);
	
	SECTION("debiting self")
	{
		for_all_versions(app, [&] {
			REQUIRE_THROWS_AS(root.manageDebit(root, idrCur, false),
				ex_MANAGE_DEBIT_MALFORMED);
		});
	}
	SECTION("debitor is not found")
	{
		for_all_versions(app, [&] {
			REQUIRE_THROWS_AS(root.manageDebit(getAccount("B").getPublicKey(), idrCur, false),
				ex_MANAGE_DEBIT_NO_DEBITOR);
		});
	}
	SECTION("no trustline for debiting asset")
	{
		for_all_versions(app, [&] {
			REQUIRE_THROWS_AS(root.manageDebit(debitor, usdCur, false),
				ex_MANAGE_DEBIT_NO_TRUST);
		});
	}
	SECTION("no debit found while deleting")
	{
		for_all_versions(app, [&] {
			REQUIRE_THROWS_AS(root.manageDebit(debitor, idrCur, true),
				ex_MANAGE_DEBIT_NOT_FOUND);
		});
	}
	SECTION("debit already exists")
	{
		for_all_versions(app, [&] {
			root.manageDebit(debitor, idrCur, false);
			REQUIRE_THROWS_AS(root.manageDebit(debitor, idrCur, false),
				ex_MANAGE_DEBIT_ALREADY_EXISTS);
		});
	}
	SECTION("basic tests")
	{
		for_all_versions(app, [&] {
			root.manageDebit(debitor, idrCur, false);
			root.manageDebit(debitor, idrCur, true);
		});
	}
}