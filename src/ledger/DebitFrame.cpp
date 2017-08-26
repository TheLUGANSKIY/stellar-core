#include "ledger/DebitFrame.h"
#include "LedgerDelta.h"
#include "crypto/KeyUtils.h"
#include "crypto/SHA.h"
#include "crypto/SecretKey.h"
#include "database/Database.h"
#include "util/types.h"

using namespace std;
using namespace soci;

namespace stellar
{
using xdr::operator==;

const char* DebitFrame::kSQLCreateStatement1 =
	"CREATE TABLE debits"
	"("
	"owner        VARCHAR(56)     NOT NULL,"
	"debitor      VARCHAR(56)     NOT NULL,"
	"assettype    INT             NOT NULL,"
	"issuer       VARCHAR(56)     NOT NULL,"
	"assetcode    VARCHAR(12)     NOT NULL,"
	"PRIMARY KEY  (owner, debitor, issuer, assetcode)"
	");";

DebitFrame::DebitFrame()
	: EntryFrame(DEBIT)
	, mDebit(mEntry.data.debit())
{
}

DebitFrame::DebitFrame(LedgerEntry const& from)
	: EntryFrame(from), mDebit(mEntry.data.debit())
{
	assert(isValid());
}

DebitFrame::DebitFrame(DebitFrame const& from) : DebitFrame(from.mEntry)
{
}

DebitFrame&
DebitFrame::operator=(DebitFrame const& other)
{
	if (&other != this)
	{
		mDebit = other.mDebit;
		mKey = other.mKey;
		mKeyCalculated = other.mKeyCalculated;
	}
	return *this;
}

void
DebitFrame::getKeyFields(LedgerKey const& key, std::string& ownerStrKey,
						  std::string& debitorStrKey,
						  std::string& issuerStrKey, std::string& assetCode)
{
	ownerStrKey = KeyUtils::toStrKey(key.debit().owner);
	debitorStrKey = KeyUtils::toStrKey(key.debit().debitor);
	if (key.debit().asset.type() == ASSET_TYPE_CREDIT_ALPHANUM4)
	{
		issuerStrKey =
			KeyUtils::toStrKey(key.debit().asset.alphaNum4().issuer);
		assetCodeToStr(key.debit().asset.alphaNum4().assetCode, assetCode);
		return;
	}
	else if (key.debit().asset.type() == ASSET_TYPE_CREDIT_ALPHANUM12)
	{
		issuerStrKey =
			KeyUtils::toStrKey(key.debit().asset.alphaNum12().issuer);
		assetCodeToStr(key.debit().asset.alphaNum12().assetCode, assetCode);
		return;
	}
}

AccountID
DebitFrame::getOwner() const
{
	assert(isValid());
	return mDebit.owner;
}

AccountID
DebitFrame::getDebitor() const
{
	assert(isValid());
	return mDebit.debitor;
}

Asset
DebitFrame::getAsset() const
{
	assert(isValid());
	return mDebit.asset;
}

bool
DebitFrame::isValid(DebitEntry const& debit)
{
	bool res = debit.asset.type() != ASSET_TYPE_NATIVE;
	res = res && isAssetValid(debit.asset);
	return res;
}

bool
DebitFrame::isValid() const
{
	return isValid(mDebit);
}

bool
DebitFrame::exists(Database& db, LedgerKey const& key)
{
	if (cachedEntryExists(key, db) && getCachedEntry(key, db) != nullptr)
	{
		return true;
	}

	std::string ownerStrKey, debitorStrKey, issuerStrKey, assetCode;
	getKeyFields(key, ownerStrKey, debitorStrKey, issuerStrKey, assetCode);
	int exists = 0;
	auto timer = db.getSelectTimer("debit-exists");
	auto prep = db.getPreparedStatement(
		"SELECT EXISTS (SELECT NULL FROM debits "
		"WHERE owner=:v1 AND debitor=:v2 AND issuer=:v3 AND assetcode=:v4)");
	auto& st = prep.statement();
	st.exchange(use(ownerStrKey));
	st.exchange(use(debitorStrKey));
	st.exchange(use(issuerStrKey));
	st.exchange(use(assetCode));
	st.exchange(into(exists));
	st.define_and_bind();
	st.execute(true);
	return exists != 0;
}

uint64_t
DebitFrame::countObjects(soci::session& sess)
{
	uint64_t count = 0;
	sess << "SELECT COUNT(*) FROM debits;", into(count);
	return count;
}

void
DebitFrame::storeDelete(LedgerDelta& delta, Database& db) const
{
	storeDelete(delta, db, getKey());
}

void
DebitFrame::storeDelete(LedgerDelta& delta, Database& db, LedgerKey const& key)
{
	flushCachedEntry(key, db);

	std::string ownerStrKey, debitorStrKey, issuerStrKey, assetCode;
	getKeyFields(key, ownerStrKey, debitorStrKey, issuerStrKey, assetCode);

	auto timer = db.getDeleteTimer("debit");
	db.getSession() << "DELETE FROM debits "
		"WHERE owner=:v1 AND debitor=:v2 AND issuer=:v3 AND assetcode=:v4",
		use(ownerStrKey), use(debitorStrKey), use(issuerStrKey), use(assetCode);

	delta.deleteEntry(key);
}

void
DebitFrame::storeChange(LedgerDelta& delta, Database& db)
{
	throw std::runtime_error("DebitFrame is unchangeable object");
}

void
DebitFrame::storeAdd(LedgerDelta& delta, Database& db)
{
	if (!isValid())
	{
		throw std::runtime_error("Invalid DebitEntry");
	}

	auto key = getKey();
	flushCachedEntry(key, db);

	touch(delta);

	std::string ownerStrKey, debitorStrKey, issuerStrKey, assetCode;
	getKeyFields(key, ownerStrKey, debitorStrKey, issuerStrKey, assetCode);
	unsigned int assetType = getKey().debit().asset.type();

	auto prep = db.getPreparedStatement(
		"INSERT INTO debits "
		"(owner, debitor, assettype, issuer, assetcode)"
		"VALUES (:v1, :v2, :v3, :v4, :v5)");
	auto& st = prep.statement();
	st.exchange(use(ownerStrKey));
	st.exchange(use(debitorStrKey));
	st.exchange(use(assetType));
	st.exchange(use(issuerStrKey));
	st.exchange(use(assetCode));
	st.define_and_bind();
	{
		auto timer = db.getInsertTimer("debit");
		st.execute(true);
	}

	if (st.get_affected_rows() != 1)
	{
		throw std::runtime_error("Could not update data in SQL");
	}

	delta.addEntry(*this);
}

static const char* debitColumnSelector =
	"SELECT "
	"owner, debitor, assettype, issuer, assetcode "
	"FROM debits";

DebitFrame::pointer
DebitFrame::loadDebit(AccountID const& owner, AccountID const& debitor, Asset const& asset,
	Database& db, LedgerDelta* delta)
{
	LedgerKey key;
	key.type(DEBIT);
	key.debit().owner = owner;
	key.debit().debitor = debitor;
	key.debit().asset = asset;
	if (cachedEntryExists(key, db))
	{
		auto p = getCachedEntry(key, db);
		if (!p)
		{
			return nullptr;
		}
		pointer ret = std::make_shared<DebitFrame>(*p);
		if (delta)
		{
			delta->recordEntry(*ret);
		}
		return ret;
	}

	std::string ownerStr, debitorStr, issuerStr, assetStr;

	ownerStr = KeyUtils::toStrKey(owner);
	debitorStr = KeyUtils::toStrKey(debitor);
	if (asset.type() == ASSET_TYPE_CREDIT_ALPHANUM4)
	{
		assetCodeToStr(asset.alphaNum4().assetCode, assetStr);
		issuerStr = KeyUtils::toStrKey(asset.alphaNum4().issuer);
	}
	else if (asset.type() == ASSET_TYPE_CREDIT_ALPHANUM12)
	{
		assetCodeToStr(asset.alphaNum12().assetCode, assetStr);
		issuerStr = KeyUtils::toStrKey(asset.alphaNum12().issuer);
	}

	auto query = std::string(debitColumnSelector);
	query += (" WHERE owner = :owner "
			  " AND debitor = :debitor "
		      " AND issuer = :issuer "
		      " AND assetcode = :asset");
	auto prep = db.getPreparedStatement(query);
	auto& st = prep.statement();
	st.exchange(use(ownerStr));
	st.exchange(use(debitorStr));
	st.exchange(use(issuerStr));
	st.exchange(use(assetStr));

	pointer retDebit;
	auto timer = db.getSelectTimer("debit");
	loadDebits(prep, [&retDebit](LedgerEntry const& debit) {
		retDebit = make_shared<DebitFrame>(debit);
	});

	if (retDebit)
	{
		retDebit->putCachedEntry(db);
	}
	else
	{
		putCachedEntry(key, nullptr, db);
	}

	if (delta && retDebit)
	{
		delta->recordEntry(*retDebit);
	}
	return retDebit;
}

void
DebitFrame::loadDebits(StatementContext& prep,
					   std::function<void(LedgerEntry const&)> debitProcessor)
{
	string ownerStrKey;
	string debitorStrKey;
	std::string issuerStrKey, assetCode;
	unsigned int assetType;

	LedgerEntry le;
	le.data.type(DEBIT);

	DebitEntry& debit = le.data.debit();

	auto& st = prep.statement();
	st.exchange(into(ownerStrKey));
	st.exchange(into(debitorStrKey));
	st.exchange(into(assetType));
	st.exchange(into(issuerStrKey));
	st.exchange(into(assetCode));
	st.define_and_bind();

	st.execute(true);
	while (st.got_data())
	{
		debit.owner = KeyUtils::fromStrKey<PublicKey>(ownerStrKey);
		debit.debitor = KeyUtils::fromStrKey<PublicKey>(debitorStrKey);
		debit.asset.type((AssetType)assetType);
		if (assetType == ASSET_TYPE_CREDIT_ALPHANUM4)
		{
			debit.asset.alphaNum4().issuer =
				KeyUtils::fromStrKey<PublicKey>(issuerStrKey);
			strToAssetCode(debit.asset.alphaNum4().assetCode, assetCode);
		}
		else if (assetType == ASSET_TYPE_CREDIT_ALPHANUM12)
		{
			debit.asset.alphaNum12().issuer =
				KeyUtils::fromStrKey<PublicKey>(issuerStrKey);
			strToAssetCode(debit.asset.alphaNum12().assetCode, assetCode);
		}

		if (!isValid(debit))
		{
			throw std::runtime_error("Invalid DebitEntry");
		}

		debitProcessor(le);

		st.fetch();
	}
}

void
DebitFrame::loadDebits(AccountID const& owner,
					   std::vector<DebitFrame::pointer>& retDebits,
					   Database& db)
{
	std::string ownerStrKey;
	ownerStrKey = KeyUtils::toStrKey(owner);

	auto query = std::string(debitColumnSelector);
	query += (" WHERE owner = :owner ");
	auto prep = db.getPreparedStatement(query);
	auto& st = prep.statement();
	st.exchange(use(ownerStrKey));

	auto timer = db.getSelectTimer("debit");
	loadDebits(prep, [&retDebits](LedgerEntry const& cur){
		retDebits.emplace_back(make_shared<DebitFrame>(cur));
	});
}

std::unordered_map<AccountID, std::vector<DebitFrame::pointer>>
DebitFrame::loadAllDebits(Database& db)
{
	std::unordered_map<AccountID, std::vector<DebitFrame::pointer>> retDebits;

	auto query = std::string(debitColumnSelector);
	query += (" ORDER BY owner");
	auto prep = db.getPreparedStatement(query);

	auto timer = db.getSelectTimer("debit");
	loadDebits(prep, [&retDebits](LedgerEntry const& cur){
		auto& thisUserDebits = retDebits[cur.data.debit().owner];
		thisUserDebits.emplace_back(make_shared<DebitFrame>(cur));
	});
	return retDebits;
}

void
DebitFrame::dropAll(Database& db)
{
	db.getSession() << "DROP TABLE IF EXISTS debits;";
	db.getSession() << kSQLCreateStatement1;
}
}