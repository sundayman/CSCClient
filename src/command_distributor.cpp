/*
 * command_distributor.cpp
 *
 *  Created on: 2012-7-11
 *      Author: sundayman66@gmail.com
 */
#include "command_distributor.h"

#include <sstream>

#include <syslog.h>

#include "../third_party/tinyxml/tinyxml.h"
#include "common/tea.h"

#include "protocol_def.h"

#include "order/echo_sender.h"

using namespace Net;
using namespace std;

const char ProtocolHeader::kProtocolStart[] = "protocol:";
const char ProtocolHeader::kProtocolVersion[] = "version:";
const char ProtocolHeader::kProtocolPubkey[] = "pub-key:";
const char ProtocolHeader::kProtocolSize[] = "size:";
const char ProtocolHeader::kProtocolCmdId[] = "cmd-id:";
const char ProtocolHeader::kProtocolSeparator[] = "\n";
const char ProtocolHeader::kProtocolSeparator2[] = "\r\n";

const int CommandDistributor::kMaxLengthOfHeader = 1024;
const int CommandDistributor::kMaxLengthOfMethod = 64 * 1024;
CommandDistributor::CmdMap CommandDistributor::s_cmds;

ProtocolHeader::ProtocolHeader() {
	strcpy(protocol, PROTOCOL_NAME);
	strcpy(version, PROTOCOL_VERSION);
	strcpy(pubkey, PROTOCOL_NOT_SET);
	strcpy(commandId, PROTOCOL_NOT_SET);
	size = 0;
}

string ProtocolHeader::BuildHeader(int size, const char *ack_id) {

	if (  !ack_id )
		return "";
	ostringstream str;
	str<<kProtocolStart<<protocol<<"\n"<<kProtocolVersion<<version<<"\n"
			<<kProtocolPubkey<<pubkey<<"\n"
			<<kProtocolSize<<size<<"\n"<<kProtocolCmdId<<ack_id<<"\n";
	return str.str();
}


int CommandDistributor::RecvSomething(const DiplomatPtr &diplomat) {

	if ( !diplomat )
		return 0;

	char *buffer = NULL;
	int length = diplomat->Peek(&buffer);
	if ( length > 0 ) {

		buffer[length] = '\0';

	} else {

		syslog(LOG_ERR,
			"net: didn't recv anything\n");
		return 0;

	}

	DiplomatMemoPtr memo = (DiplomatMemo*)(diplomat->GetMemo().get());
	int cmd_cnt = 0;
	int cursor = 0;
	for (;cursor < length;) {

		if ( !memo->_header_got ) {

			int ret = GetProtocolHeader(diplomat, buffer, cursor, length);
			if ( -1 == ret )
				break;
			else if ( 1 == ret )
				continue;
		}

		if ( memo->_header_got ) {

			ProtocolHeader &header =memo->_header;
			if ( header.size <= length - cursor ) {

				ParseCommand(diplomat, buffer + cursor, header.version);
				cursor += header.size;

				memo->_header_got = false;
				cmd_cnt ++;
				continue;

			}  else if ( header.size > kMaxLengthOfMethod ) {
				memo->_header_got = false;
				cursor = length;
				break;

			} else {

#if ENABLE_DEBUG_LOG
				syslog(LOG_INFO, "net: no recv a complete pagkage; size:%d recv:%d ",
						header.size, length - cursor);
#endif
				break;
			}

		}

	}
	diplomat->Drain(cursor);
	return cmd_cnt;

}


bool CommandDistributor::RegisterCommand(const CommandPtr &command) {

	s_cmds[command->Name()] = command;
	return true;
}


int CommandDistributor::ParseProtocolHeader(
		const char* data, DiplomatMemoPtr &memo) {

	ProtocolHeader &header = memo->_header;
	const char* tmp = GetProtocolKeyword(data, header.kProtocolCmdId,
        header.commandId, sizeof(header.commandId) - 1 );
    if ( NULL == tmp) {

        return 0;
    }

    int ret = tmp - data + 1;
    memo->_header_length = ret;

    tmp = GetProtocolKeyword(data, header.kProtocolStart,
            header.protocol, sizeof(header.protocol) -1 );
    if (  NULL == tmp ) {

        syslog(LOG_ERR, "net: protocol header do no include protocol keyword\n");
        return -1;
    }

    tmp = GetProtocolKeyword(tmp, header.kProtocolVersion,
            header.version, sizeof(header.version) -1 );
    if (  NULL == tmp ) {

        syslog(LOG_ERR, "net: protocol header do no include %s keyword: %s\n",
        		header.kProtocolVersion, data);
        return -1;
    }

    tmp = GetProtocolKeyword(tmp, header.kProtocolPubkey,
            header.pubkey, sizeof(header.pubkey) - 1 );
    if (  NULL == tmp ) {

        syslog(LOG_ERR, "net: protocol header do no include pub-key keyword\n");
        return -1;
    }

    char size[32];
    memset(size, 0, 32);
    tmp = GetProtocolKeyword(tmp, header.kProtocolSize,
            size, sizeof(size) - 1 );
    if (  NULL == tmp ) {

        syslog(LOG_ERR, "net: protocol header do no include size keyword\n");
        return -1;
    }
    header.size = atoi(size);

    const char *foo = GetProtocolKeyword(tmp, header.kProtocolSize,
            size, sizeof(size) - 1 );
    if ( foo && foo - data + 1 < ret )
    {
        syslog(LOG_ERR, "net: there are two 'size' keyword between fileGuid\n");
        return -1;
    }
    tmp = GetProtocolKeyword(tmp, header.kProtocolCmdId,
            header.commandId, sizeof(header.commandId) - 1 );
    if (  NULL == tmp ) {

        syslog(LOG_ERR, "net: protocol header do no include cmmandId keyword\n");
        return -1;
    }
    memo->_header_got = true;
    return ret;

}

int CommandDistributor::GetProtocolHeader(
		const DiplomatPtr &diplomat, char *buffer, int &cursor, int length) {

	DiplomatMemoPtr memo = (DiplomatMemo*)(diplomat->GetMemo().get());
	int ret = ParseProtocolHeader(buffer + cursor, memo);
	if ( 0 == ret ) {

		for ( int i = 0;length - cursor > kMaxLengthOfHeader && 0 == ret; i ++) {

			if ( i == 0)
				syslog(LOG_INFO,
				"net: buf is more than max haeder len but do not recv header\n");
			cursor += strlen(memo->_header.kProtocolStart); //丢弃第一个关键字长度

			ret = ParseProtocolHeader(buffer + cursor, memo);
		}

		if ( 0 == ret ) {
			return -1;
		}

	} 
	if ( -1 == ret ) {

		cursor += memo->_header_length;
		syslog(LOG_INFO, "net: recv illegal package\n");
		return 1;

	} else {

		cursor += ret;
		return 0;
	}

	syslog(LOG_ERR, "net: out of control");
	return 0;
}


const char *CommandDistributor::GetProtocolKeyword(const char *data, const char *keyword_start,
        char *keyword, int keyword_len) {

    const char* protocol_start = strstr(data, keyword_start);
    if ( NULL == protocol_start ) {
        return NULL;
    }

    const char *separator = strstr(protocol_start + strlen(keyword_start),
    		ProtocolHeader::kProtocolSeparator);
    if ( NULL == separator ) {

    	separator = strstr(protocol_start + strlen(keyword_start),
    	    		ProtocolHeader::kProtocolSeparator2);
    	if ( !separator )
    		return NULL;
    }

    int maxTmp = separator - protocol_start - strlen(keyword_start) > keyword_len - 1 ?
            keyword_len - 1: separator - protocol_start - strlen(keyword_start);
    memcpy(keyword, protocol_start + strlen(keyword_start), maxTmp);
    keyword[maxTmp] = '\0';

    return separator;
}


int CommandDistributor::ParseCommand(const DiplomatPtr &diplomat,
		char *content, char *version) {

	TiXmlDocument commands;
	commands.Parse(content);
	TiXmlElement *root_ele = commands.RootElement();
	if (root_ele == NULL) {

		syslog(LOG_ERR, "net: get root element error");
		return 0;
	}

	int ret = 0;
	//考虑到一个xml可能包括多个method
	for (TiXmlElement *method_ele = root_ele->FirstChildElement("method");
			method_ele; method_ele = method_ele->NextSiblingElement()) {

		TiXmlElement *name_ele = method_ele->FirstChildElement("name");
		if (NULL == name_ele) {
			syslog(LOG_ERR, "net: can not find method name node");
			continue;
		}

		const char* cmd_id = name_ele->Attribute("id");
		const char* name = name_ele->GetText();
		if	( !name || !cmd_id ) {
			syslog(LOG_ERR, "net: can not find method name value");
			continue;
		}

		TiXmlElement *params_ele = method_ele->FirstChildElement("params");
		if ( NULL == params_ele ) {
			syslog(LOG_ERR, "net: method:%s parameter is invalid", name);
			continue;
		}

		CmdMap::iterator cmd = s_cmds.find(name);
		if ( cmd != s_cmds.end() ) {
			cmd->second->Excute(diplomat, params_ele, version, cmd_id);

		} else {
			syslog(LOG_DEBUG, "net: method no handle :%s", name);

		}

		ret ++;
	}

	return ret;

}
class Test :  public Utility::Thread
{
public:
	Test(const DiplomatPtr &diplomat): _diplomat(diplomat){}
	virtual void run()
	{
		for ( ;; ) {
			EchoSender echo_sender;
			if ( !echo_sender.Excute(_diplomat, NULL) )
				return ;
#ifndef WIN32
			sleep(1);
#else
			Sleep(10);
#endif
		}
	}
	DiplomatPtr _diplomat;
};

int CommandDistributor::GarrisonDiplomat(const DiplomatPtr &diplomat) {

	diplomat->SetMemo(new DiplomatMemo());
	Test* test = new Test(diplomat);
	test->start();
	return 0;
}


int CommandDistributor::WithdrawDiplomat(const DiplomatPtr &diplomat) {

	diplomat->Drain(-1); //清空接收缓冲
	return 0;
}

