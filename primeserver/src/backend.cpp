/*
 * backend.cpp
 *
 *  Created on: 29.04.2014
 *      Author: mad
 */



#include "pool.h"

#include "bitcoinrpc.h"


#include <sstream>




PoolBackend::PoolBackend(CWallet* pwallet) {
	
	printf("PoolBackend started.\n");
	
	mWallet = pwallet;
	
	mPoolAddress = GetArg("-feeaddr", "POOLFEE_ADDRESS");
	
	mCtx = zctx_new();
	
	mPipe = zthread_fork(mCtx, &PoolBackend::InvokeThread, this);
	
	std::string dbconn;
	{
		std::ostringstream ss;
		ss << "host=" << GetArg("-dbhost", "localhost");
		ss << " port=" << GetArg("-dbport", 5432);
		ss << " dbname=" << GetArg("-dbname", "testdb8");
		ss << " user=" << GetArg("-dbuser", "backend");
		ss << " password=" << GetArg("-dbpass", "XYZPASS");
		dbconn = ss.str();
	}
	
	mBackend = new dbo::backend::Postgres(dbconn);
	mSession = new dbo::Session();
	mSession->setConnection(*mBackend);
	
	mSession->mapClass<db::Address>("address");
	mSession->mapClass<db::ShareValue>("sharevalue");
	mSession->mapClass<db::PoolBalance>("poolbalance");
	mSession->mapClass<db::PayoutRequest>("payoutrequest");
	mSession->mapClass<db::PayoutTransaction>("payouttransaction");
	mSession->mapClass<db::Block>("block");
	mSession->mapClass<db::Share>("share");
	mSession->mapClass<db::ClientStats>("clientstats");
	mSession->mapClass<db::ReqStats>("reqstats");
	mSession->mapClass<db::ServerStats>("serverstats");
	mSession->mapClass<db::SiteStats>("sitestats");
	mSession->mapClass<db::Settings>("settings");
	
	if(GetBoolArg("-initwtdb", false))
		mSession->createTables();
	
	dbo::Transaction transaction(*mSession);
	
	db::Settings* settings = new db::Settings();
	settings->InsertTime = WDateTime::currentDateTime();
	settings->PoolName = GetArg("-poolname", "xpmpool");
	settings->Target = GetArg("-target", 10);
	settings->MinShare = GetArg("-minshare", 8);
	//settings->NumShares = GetArg("-numshares", 3);
	settings->NumShares = 3;
	settings->MaxShare = settings->MinShare + settings->NumShares - 1;
	settings->Fee = GetArg("-poolfee", 10);
	settings->MinPayout = GetArg("-minpayout", 11);
	
	mSettings = mSession->add(settings);
	
	transaction.commit();
	
	// Compute share values etc
	mTickCounter = 0;
	Accounting();
	
}


PoolBackend::~PoolBackend() {
	
	zsocket_signal(mPipe);
	zsocket_wait(mPipe);
	
	zctx_destroy(&mCtx);
	
	//mSession->flush();
	delete mSession;
	delete mBackend;
	
	printf("PoolBackend stopped.\n");
	
}


void PoolBackend::InvokeThread(void *arg, zctx_t *ctx, void *pipe) {
	
	((PoolBackend*)arg)->ThreadLoop(ctx, pipe);
	
}


void PoolBackend::ThreadLoop(zctx_t *ctx, void *pipe) {
	
	void* router = zsocket_new(ctx, ZMQ_ROUTER);
	zsocket_bind(router, "tcp://*:8888");
	
	zsocket_set_rcvhwm(router, 1*1000*1000);
	
	zloop_t* wloop = zloop_new();
	
	zmq_pollitem_t item_input = {router, 0, ZMQ_POLLIN, 0};
	int err = zloop_poller(wloop, &item_input, &PoolBackend::InvokeInput, this);
	assert(!err);
	
	zmq_pollitem_t item_pipe = {pipe, 0, ZMQ_POLLIN, 0};
	err = zloop_poller(wloop, &item_pipe, &PoolBackend::InvokeShutdown, this);
	assert(!err);
	
	err = zloop_timer(wloop, 60*1000, 0, &PoolBackend::InvokeAccounting, this);
	assert(err >= 0);
	
	zloop_start(wloop);
	
	zloop_destroy(&wloop);
	
	zsocket_destroy(ctx, router);
	
	printf("PoolBackend shutdown.\n");
	
	zsocket_signal(pipe);
	
}


int PoolBackend::InvokeInput(zloop_t *wloop, zmq_pollitem_t *item, void *arg) {
	
	return ((PoolBackend*)arg)->HandleData(item);
	
}

int PoolBackend::InvokeShutdown(zloop_t *wloop, zmq_pollitem_t *item, void *arg) {
	
	return -1;
	
}


int PoolBackend::InvokeAccounting(zloop_t *loop, int timer_id, void *arg) {
	
	return ((PoolBackend*)arg)->Accounting();
	
}

int PoolBackend::Accounting() {
	
	dbo::Session& session = *mSession;
	dbo::Transaction transaction(session);
	
	WDateTime currtime = WDateTime::currentDateTime();
	
	// Site stats
	if(mTickCounter % 1 == 0){
		
		db::SiteStats* stats = new db::SiteStats();
		stats->InsertTime = currtime;
		stats->Workers = 0;
		stats->Latency = 0;
		stats->CPD = 0;
		stats->StaleRate = -1;
		
		for(std::map<std::pair<std::string,int>, proto::ServerStats>::const_iterator iter = mLastServerStats.begin();
				iter != mLastServerStats.end(); ++iter)
		{
			stats->Workers += iter->second.workers();
			stats->Latency += iter->second.latency();
			stats->CPD += iter->second.cpd();
		}
		
		if(mLastServerStats.size())
			stats->Latency /= mLastServerStats.size();
		
		printf("SiteStats: %d workers  %dms latency  %.2f chains/day\n", stats->Workers, stats->Latency, stats->CPD);
		session.add(stats);
		//mLastServerStats.clear();
		
	}
	
	// Check blocks
	if(mTickCounter > 1){
		
		dbo::collection<dbo::ptr<db::Block> > blocks =
				session.find<db::Block>().where("confirmations < 100 and orphan = FALSE");
		
		printf("Accounting: Checking %d blocks for confirmations...\n", (int)blocks.size());
		
		int confirmationsSum = 0;
		for(dbo::collection<dbo::ptr<db::Block> >::iterator iter = blocks.begin();
				iter != blocks.end(); ++iter)
		{
			dbo::ptr<db::Block> dbblock = *iter;
			
			uint256 hash = uint256(dbblock->Hash);
			std::map<uint256, CBlockIndex*>::const_iterator mi = mapBlockIndex.find(hash);
			if(mi != mapBlockIndex.end()){
				
				const CBlockIndex* pindex = mi->second;
				if(pindex->IsInMainChain()){
					
					CBlock block;
					block.ReadFromDisk(pindex);
					CMerkleTx txGen(block.vtx[0]);
					txGen.SetMerkleBranch(&block);
					int iConfirmations = txGen.GetDepthInMainChain();
					
					confirmationsSum += iConfirmations - dbblock->Confirmations;
					dbblock.modify()->Confirmations = iConfirmations;
					
				}else{
					
					printf("Accounting: Orphan block found: %s\n", dbblock->Hash.c_str());
					dbblock.modify()->Orphan = true;
					
				}
				
			}else{
				
				printf("Accounting: block not found: %s\n", dbblock->Hash.c_str());
				dbblock.modify()->Orphan = true;
				
			}
			
		}
		
		printf("Accounting: found %d confirmations total\n", confirmationsSum);
		
	}
	
	// Account shares
	if(mTickCounter % 1 == 0){
		
		dbo::collection<dbo::ptr<db::Share> > shares =
				session.find<db::Share>().where("accounted = FALSE").orderBy("Addr");
		
		printf("Accounting: Checking %d new shares...\n", (int)shares.size());
		
		int64 valueSum = 0;
		dbo::ptr<db::Address> addr;
		for(dbo::collection<dbo::ptr<db::Share> >::iterator iter = shares.begin();
				iter != shares.end(); ++iter)
		{
			dbo::ptr<db::Share> share = *iter;
			
			if(!addr || share->Addr != addr->Addr){
				
				addr = session.find<db::Address>().where("addr = ?").bind(share->Addr);
				if(!addr){
					
					db::Address* newaddr = new db::Address();
					newaddr->Addr = share->Addr;
					newaddr->Balance = 0;
					newaddr->Requested = 0;
					newaddr->Paid = 0;
					newaddr->MinPayout = 0;
					newaddr->Blocked = false;
					
					printf("Accounting: adding new address: %s\n", newaddr->Addr.c_str());
					addr = session.add(newaddr);
					
				}
				
			}
			
			valueSum += share->Value;
			addr.modify()->Balance += share->Value;
			share.modify()->Accounted = true;
			
			int64 minpayout = int64(mSettings->MinPayout) * COIN;
			if(addr->MinPayout >= 1)
				minpayout = int64(addr->MinPayout) * COIN;
			if(addr->Requested == 0 && addr->Paid == 0)
				minpayout = COIN;
			
			if(addr->Balance - addr->Requested > minpayout && !addr->Blocked){
				
				db::PayoutRequest* req = new db::PayoutRequest();
				req->InsertTime = currtime;
				req->Addr = addr->Addr;
				req->Amount = minpayout;
				
				addr.modify()->Requested += req->Amount;
				
				printf("Accounting: payout request %s XPM to %s\n", FormatMoney(req->Amount).c_str(), req->Addr.c_str());
				session.add(req);
				
			}
			
		}
		
		printf("Accounting: %s XPM have been mined.\n", FormatMoney(valueSum).c_str());
		
	}
	
	if(mTickCounter == 3 && GetBoolArg("-finalpayout", false)){
		
		dbo::collection<dbo::ptr<db::Address> > users = session.find<db::Address>();
		
		printf("Accounting: FINAL: Checking %d users...\n", (int)users.size());
		
		for(dbo::collection<dbo::ptr<db::Address> >::iterator iter = users.begin();
				iter != users.end(); ++iter)
		{
			dbo::ptr<db::Address> addr = *iter;
			
			long long amount = addr->Balance - addr->Requested;
			if(amount > 10000000){
				
				db::PayoutRequest* req = new db::PayoutRequest();
				req->InsertTime = currtime;
				req->Addr = addr->Addr;
				req->Amount = amount;
				
				addr.modify()->Requested += req->Amount;
				
				printf("Accounting: payout request %s XPM to %s\n", FormatMoney(req->Amount).c_str(), req->Addr.c_str());
				session.add(req);
				
			}
			
		}
		
	}
	
	// Payout execution
	if(mTickCounter % 15 == 5 && GetBoolArg("-payout", true)){
		
		dbo::collection<dbo::ptr<db::PayoutRequest> > requests =
				session.find<db::PayoutRequest>().orderBy("inserttime ASC");
		
		printf("Accounting: checking %d payout requests...\n", (int)requests.size());
		
		for(dbo::collection<dbo::ptr<db::PayoutRequest> >::iterator iter = requests.begin();
				iter != requests.end(); ++iter)
		{
			dbo::ptr<db::PayoutRequest> request = *iter;
			
			dbo::ptr<db::Address> addr;
			bool blockuser = false;
			bool empty = false;
			bool success = false;
			bool remove = false;
			
			while(true){
				
				addr = session.find<db::Address>().where("addr = ?").bind(request->Addr);
				if(!addr){
					printf("Accounting: ERROR: Addr not found for payout: %s", request->Addr.c_str());
					break;
				}
				
				CBitcoinAddress address(request->Addr);
				if(!address.IsValid()){
					printf("Accounting: invalid address found: %s\n", request->Addr.c_str());
					blockuser = true;
					remove = true;
					break;
				}
				
				int64 balance = mWallet->GetBalance();
				if(balance - request->Amount < COIN){
					printf("Accounting: no money left to pay.\n");
					empty = true;
					break;
				}
				
				LOCK(cs_main);
				
				CWalletTx wtx;
				wtx.mapValue["comment"] = mSettings->PoolName;
				
				std::string strError = mWallet->SendMoneyToDestination(address.Get(), request->Amount, wtx);
				if(strError == ""){
					
					db::PayoutTransaction* trans = new db::PayoutTransaction();
					trans->InsertTime = currtime;
					trans->Addr = request->Addr;
					trans->Amount = request->Amount;
					trans->TxID = wtx.GetHash().ToString();
					session.add(trans);
					
					printf("Accounting: %s XPM sent with txid %s\n", FormatMoney(request->Amount).c_str(), trans->TxID.c_str());
					success = true;
					
				}else{
					printf("Accounting: SendMoneyToDestination FAILED: %s XPM to %s because: %s\n",
							FormatMoney(request->Amount).c_str(), request->Addr.c_str(), strError.c_str());
				}
				
				break;
			}
			
			if(addr){
				
				if(success){
					addr.modify()->Balance -= request->Amount;
					addr.modify()->Requested -= request->Amount;
					addr.modify()->Paid += request->Amount;
				}
				
				if(blockuser)
					addr.modify()->Blocked = true;
				
			}
			
			if(success || remove)
				request.remove();
			
			if(empty)
				break;
			
		}
		
	}
	
	// Pool balance
	if(mTickCounter % 15 == 5){
		
		db::PoolBalance* balance = new db::PoolBalance();
		balance->InsertTime = currtime;
		balance->Balance = mWallet->GetBalance();
		balance->Immature = mWallet->GetImmatureBalance();
		balance->UserBalance = session.query<long long>("select coalesce(sum(balance), 0) from address");
		balance->NetBalance = balance->Balance + balance->Immature - balance->UserBalance;
		
		printf("Accounting: balance=%s XPM  immature=%s XPM  users=%s XPM  net=%s XPM\n",
				FormatMoney(balance->Balance).c_str(), FormatMoney(balance->Immature).c_str(),
				FormatMoney(balance->UserBalance).c_str(), FormatMoney(balance->NetBalance).c_str());
		
		session.add(balance);
		
	}
	
	// Update share values
	if(mTickCounter % 30 == 0){
		
		WDateTime tbegin = currtime.addDays(-1);
		int minshare = mSettings->MinShare;
		int numshares = mSettings->NumShares;
		int maxshare = mSettings->MaxShare;
		double fee = double(mSettings->Fee) / 100.;
		double payperc = 1. - fee;
		
		int64 iblocksum = session.query<long long>("select coalesce(sum(value), 0) from block")
								.where("inserttime > ? and confirmations >= 6")
								.bind(tbegin.addSecs(-6*60));
		
		printf("Accounting: blocksum = %s XPM\n", FormatMoney(iblocksum).c_str());
		
		double blocksum = double(iblocksum)/double(COIN);
		
		std::map<int, int> sharecount;
		for(int i = 0; i < numshares; ++i){
			
			int length = minshare + i;
			sharecount[length] = session.query<int>("select count(1) from share")
									.where("inserttime > ? and chainlength = ?")
									.bind(tbegin).bind(length);
			
		}
		
		sharecount[maxshare] += session.query<int>("select count(1) from share")
								.where("inserttime > ? and chainlength > ?")
								.bind(tbegin).bind(maxshare);
		
		for(int i = 0; i < numshares; ++i){
			
			int length = minshare + i;
			double shareperc = 0.;
			switch(i){
				case 0: shareperc = 0.3; break;
				case 1: shareperc = 0.3; break;
				case 2: shareperc = 0.4; break;
			}
			
			double avail = blocksum * payperc * shareperc;
			double value = 0.;
			if(sharecount[length] > 0)
				value = avail / double(sharecount[length]);
			
			value = fmin(value, 10.);
			
			int64 ivalue = value * double(COIN);
			
			mShareValues[length] = ivalue;
			
			db::ShareValue* sharevalue = new db::ShareValue();
			sharevalue->InsertTime = currtime;
			sharevalue->ChainLength = length;
			sharevalue->Value = ivalue;
			session.add(sharevalue);
			
			printf("New ShareValue: %dx %d-chain => %s XPM\n", sharecount[length], length, FormatMoney(ivalue).c_str());
			
		}
		
	}
	
	transaction.commit();
	//session.flush();
	
	mTickCounter++;
	return 0;
	
}


int PoolBackend::HandleData(zmq_pollitem_t *item) {
	
	proto::Data& data = mData;
	
	zmsg_t* msg = zmsg_recv(item->socket);
	zframe_t* frame = zmsg_last(msg);
	zmsg_remove(msg, frame);
	size_t fsize = zframe_size(frame);
	const byte* fbytes = zframe_data(frame);
	
	data.ParseFromArray(fbytes, fsize);
	zframe_destroy(&frame);
	
	//data.PrintDebugString();
	
	dbo::Session& session = *mSession;
	dbo::Transaction transaction(session);
	
	WDateTime currtime = WDateTime::currentDateTime();
	
	if(data.has_share()){
		
		const proto::Share& s = data.share();
		
		db::Share* share = new db::Share();
		share->Addr = repInvUTF8(s.addr());
		share->Name = repInvUTF8(s.name());
		share->ClientID = db::format::ulongToStr(s.clientid());
		share->InsertTime = currtime;
		share->Height = s.height();
		share->ChainLength = s.length();
		share->IsBlock = s.isblock();
		share->Value = mShareValues[std::min(s.length(), (unsigned)mSettings->MaxShare)];
		share->Accounted = false;
		
		if(s.isblock()){
			
			db::Block* block = new db::Block();
			block->InsertTime = currtime;
			block->Hash = s.blockhash();
			block->Height = s.height()+1;
			block->Confirmations = 0;
			block->Orphan = false;
			block->Value = s.genvalue();
			block->Addr = repInvUTF8(share->Addr);
			block->ClientID = share->ClientID;
			
			session.add(block);
			
			db::Share* fee = new db::Share();
			fee->Addr = mPoolAddress;
			fee->Name = "poolfee";
			fee->ClientID = "0";
			fee->InsertTime = currtime;
			fee->Height = s.height();
			fee->ChainLength = -1;
			fee->IsBlock = false;
			fee->Value = (block->Value * mSettings->Fee) / 100;
			fee->Accounted = false;
			
			session.add(fee);
			
		}
		
		session.add(share);
		
	}
	
	if(data.has_clientstats()){
		
		const proto::ClientStats& s = data.clientstats();
		
		db::ClientStats* stats = new db::ClientStats();
		stats->Addr = repInvUTF8(s.addr());
		stats->Name = repInvUTF8(s.name());
		stats->ClientID = db::format::ulongToStr(s.clientid());
		stats->InsertTime = currtime;
		stats->Version = s.version();
		stats->CPD = s.cpd();
		stats->Latency = s.latency();
		stats->Temp = s.temp();
		stats->Errors = s.errors();
		stats->GPUs = s.ngpus();
		stats->Height = s.height();
		
		session.add(stats);
		
	}
	
	if(data.has_serverstats()){
		
		const proto::ServerStats& s = data.serverstats();
		
		db::ServerStats* stats = new db::ServerStats();
		stats->ServerName = s.name();
		stats->Thread = s.thread();
		stats->InsertTime = currtime;
		stats->Workers = s.workers();
		stats->Latency = s.latency();
		stats->CPD = s.cpd();
		
		session.add(stats);
		
		for(int i = 0; i < s.reqstats_size(); ++i){
			
			const proto::ReqStats& r = s.reqstats(i);
			
			db::ReqStats* rstats = new db::ReqStats();
			rstats->ServerName = s.name();
			rstats->Thread = s.thread();
			rstats->InsertTime = currtime;
			rstats->ReqName = proto::Request::Type_Name(r.reqtype());
			rstats->ErrorName = proto::Reply::ErrType_Name(r.errtype());
			rstats->Count = r.count();
			
			session.add(rstats);
			
		}
		
		mLastServerStats[std::make_pair(s.name(), s.thread())] = s;
		
	}
	
	transaction.commit();
	
	zmsg_destroy(&msg);
	return 0;
	
}









