#pragma once

#include "ledger\AccountFrame.h"
#include "ledger\EntryFrame.h"
#include <functional>
#include <unordered_map>

namespace soci
{
	class session;
	namespace details
	{
		class prepare_temp_type;
	}
}

namespace stellar
{
	class StatementContext;

	class DebitFrame : public EntryFrame
	{
	public:
		typedef std::shared_ptr<DebitFrame> pointer;

	private:
		static void getKeyFields(LedgerKey const& key, std::string& ownerStrKey,
								  std::string& debitorStrKey, 
								  std::string& issuerStrKey, std::string& assetCode);

		static void
		loadDebits(StatementContext& prep,
				   std::function<void(LedgerEntry const&)> debitProcessor);

		DebitEntry& mDebit;

		DebitFrame(DebitFrame const& from);
		DebitFrame& operator=(DebitFrame const& other);

	public:
		DebitFrame();
		DebitFrame(LedgerEntry const& from);

		EntryFrame::pointer
			copy() const override
		{
			return EntryFrame::pointer(new DebitFrame(*this));
		}

		// Instance-based overrides of EntryFrame.
		void storeDelete(LedgerDelta& delta, Database& db) const override;
		void storeChange(LedgerDelta& delta, Database& db) override;
		void storeAdd(LedgerDelta& delta, Database& db) override;

		// Static helper that don't assume an instance.
		static void storeDelete(LedgerDelta& delta, Database& db,
			LedgerKey const& key);
		static bool exists(Database& db, LedgerKey const& key);
		static uint64_t countObjects(soci::session& sess);

		// returns the specified debit
		static pointer loadDebit(AccountID const& owner, AccountID const& debitor, Asset const& asset,
			Database& db, LedgerDelta* delta = nullptr);

		// note: only returns debits stored in the database
		static void loadDebits(AccountID const& owner,
							   std::vector<DebitFrame::pointer>& retDebits,
			                   Database& db);

		// loads ALL debits from the database (very slow!)
		static std::unordered_map<AccountID, std::vector<DebitFrame::pointer>>
			loadAllDebits(Database& db);

		AccountID getOwner() const;
		AccountID getDebitor() const;
		Asset getAsset() const;

		DebitEntry const&
		getDebit() const
		{
			return mDebit;
		}
		DebitEntry&
		getDebit()
		{
			return mDebit;
		}

		static bool isValid(DebitEntry const& debit);
		bool isValid() const;


		static void dropAll(Database& db);
		static const char* kSQLCreateStatement1;
		static const char* kSQLCreateStatement2;
	};
}