/* 
 * filename : protocol.h
 * author   : sundayman
 * date     : 4 15, 2013
 * brief    : 协议接口，要实现新的协议，需要继承该接口，并且由协议工厂类调用 
 */

#ifndef PROTOCOL_H_
#define PROTOCOL_H_

/**
 * \brief embassy service for protocol analysis layer.
 * \	  if you want to change default action of the embassy ,
 * \		 you can inherit class Embassy and then implement your version
 *	协议对象工厂接口，实现这个接口就可以实现特定的协议工厂（产生协议对象的对象）
 */

#include "ref_counter.h"
#include "diplomat.h"
namespace Net {

class Protocol : public Utility::RefCounter {
public:
	Protocol(const DiplomatPtr &diplomat) {
		_diplomat = diplomat;
	}
	virtual ~Protocol(){}
	virtual int GarrisonDiplomat() = 0;
	virtual int WithdrawDiplomat() = 0;
	virtual int RecvSomething() = 0;
	DiplomatPtr GetDiplomat() {
		return _diplomat;
	}
protected:
	DiplomatPtr _diplomat;
};
typedef Utility::SmartPtr<Protocol> ProtocolPtr;
};
#endif /* PROTOCOL_H_ */
