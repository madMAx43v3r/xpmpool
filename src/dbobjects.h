/*
 * dbobjects.h
 *
 *  Created on: 27.04.2014
 *      Author: mad
 */

#ifndef DBOBJECTS_H_
#define DBOBJECTS_H_



#include <string>
#include <sstream>

#include <Wt/WDateTime>
#include <Wt/Dbo/Dbo>
#include <Wt/Dbo/WtSqlTraits>
namespace dbo = Wt::Dbo;

using namespace Wt;


namespace pool {
namespace db {


class format {
public:
	
	static std::string intToStr(int i) {
		std::ostringstream ss;
		ss << i;
		return ss.str();
	}
	
	static std::string uintToStr(unsigned i) {
		std::ostringstream ss;
		ss << i;
		return ss.str();
	}
	
	static std::string longToStr(long long i) {
		std::ostringstream ss;
		ss << i;
		return ss.str();
	}
	
	static std::string ulongToStr(unsigned long long i) {
		std::ostringstream ss;
		ss << i;
		return ss.str();
	}
	
	static std::string doubleToStr(double d) {
		std::ostringstream ss;
		ss << d;
		return ss.str();
	}
	
	static std::string floatToStr(float d) {
		std::ostringstream ss;
		ss << d;
		return ss.str();
	}
	
};



class Address {
public:
	
	std::string Addr;
	
	long long Balance;
	long long Requested;
	long long Paid;
	long long MinPayout;
	
	bool Blocked;
	
	
	template<class Action>
	void persist(Action& a)
	{
		dbo::field(a, Addr, "addr");
		dbo::field(a, Balance, "balance");
		dbo::field(a, Requested, "requested");
		dbo::field(a, Paid, "paid");
		dbo::field(a, MinPayout, "minpayout");
		dbo::field(a, Blocked, "blocked");
	}
	
};


class ShareValue {
public:
	
	WDateTime InsertTime;
	int ChainLength;
	long long Value;
	
	
	template<class Action>
	void persist(Action& a)
	{
		dbo::field(a, InsertTime, "inserttime");
		dbo::field(a, ChainLength, "chainlength");
		dbo::field(a, Value, "value");
	}
	
};


class PoolBalance {
public:
	
	WDateTime InsertTime;
	long long Balance;
	long long Immature;
	long long UserBalance;
	long long NetBalance;
	
	
	template<class Action>
	void persist(Action& a)
	{
		dbo::field(a, InsertTime, "inserttime");
		dbo::field(a, Balance, "balance");
		dbo::field(a, Immature, "immature");
		dbo::field(a, UserBalance, "userbalance");
		dbo::field(a, NetBalance, "netbalance");
	}
	
};


class PayoutRequest {
public:
	
	WDateTime InsertTime;
	std::string Addr;
	long long Amount;
	
	
	template<class Action>
	void persist(Action& a)
	{
		dbo::field(a, InsertTime, "inserttime");
		dbo::field(a, Addr, "addr");
		dbo::field(a, Amount, "amount");
	}
	
};


class PayoutTransaction {
public:
	
	WDateTime InsertTime;
	std::string Addr;
	long long Amount;
	
	std::string TxID;
	
	
	template<class Action>
	void persist(Action& a)
	{
		dbo::field(a, InsertTime, "inserttime");
		dbo::field(a, Addr, "addr");
		dbo::field(a, Amount, "amount");
		dbo::field(a, TxID, "txid");
	}
	
};


class Block {
public:
	
	WDateTime InsertTime;
	std::string Hash;
	int Height;
	int Confirmations;
	bool Orphan;
	long long Value;
	
	std::string Addr;
	std::string ClientID;
	
	
	template<class Action>
	void persist(Action& a)
	{
		dbo::field(a, InsertTime, "inserttime");
		dbo::field(a, Hash, "hash");
		dbo::field(a, Height, "height");
		dbo::field(a, Confirmations, "confirmations");
		dbo::field(a, Orphan, "orphan");
		dbo::field(a, Value, "value");
		dbo::field(a, Addr, "addr");
		dbo::field(a, ClientID, "clientid");
	}
	
};


class Share {
public:
	
	std::string Addr;
	std::string Name;
	std::string ClientID;
	
	WDateTime InsertTime;
	int Height;
	int ChainLength;
	bool IsBlock;
	
	long long Value;
	bool Accounted;
	
	
	template<class Action>
	void persist(Action& a)
	{
		dbo::field(a, Addr, "addr");
		dbo::field(a, Name, "name");
		dbo::field(a, ClientID, "clientid");
		dbo::field(a, InsertTime, "inserttime");
		dbo::field(a, Height, "height");
		dbo::field(a, ChainLength, "chainlength");
		dbo::field(a, IsBlock, "isblock");
		dbo::field(a, Value, "value");
		dbo::field(a, Accounted, "accounted");
	}
	
	
};


class ClientStats {
public:
	
	std::string Addr;
	std::string Name;
	std::string ClientID;
	
	WDateTime InsertTime;
	int Version;
	double CPD;
	int Latency;
	int Temp;
	int Errors;
	int GPUs;
	int Height;
	
	
	template<class Action>
	void persist(Action& a)
	{
		dbo::field(a, Addr, "addr");
		dbo::field(a, Name, "name");
		dbo::field(a, ClientID, "clientid");
		dbo::field(a, InsertTime, "inserttime");
		dbo::field(a, Version, "clientversion");
		dbo::field(a, CPD, "cpd");
		dbo::field(a, Latency, "latency");
		dbo::field(a, Temp, "temp");
		dbo::field(a, Errors, "errors");
		dbo::field(a, GPUs, "gpus");
		dbo::field(a, Height, "height");
	}
	
	
};


class ServerStats;

class ReqStats {
public:
	
	std::string ServerName;
	int Thread;
	
	WDateTime InsertTime;
	std::string ReqName;
	std::string ErrorName;
	int Count;
	
	
	template<class Action>
	void persist(Action& a)
	{
		dbo::field(a, ServerName, "servername");
		dbo::field(a, Thread, "thread");
		dbo::field(a, InsertTime, "inserttime");
		dbo::field(a, ReqName, "reqname");
		dbo::field(a, ErrorName, "errorname");
		dbo::field(a, Count, "count");
	}
	
};


class ServerStats {
public:
	
	std::string ServerName;
	int Thread;
	
	WDateTime InsertTime;
	int Workers;
	int Latency;
	double CPD;
	
	template<class Action>
	void persist(Action& a)
	{
		dbo::field(a, ServerName, "servername");
		dbo::field(a, Thread, "thread");
		dbo::field(a, InsertTime, "inserttime");
		dbo::field(a, Workers, "workers");
		dbo::field(a, Latency, "latency");
		dbo::field(a, CPD, "cpd");
	}
	
	
};


class SiteStats {
public:
	
	WDateTime InsertTime;
	int Workers;
	int Latency;
	double CPD;
	double StaleRate;
	
	
	template<class Action>
	void persist(Action& a)
	{
		dbo::field(a, InsertTime, "inserttime");
		dbo::field(a, Workers, "workers");
		dbo::field(a, Latency, "latency");
		dbo::field(a, CPD, "cpd");
		dbo::field(a, StaleRate, "stalerate");
	}
	
};


class Settings {
public:
	
	WDateTime InsertTime;
	std::string PoolName;
	int Target;
	int MinShare;
	int MaxShare;
	int NumShares;
	int Fee;
	int MinPayout;
	
	
	template<class Action>
	void persist(Action& a)
	{
		dbo::field(a, InsertTime, "inserttime");
		dbo::field(a, PoolName, "poolname");
		dbo::field(a, Target, "target");
		dbo::field(a, MinShare, "minshare");
		dbo::field(a, MaxShare, "maxshare");
		dbo::field(a, NumShares, "numshares");
		dbo::field(a, Fee, "fee");
		dbo::field(a, MinPayout, "minpayout");
	}
	
};








};	//namespace db
};	//namespace pool


#endif /* DBOBJECTS_H_ */
