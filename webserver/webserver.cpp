/*
 * webserver.cpp
 *
 *  Created on: 06.05.2014
 *      Author: mad
 */



#include "dbobjects.h"
using namespace pool;

#include <Wt/Dbo/backend/Postgres>
#include <Wt/Dbo/Session>
#include <Wt/Dbo/QueryModel>
#include <Wt/Dbo/FixedSqlConnectionPool>

#include <Wt/WApplication>
#include <Wt/WBreak>
#include <Wt/WContainerWidget>
#include <Wt/WLineEdit>
#include <Wt/WPushButton>
#include <Wt/WText>
#include <Wt/WBoxLayout>
#include <Wt/WGroupBox>
#include <Wt/WTimer>
#include <Wt/WTableView>
#include <Wt/WTable>



dbo::FixedSqlConnectionPool* gDBPool = 0;



class WebServer : public Wt::WApplication {
public:
	
	WebServer(const Wt::WEnvironment& env);
	~WebServer();
	
	void Update();
	
	void LoadAddress();
	
	typedef boost::tuple<int, std::string> ShareValue;
	
	typedef boost::tuple<int, int> ShareCount;
	
	// balance, requested, paid, minpayout
	typedef boost::tuple<std::string, std::string, std::string> Account;
	
	// name, gpus, inserttime, height, latency, temp, errors, cpd
	typedef boost::tuple<std::string, int, WDateTime, int, std::string, std::string, int, std::string> Worker;
	
	// name, inserttime, chainlength, value, isblock
	//typedef boost::tuple<std::string, std::string, int, std::string, bool> Share;
	
private:
	
	dbo::Session* mSession;
	
	dbo::ptr<db::Settings> mSettings;
	int mTimeOff;
	
	WDateTime mLastUpdate;
	
	WText* mPoolTime;
	WText* mPoolWorkers;
	WText* mPoolLatency;
	WText* mPoolCPD;
	
	WLineEdit* mUserAddress;
	std::string mAddr;
	
	WGroupBox* mAccountBox;
	WGroupBox* mWorkerBox;
	//WGroupBox* mShareBox;
	
	dbo::QueryModel<dbo::ptr<db::Block> >* mBlocks;
	dbo::QueryModel<ShareValue>* mShareValues;
	dbo::QueryModel<ShareCount>* mShareCount;
	dbo::QueryModel<Account>* mAccount;
	dbo::QueryModel<Worker>* mWorkers;
	//dbo::QueryModel<Share>* mShares;
	
	
};


WebServer::WebServer(const Wt::WEnvironment& env)
	:	Wt::WApplication(env)
{
	
	mSession = new dbo::Session();
	mSession->setConnectionPool(*gDBPool);
	
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
	
	dbo::Transaction transaction(*mSession);
	
	mSettings = mSession->find<db::Settings>().orderBy("id desc").limit(1);
	
	dbo::ptr<db::SiteStats> stats = mSession->find<db::SiteStats>().orderBy("id desc").limit(1);
	mTimeOff = stats->InsertTime.secsTo(WDateTime::currentDateTime());
	
	mLastUpdate = WDateTime::currentDateTime();
	
	
	setTitle(mSettings->PoolName);
	root()->setContentAlignment(AlignCenter);
	
	WContainerWidget* main = new WContainerWidget(root());
	main->resize(1200, WLength());
	
	WBoxLayout* layout = new WBoxLayout(Wt::WBoxLayout::TopToBottom);
	
	WBoxLayout* pool = new WBoxLayout(Wt::WBoxLayout::LeftToRight);
	{
		WGroupBox* box = new WGroupBox("Time");
		mPoolTime = new WText("???", box);
		pool->addWidget(box);
	}
	{
		WGroupBox* box = new WGroupBox("Workers");
		mPoolWorkers = new WText("???", box);
		pool->addWidget(box);
	}
	{
		WGroupBox* box = new WGroupBox("Latency");
		mPoolLatency = new WText("???", box);
		pool->addWidget(box);
	}
	{
		WGroupBox* box = new WGroupBox("CPD");
		mPoolCPD = new WText("???", box);
		pool->addWidget(box);
	}
	
	layout->addLayout(pool);
	
	WBoxLayout* info = new WBoxLayout(Wt::WBoxLayout::LeftToRight);
	{
		WGroupBox* box = new WGroupBox("Current Share Values");
		
		mShareValues = new dbo::QueryModel<ShareValue>();
		mShareValues->setQuery(mSession->query<ShareValue>
								("select chainlength, to_char(value/100000000., '0D99999')||' XPM' as value from sharevalue")
								.where("chainlength >= ? and id IN (select max(id) from sharevalue group by chainlength)")
								.bind(mSettings->MinShare)
								.orderBy("chainlength"));
		mShareValues->addColumn("chainlength", "Length");
		mShareValues->addColumn("value", "Value");
		
		WTableView* view = new WTableView(box);
		view->setModel(mShareValues);
		view->setAlternatingRowColors(true);
		
		info->addWidget(box);
	}
	{
		WGroupBox* box = new WGroupBox("24h Share Count");
		
		mShareCount = new dbo::QueryModel<ShareCount>();
		mShareCount->setQuery(mSession->query<ShareCount>("select chainlength, count(1) as number from share")
								.where("chainlength > 0 and inserttime > NOW() - interval '24 hour'")
								.groupBy("chainlength")
								.orderBy("chainlength"));
		mShareCount->addColumn("chainlength", "Length");
		mShareCount->addColumn("number", "Count");
		
		WTableView* view = new WTableView(box);
		view->setModel(mShareCount);
		view->setAlternatingRowColors(true);
		
		info->addWidget(box);
	}
	
	layout->addLayout(info);
	
	{
		WGroupBox* box = new WGroupBox("Your Address");
		
		mUserAddress = new WLineEdit(box);
		mUserAddress->resize(300, WLength());
		WPushButton* button = new WPushButton("Submit", box);
		button->clicked().connect(this, &WebServer::LoadAddress);
		
		layout->addWidget(box);
	}
	{
		mAccountBox = new WGroupBox("Your Account");
		
		mAccount = new dbo::QueryModel<Account>();
		mAccount->setQuery(mSession->query<Account>("select '?' as balance, '?' as requested, '?' as paid"));
		mAccount->addColumn("balance", "Balance");
		mAccount->addColumn("requested", "Requested");
		mAccount->addColumn("paid", "Paid");
		
		WTableView* view = new WTableView(mAccountBox);
		view->setModel(mAccount);
		view->setAlternatingRowColors(true);
		
		layout->addWidget(mAccountBox);
	}
	{
		mWorkerBox = new WGroupBox("Your Workers");
		
		mWorkers = new dbo::QueryModel<Worker>();
		mWorkers->setQuery(mSession->query<Worker>
					("select '?' as name, -1 as gpus, now() as inserttime, "
						"-1 as height, '?' as latency, '?' as temp, -1 as errors, '?' as cpd"));
		mWorkers->addColumn("name", "Name");
		mWorkers->addColumn("gpus", "GPUs");
		mWorkers->addColumn("inserttime", "Last Update");
		mWorkers->addColumn("height", "Height");
		mWorkers->addColumn("latency", "Latency");
		mWorkers->addColumn("temp", "Temp");
		mWorkers->addColumn("errors", "Errors");
		mWorkers->addColumn("cpd", "CPD");
		
		WTableView* view = new WTableView(mWorkerBox);
		view->setModel(mWorkers);
		view->setColumnResizeEnabled(true);
		view->setColumnWidth(0, 250);
		view->setColumnWidth(1, 80);
		view->setColumnWidth(4, 100);
		view->setColumnWidth(5, 100);
		view->setColumnWidth(6, 100);
		view->setColumnWidth(7, 150);
		view->setAlternatingRowColors(true);
		
		layout->addWidget(mWorkerBox);
	}
	/*{
		mShareBox = new WGroupBox("Your Last Shares");
		mShares = 0;
		layout->addWidget(mShareBox);
	}*/
	
	{
		WGroupBox* box = new WGroupBox("Last Blocks found by Pool");
		
		mBlocks = new dbo::QueryModel<dbo::ptr<db::Block> >();
		mBlocks->setQuery(mSession->find<db::Block>().orderBy("id desc").limit(20));
		mBlocks->addColumn("height", "Height");
		mBlocks->addColumn("hash", "Hash");
		mBlocks->addColumn("inserttime", "Time");
		mBlocks->addColumn("confirmations", "Confirmations");
		mBlocks->addColumn("value", "Value");
		
		WTableView* view = new WTableView(box);
		view->setModel(mBlocks);
		view->setColumnWidth(1, 500);
		view->setAlternatingRowColors(true);
		
		layout->addWidget(box);
	}
	
	
	
	main->setLayout(layout);
	
	Update();
	
	Wt::WTimer* timer = new Wt::WTimer(this);
	timer->setInterval(59*1000);
	timer->timeout().connect(this, &WebServer::Update);
	timer->start();
	
	transaction.commit();
	
}


WebServer::~WebServer() {
	
	if(mSession)
		delete mSession;
	
}


void WebServer::Update() {
	
	dbo::Transaction transaction(*mSession);
	
	{
		dbo::ptr<db::SiteStats> stats = mSession->find<db::SiteStats>().orderBy("id desc").limit(1);
		mPoolTime->setText(stats->InsertTime.toString());
		mPoolWorkers->setText(db::format::intToStr(stats->Workers));
		mPoolLatency->setText(db::format::intToStr(stats->Latency).append(" ms"));
		mPoolCPD->setText(db::format::floatToStr(stats->CPD).append(" chains/day"));
	}
	
	mShareValues->reload();
	mShareCount->reload();
	mBlocks->reload();
	mAccount->reload();
	mWorkers->reload();
	
	transaction.commit();
	
}


void WebServer::LoadAddress() {
	
	mAddr = mUserAddress->text().toUTF8();
	
	mAccount->setQuery(mSession->query<Account>
						("select to_char(balance/100000000., '9999990D99999')||' XPM' as balance, "
								"to_char(requested/100000000., '9999990')||' XPM' as requested, "
								"to_char(paid/100000000., '9999990')||' XPM' as paid "
								"from address")
						.where("addr = ?").bind(mAddr),
						true);
	
	mWorkers->setQuery(mSession->query<Worker>
						("select name, gpus, inserttime, height, latency||' ms' as latency, temp||' C' as temp, errors, "
							"to_char(cpd, '999990D99')||' chains/day' as cpd "
							"from clientstats")
						.where("inserttime > now() - interval '3 minute' "
								"and id IN (select max(id) from clientstats where addr = ? group by name)")
						.bind(mAddr)
						.orderBy("name"),
						true);
	
}





Wt::WApplication* createApplication(const Wt::WEnvironment& env)
{
    return new WebServer(env);
}


int main(int argc, char **argv)
{
	
	std::string dbconn;
	{
		std::ostringstream ss;
		ss << "host=" << "localhost";
		ss << " port=" << 5432;
		ss << " dbname=" << "xpmpool";
		ss << " user=" << "webserver";
		ss << " password=" << "XYZPASS";
		dbconn = ss.str();
	}
	
	dbo::backend::Postgres* backend = new dbo::backend::Postgres(dbconn);
	if(!backend){
		printf("new dbo::backend::Postgres(dbconn) FAILED\n");
		exit(0);
	}
	
	gDBPool = new dbo::FixedSqlConnectionPool(backend, 8);
	if(!gDBPool){
		printf("new FixedSqlConnectionPool FAILED\n");
		exit(0);
	}
	
	return Wt::WRun(argc, argv, &createApplication);
	
	delete gDBPool;
	
}














