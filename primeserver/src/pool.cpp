/*
 * pool.cpp
 *
 *  Created on: 18.04.2014
 *      Author: mad
 */



#include "pool.h"

#include "bitcoinrpc.h"




void thread_test_inv(void* args, zctx_t *ctx, void *pipe);



PrimeWorker::PrimeWorker(CWallet* pwallet, unsigned threadid, unsigned target)
	:	mReserveKey(pwallet)
{
	
	mWallet = pwallet;
	mThreadID = threadid;
	
	mServer = 0;
	mSignals = 0;
	mBackend = 0;
	
	mCurrHeight = 0;
	mExtraNonce = 0;
	mBlockTemplate = 0;
	mIndexPrev = 0;
	mWorkerCount = 0;
	mInvCount = 0;
	
	mServerPort = GetArg("-serverport", 60000) + 2*mThreadID;
	mSignalPort = mServerPort+1;
	
	mTarget = target;
	mReqDiff = GetArg("-reqdiff", 0);
	mHost = GetArg("-host", "localhost");
	mName = GetArg("-servername", "Server1");
	
	mServerInfo.set_host(mHost);
	mServerInfo.set_router(mServerPort);
	mServerInfo.set_pub(mSignalPort);
	mServerInfo.set_target(mTarget);
	
	mServerStats.set_name(mName);
	mServerStats.set_thread(mThreadID);
	mServerStats.set_workers(0);
	
}


void PrimeWorker::InvokeWork(void *args, zctx_t *ctx, void *pipe){
	
	((PrimeWorker*)args)->Work(ctx, pipe);
	
}

int PrimeWorker::InvokeInput(zloop_t *wloop, zmq_pollitem_t *item, void *arg){
	
	return ((PrimeWorker*)arg)->HandleInput(item);
	
}

/*int PrimeWorker::InvokeBackend(zloop_t *wloop, zmq_pollitem_t *item, void *arg){
	
	return ((PrimeWorker*)arg)->HandleBackend(item);
	
}*/

int PrimeWorker::InvokeRequest(zloop_t *wloop, zmq_pollitem_t *item, void *arg){
	
	return ((PrimeWorker*)arg)->HandleRequest(item);
	
}

int PrimeWorker::InvokeTimerFunc(zloop_t *wloop, int timer_id, void *arg) {
	
	return ((PrimeWorker*)arg)->FlushStats();
	
}


void PrimeWorker::Work(zctx_t *ctx, void *pipe) {
	
	printf("PrimeWorker started.\n");
	
	mBackend = zsocket_new(ctx, ZMQ_DEALER);
	void* frontend = zsocket_new(ctx, ZMQ_DEALER);
	void* input = zsocket_new(ctx, ZMQ_SUB);
	mServer = zsocket_new(ctx, ZMQ_ROUTER);
	mSignals = zsocket_new(ctx, ZMQ_PUB);
	
	zsocket_set_sndhwm(mBackend, 1*1000*1000);
	
	int err = 0;
	err = zsocket_bind(mServer, "tcp://*:%d", mServerPort);
	if(!err)
		printf("zsocket_bind(mServer, tcp://*:*) failed.\n");
	
	err = zsocket_bind(mSignals, "tcp://*:%d", mSignalPort);
	if(!err)
		printf("zsocket_bind(mSignals, tcp://*:*) failed.\n");
	
	printf("PrimeWorker: mServerPort=%d mSignalPort=%d\n", mServerPort, mSignalPort);
	
	err = zsocket_connect(mBackend, "tcp://localhost:8888");
	assert(!err);
	
	err = zsocket_connect(frontend, "tcp://localhost:7777");
	assert(!err);
	
	err = zsocket_connect(input, "inproc://bitcoin");
	assert(!err);
	
	const char one[2] = {1, 0};
	zsocket_set_subscribe(input, one);
	
	zloop_t* wloop = zloop_new();
	
	zmq_pollitem_t item_input = {input, 0, ZMQ_POLLIN, 0};
	err = zloop_poller(wloop, &item_input, &PrimeWorker::InvokeInput, this);
	assert(!err);
	
	zmq_pollitem_t item_server = {mServer, 0, ZMQ_POLLIN, 0};
	err = zloop_poller(wloop, &item_server, &PrimeWorker::InvokeRequest, this);
	assert(!err);
	
	zmq_pollitem_t item_frontend = {frontend, 0, ZMQ_POLLIN, 0};
	err = zloop_poller(wloop, &item_frontend, &PrimeWorker::InvokeRequest, this);
	assert(!err);
	
	err = zloop_timer(wloop, 60000, 0, &PrimeWorker::InvokeTimerFunc, this);
	assert(err >= 0);
	
	zsocket_signal(pipe);
	
	zloop_start(wloop);
	
	zloop_destroy(&wloop);
	
	zsocket_destroy(ctx, mServer);
	zsocket_destroy(ctx, mSignals);
	zsocket_destroy(ctx, mBackend);
	zsocket_destroy(ctx, frontend);
	zsocket_destroy(ctx, input);
	
	zsocket_signal(pipe);
	
	printf("PrimeWorker exited.\n");
	
}


zmsg_t* PrimeWorker::ReceiveRequest(proto::Request& req, void* socket) {
	
	zmsg_t* msg = zmsg_recv(socket);
	zframe_t* frame = zmsg_last(msg);
	zmsg_remove(msg, frame);
	size_t fsize = zframe_size(frame);
	const byte* fbytes = zframe_data(frame);
	
	bool ok = req.ParseFromArray(fbytes, fsize);
	zframe_destroy(&frame);
	
	bool valid = false;
	while(ok){
		
		if(!proto::Request::Type_IsValid(req.type()))
			break;
		
		if(!req.has_reqnonce())
			break;
		
		if(CheckVersion(req.version()) <= 0)
			break;
		
		uint256 reqnonce;
		{
			const std::string& nonce = req.reqnonce();
			if(nonce.length() != sizeof(uint256))
				break;
			memcpy(reqnonce.begin(), nonce.c_str(), sizeof(uint256));
		}
		
		if(!CheckReqNonce(reqnonce) || !mReqNonces.insert(reqnonce).second)
			break;
		
		if(req.has_stats()){
			
			const proto::ClientStats& stats = req.stats();
			if(!isValidUTF8(stats.addr()))
				break;
			if(!isValidUTF8(stats.name()))
				break;
			if(stats.cpd() < 0 || stats.cpd() > 150. || stats.cpd() != stats.cpd())
				break;
			
		}
		
		if(req.has_share()){
			
			const proto::Share& share = req.share();
			if(!isValidUTF8(share.addr()))
				break;
			if(!isValidUTF8(share.name()))
				break;
			if(!isValidUTF8(share.hash()))
				break;
			if(!isValidUTF8(share.merkle()))
				break;
			if(!isValidUTF8(share.multi()))
				break;
			if(share.has_blockhash() && !isValidUTF8(share.blockhash()))
				break;
			
		}
		
		valid = true;
		break;
	}
	
	if(valid)
		return msg;
	else{
		mInvCount++;
		zmsg_destroy(&msg);
		return 0;
	}
	
}


void PrimeWorker::SendReply(const proto::Reply& rep, zmsg_t** msg, void* socket) {
	
	size_t fsize = rep.ByteSize();
	zframe_t* frame = zframe_new(0, fsize);
	byte* data = zframe_data(frame);
	rep.SerializeToArray(data, fsize);
	
	zmsg_append(*msg, &frame);
	zmsg_send(msg, socket);
	
}


void PrimeWorker::SendData(const proto::Data& dataobj, void* socket) {
	
	size_t fsize = dataobj.ByteSize();
	zframe_t* frame = zframe_new(0, fsize);
	byte* data = zframe_data(frame);
	dataobj.SerializeToArray(data, fsize);
	
	zmsg_t* msg = zmsg_new();
	zmsg_append(msg, &frame);
	zmsg_send(&msg, socket);
	
}


int PrimeWorker::HandleInput(zmq_pollitem_t *item) {
	
	zmsg_t* msg = zmsg_recv(item->socket);
	zframe_t* frame = zmsg_next(msg);
	size_t fsize = zframe_size(frame);
	const byte* fbytes = zframe_data(frame);
	
	proto::Signal& sig = mSignal;
	sig.ParseFromArray(fbytes+1, fsize-1);
	
	if(sig.type() == proto::Signal::NEWBLOCK){
		
		mCurrBlock = sig.block();
		mCurrHeight = mCurrBlock.height();
		//printf("HandleInput(): proto::Signal::NEWBLOCK %d\n", mCurrHeight);
		
		zmsg_send(&msg, mSignals);
		
		while(true){
			
			while(vNodes.empty())
				MilliSleep(1000);
			
			mIndexPrev = pindexBest;
			
			if(!mIndexPrev)
				MilliSleep(1000);
			else
				break;
			
		}
		
		mWorkerCount = mNonceMap.size();
		
		mNonceMap.clear();
		mReqNonces.clear();
		mShares.clear();
		
		if(mBlockTemplate)
			delete mBlockTemplate;
		
		mBlockTemplate = CreateNewBlock(mReserveKey);
		if(!mBlockTemplate){
			printf("ERROR: CreateNewBlock() failed.\n");
			return -1;
		}
		
	}else if(sig.type() == proto::Signal::SHUTDOWN){
		
		printf("HandleInput(): proto::Signal::SHUTDOWN\n");
		
		zmsg_send(&msg, mSignals);
		
		FlushStats();
		
		return -1;
		
	}
	
	zmsg_destroy(&msg);
	return 0;
	
}


int PrimeWorker::FlushStats() {
	
	unsigned long latency = 0;
	double cpd = 0;
	for(std::map<std::pair<std::string,uint64>, proto::Data>::const_iterator iter = mStats.begin();
			iter != mStats.end(); ++iter)
	{
		const proto::ClientStats& stats = iter->second.clientstats();
		if(stats.latency() < 60*1000)
			latency += stats.latency();
		cpd += stats.cpd();
		
		SendData(iter->second, mBackend);
	}
	
	if(mStats.size())
		latency /= mStats.size();
	
	mServerStats.set_workers(mWorkerCount);
	mServerStats.set_latency(latency);
	mServerStats.set_cpd(cpd);
	
	for(std::map<std::pair<int,int>,int>::const_iterator iter = mReqStats.begin(); iter != mReqStats.end(); ++iter){
		
		proto::ReqStats* stats = mServerStats.add_reqstats();
		stats->set_reqtype((proto::Request::Type)iter->first.first);
		stats->set_errtype((proto::Reply::ErrType)iter->first.second);
		stats->set_count(iter->second);
		
	}
	
	//mServerStats.PrintDebugString();
	
	mData.Clear();
	mData.mutable_serverstats()->CopyFrom(mServerStats);
	SendData(mData, mBackend);
	
	mServerStats.mutable_reqstats()->Clear();
	mReqStats.clear();
	mStats.clear();
	
	printf("PrimeWorker %d: mInvCount = %d/%d\n", mThreadID, (unsigned)(mInvCount >> 32), (unsigned)mInvCount);
	
	return 0;
	
}


int PrimeWorker::CheckVersion(unsigned version) {
	
	/*unsigned client = version >> 4;
	unsigned target = version % 16;
	
	if(target < mTarget)
		return -1;*/
	
	if(version >= 10){
		return 2;
	}else
		return 0;
	
}


int PrimeWorker::CheckReqNonce(const uint256& nonce) {
	
	const uint32_t* limbs = (uint32_t*)nonce.begin();
	
	uint32_t tmp = limbs[0];
	for(int i = 1; i < 7; ++i)
		tmp *= limbs[i];
	tmp += limbs[7];
	
	return !tmp;
	
}


int PrimeWorker::HandleRequest(zmq_pollitem_t *item) {
	
	proto::Request& req = mRequest;
	zmsg_t* msg = ReceiveRequest(req, item->socket);
	if(!msg)
		return 0;
	
	//req.PrintDebugString();
	
	proto::Request::Type rtype = req.type();
	proto::Reply::ErrType etype = proto::Reply::NONE;
	
	proto::Reply& rep = mReply;
	rep.Clear();
	rep.set_type(rtype);
	rep.set_reqid(req.reqid());
	
	if(!proto::Request::Type_IsValid(rtype)){
		printf("ERROR: !proto::Request::Type_IsValid.\n");
		rtype = proto::Request::NONE;
		etype = proto::Reply::INVALID;
	}
	
	while(etype == proto::Reply::NONE) {
		
		int vstatus = CheckVersion(req.version());
		if(vstatus <= 0){
			rep.set_errstr("Your miner version is no longer supported. Please upgrade.");
			etype = proto::Reply::VERSION;
			break;
		}
		
		if(rtype == proto::Request::CONNECT){
			
			rep.mutable_sinfo()->CopyFrom(mServerInfo);
				
			if(vstatus == 1){
				etype = proto::Reply::VERSION;
				rep.set_errstr("Your miner version will no longer be supported in the near future. Please upgrade.");
			}
			
		}else if(rtype == proto::Request::GETWORK){
			
			if(!mCurrBlock.has_height()){
				etype = proto::Reply::HEIGHT;
				break;
			}
			
			if(req.height() != mCurrHeight){
				etype = proto::Reply::HEIGHT;
				break;
			}
			
			CBlock *pblock = &mBlockTemplate->block;
			IncrementExtraNonce(pblock, mIndexPrev, mExtraNonce);
			pblock->nTime = std::max(pblock->nTime, (unsigned int)GetAdjustedTime());
			
			mNonceMap[pblock->hashMerkleRoot] = mExtraNonce;
			
			proto::Work* work = rep.mutable_work();
			work->set_height(mCurrHeight);
			work->set_merkle(pblock->hashMerkleRoot.GetHex());
			work->set_time(pblock->nTime);
			work->set_bits(pblock->nBits);
			
		}else if(rtype == proto::Request::SHARE){
			
			if(!mCurrBlock.has_height()){
				etype = proto::Reply::STALE;
				break;
			}
			
			if(!req.has_share()){
				printf("ERROR: !req.has_share().\n");
				etype = proto::Reply::INVALID;
				break;
			}
			
			const proto::Share& share = req.share();
			
			if(share.height() != mCurrHeight){
				etype = proto::Reply::STALE;
				break;
			}
			
			if(share.length() < mCurrBlock.minshare()){
				printf("ERROR: share.length too short.\n");
				etype = proto::Reply::INVALID;
				break;
			}
			
			uint256 merkleRoot;
			merkleRoot.SetHex(share.merkle());
			
			unsigned extraNonce = mNonceMap[merkleRoot];
			if(!extraNonce){
				etype = proto::Reply::STALE;
				break;
			}
			
			unsigned nCandidateType = share.chaintype();
			if(nCandidateType > 2){
				printf("ERROR: share.chaintype invalid.\n");
				etype = proto::Reply::INVALID;
				break;
			}
			
			CBlock *pblock = &mBlockTemplate->block;
			extraNonce--;
			IncrementExtraNonce(pblock, mIndexPrev, extraNonce);
			pblock->nTime = share.time();
			pblock->nBits = share.bits();
			pblock->nNonce = share.nonce();
			
			uint256 headerHash = pblock->GetHeaderHash();
			{
				uint256 headerHashClient;
				headerHashClient.SetHex(share.hash());
				if(headerHashClient != headerHash){
					printf("ERROR: headerHashClient != headerHash.\n");
					etype = proto::Reply::INVALID;
					break;
				}
			}
			
			pblock->bnPrimeChainMultiplier.SetHex(share.multi());
			uint256 blockhash = pblock->GetHash();
			
			if(!mShares.insert(blockhash).second){
				etype = proto::Reply::DUPLICATE;
				break;
			}
			
			CBigNum bnChainOrigin = CBigNum(headerHash) * pblock->bnPrimeChainMultiplier;
			unsigned int nChainLength = 0;
			bool isblock = ProbablePrimeChainTestForMiner(bnChainOrigin, pblock->nBits, nCandidateType+1, nChainLength);
			
			nChainLength = TargetGetLength(nChainLength);
			if(nChainLength >= mCurrBlock.minshare()){
				
				if(isblock)
					isblock = CheckWork(pblock, *mWallet, mReserveKey);
				
				if(share.length() != nChainLength){
					printf("ERROR: share.length() != nChainLength.\n");
					etype = proto::Reply::INVALID;
				}
				
				mData.Clear();
				proto::Share* mshare = mData.mutable_share();
				mshare->CopyFrom(share);
				mshare->set_blockhash(blockhash.GetHex());
				mshare->set_length(nChainLength);
				mshare->set_isblock(isblock);
				
				if(isblock){
					mshare->set_genvalue(pblock->vtx[0].vout[0].nValue);
				}
				
				SendData(mData, mBackend);
				
				CBitcoinAddress address(share.addr());
				if(!address.IsValid()){
					printf("ERROR: invalid address for share: %s\n", share.addr().c_str());
					etype = proto::Reply::INVALID;
					std::string errstr = "Your payment address '";
					errstr.append(share.addr());
					errstr.append("' is INVALID!!!");
					rep.set_errstr(errstr);
					break;
				}
				
			}else{
				
				printf("ERROR: share.length too short after test: %d/%d\n", nChainLength, share.length());
				etype = proto::Reply::INVALID;
				break;
				
			}
			
		}else if(rtype == proto::Request::STATS){
			
			if(!req.has_stats()){
				printf("ERROR: !req.has_stats().\n");
				etype = proto::Reply::INVALID;
				break;
			}
			
			const proto::ClientStats& stats = req.stats();
			std::pair<std::string,uint64> key = std::make_pair(stats.addr(), stats.clientid() * stats.instanceid());
			
			std::map<std::pair<std::string,uint64>, proto::Data>::iterator iter = mStats.find(key);
			if(iter != mStats.end()){
				
				proto::ClientStats* s = mStats[key].mutable_clientstats();
				s->set_version(std::min(s->version(), stats.version()));
				s->set_cpd(s->cpd() + stats.cpd());
				s->set_errors(s->errors() + stats.errors());
				s->set_temp(std::max(s->temp(), stats.temp()));
				s->set_latency(std::max(s->latency(), stats.latency()));
				s->set_ngpus(s->ngpus() + stats.ngpus());
				/*if(s->name() != stats.name()){
					s->mutable_name()->append("+");
					s->mutable_name()->append(stats.name());
				}*/
				
			}else if(mStats.size() < 100000){
				mStats[key].mutable_clientstats()->CopyFrom(stats);
			}
			
		}
		
		break;
	}
	
	if(req.height() < mCurrHeight){
		rep.mutable_block()->CopyFrom(mCurrBlock);
	}
	
	mReqStats[std::make_pair(rtype,etype)]++;
	
	rep.set_error(etype);
	
	SendReply(rep, &msg, item->socket);
	
	zmsg_destroy(&msg);
	return 0;
	
}




PoolFrontend::PoolFrontend(unsigned port) {
	
	printf("PoolFrontend started on port %d.\n", port);
	
	mCtx = zctx_new();
	
	mDealer = zsocket_new(mCtx, ZMQ_DEALER);
	mRouter = zsocket_new(mCtx, ZMQ_ROUTER);
	
	zsocket_bind(mDealer, "tcp://*:7777");
	unsigned ret = zsocket_bind(mRouter, "tcp://*:%d", port);
	if(ret != port){
		printf("Frontend: ERROR: zsocket_bind failed.\n");
		exit(-1);
	}
	
	zthread_fork(mCtx, &PoolFrontend::InvokeProxy, this);
	
}

PoolFrontend::~PoolFrontend() {
	
	printf("PoolFrontend stopped.\n");
	
	zsocket_destroy(mCtx, mRouter);
	zsocket_destroy(mCtx, mDealer);
	
	zctx_destroy(&mCtx);
	
}


void PoolFrontend::InvokeProxy(void *arg, zctx_t *ctx, void *pipe) {
	
	((PoolFrontend*)arg)->ProxyLoop();
	
}

void PoolFrontend::ProxyLoop() {
	
	zmq_proxy(mRouter, mDealer, 0);
	
	printf("PoolFrontend shutdown.\n");
	
}




PoolServer::PoolServer(CWallet* pwallet) {
	
	printf("PoolServer started.\n");
	
	mWallet = pwallet;
	
	mBackend = new PoolBackend(pwallet);
	
	mCtx = zctx_new();
	
	mWorkerSignals = zsocket_new(mCtx, ZMQ_PUB);
	zsocket_bind(mWorkerSignals, "inproc://bitcoin");
	
	mBackendDealer = 0;
	
	mMinShare = mBackend->Settings().MinShare;
	mTarget = mBackend->Settings().Target;
	
	int nThreads = GetArg("-genproclimit", 2);
	for(int i = 0; i < nThreads; ++i){
		
		PrimeWorker* worker = new PrimeWorker(mWallet, i, mTarget);
		
		void* pipe = zthread_fork(mCtx, &PrimeWorker::InvokeWork, worker);
		zsocket_wait(pipe);
		
		mWorkers.push_back(std::make_pair(worker, pipe));
		
	}
	
	mFrontend = new PoolFrontend(GetArg("-frontport", 6666));
	
	if(GetArg("-testinv", 0)){
		printf("STARTING TESTINV!!!\n");
		zthread_fork(mCtx, &thread_test_inv, (void*)GetArg("-testinv", 0));
	}
	
}

PoolServer::~PoolServer(){
	
	printf("PoolServer stopping...\n");
	
	proto::Signal sig;
	sig.set_type(proto::Signal_Type_SHUTDOWN);
	
	SendSignal(sig, mWorkerSignals);
	
	for(unsigned i = 0; i < mWorkers.size(); ++i){
		
		zsocket_wait(mWorkers[i].second);
		delete mWorkers[i].first;
		
	}
	
	delete mFrontend;
	delete mBackend;
	
	zsocket_destroy(mCtx, mWorkerSignals);
	zctx_destroy(&mCtx);
	
	printf("PoolServer stopped.\n");
	
}


void PoolServer::NotifyNewBlock(CBlockIndex* pindex) {
	
	printf("NotifyNewBlock(%d)\n", pindex->nHeight);
	
	proto::Signal sig;
	sig.set_type(proto::Signal::NEWBLOCK);
	
	proto::Block* block = sig.mutable_block();
	block->set_height(pindex->nHeight);
	block->set_hash(pindex->phashBlock->GetHex());
	block->set_prevhash(pindex->pprev->phashBlock->GetHex());
	block->set_reqdiff(0);
	block->set_minshare(mMinShare);
	
	SendSignal(sig, mWorkerSignals);
	
}


void PoolServer::SendSignal(proto::Signal& sig, void* socket) {
	
	size_t fsize = sig.ByteSize()+1;
	zframe_t* frame = zframe_new(0, fsize);
	byte* data = zframe_data(frame);
	data[0] = 1;
	sig.SerializeToArray(data+1, fsize-1);
	
	zmsg_t* msg = zmsg_new();
	zmsg_append(msg, &frame);
	zmsg_send(&msg, socket);
	
}


PrimeServer* PrimeServer::CreateServer(CWallet* pwallet) {
	
	return new PoolServer(pwallet);
	
}



template<class C>
static void Send(const C& req, void* socket) {
	
	zmsg_t* msg = zmsg_new();
	size_t fsize = req.ByteSize();
	zframe_t* frame = zframe_new(0, fsize);
	byte* data = zframe_data(frame);
	req.SerializeToArray(data, fsize);
	
	zmsg_append(msg, &frame);
	zmsg_send(&msg, socket);
	
}

inline void GetNewReqNonce(proto::Request& req) {
	
	uint32_t limbs[8];
	for(int i = 0; i < 8; ++i)
		limbs[i] = rand();
	
	uint32_t tmp = limbs[0];
	for(int i = 1; i < 7; ++i)
		tmp *= limbs[i];
	
	limbs[7] = -tmp;
	req.set_reqnonce(&limbs[0], sizeof(limbs));
	
}

inline void setRandStr(std::string* str, int size) {
	
	char buff[1024];
	
	for(int i = 0; i < size; ++i)
		buff[i] = rand();
	buff[size] = 0;
	
	str->assign(buff);
	
}

void thread_test_inv(void* args, zctx_t *ctx, void *pipe) {
	
	int param = (uint64)args;
	
	void* server = zsocket_new(ctx, ZMQ_DEALER);
	
	printf("thread_test_inv: port = %d\n", param);
	int err = zsocket_connect(server, "tcp://localhost:%d", param);
	assert(!err);
	
	static const unsigned maxsize = 1024;
	unsigned char buffer[maxsize+10];
	memset(buffer, 0, sizeof(buffer));
	
	uint64 invcount = 0;
	proto::Request req;
	while(true){
		
		zmsg_t* msg = zmsg_new();
		size_t fsize = rand() % maxsize;
		
		for(size_t i = 0; i < fsize; ++i)
			buffer[i] = rand();
		
		zframe_t* frame = zframe_new(buffer, fsize);
		
		zmsg_append(msg, &frame);
		zmsg_send(&msg, server);
		
		/*if(invcount % 2 == 0){
			
			req.set_type(proto::Request::SHARE);
			req.set_reqid(1);
			GetNewReqNonce(req);
			req.set_version(10);
			req.set_height(1337);
			
			proto::Share* share = req.mutable_share();
			setRandStr(share->mutable_addr(), rand()%32);
			setRandStr(share->mutable_name(), rand()%32);
			setRandStr(share->mutable_hash(), rand()%32);
			setRandStr(share->mutable_merkle(), rand()%32);
			setRandStr(share->mutable_multi(), rand()%50);
			setRandStr(share->mutable_blockhash(), rand()%40);
			share->set_clientid(1313);
			share->set_bits(3444);
			share->set_length(10);
			share->set_chaintype(1);
			share->set_height(2334);
			share->set_isblock(true);
			share->set_nonce(333434);
			
		}else{
			
			req.set_type(proto::Request::STATS);
			req.set_reqid(1);
			GetNewReqNonce(req);
			req.set_version(10);
			req.set_height(1337);
			
			proto::ClientStats* stats = req.mutable_stats();
			setRandStr(stats->mutable_addr(), rand()%32);
			setRandStr(stats->mutable_name(), rand()%32);
			stats->set_clientid(345345);
			stats->set_instanceid(34344555);
			stats->set_version(10);
			stats->set_latency(445);
			stats->set_ngpus(4);
			stats->set_height(433435);
			
		}
		
		Send(req, server);*/
		
		if(zctx_interrupted)
			break;
		
		invcount++;
		if(invcount % 102400 == 0)
			printf("thread_test_inv: invcount = %d/%d\n", (unsigned)(invcount >> 32), (unsigned)invcount);
		
	}
	
	zsocket_destroy(ctx, server);
	
}










