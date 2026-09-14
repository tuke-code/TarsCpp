

#include "StatImp.h"
#include "FrameworkServer.h"
#include "StatData.h"

///////////////////////////////////////////////////////////
//
int StatImp::reportMicMsg( const map<tars::StatMicMsgHead, tars::StatMicMsgBody>& statmsg,bool bFromClient, tars::CurrentPtr current )
{
//	LOG_CONSOLE_DEBUG << "report---------------------------------access size:" << statmsg.size() << "|bFromClient:" <<bFromClient << endl;

    if(bFromClient)
	{
		appendClientStatData(statmsg);
	}
    else
	{
		appendServerStatData(statmsg);
	}

    return 0;
}

int StatImp::reportSampleMsg(const vector<StatSampleMsg> &msg,tars::CurrentPtr current )
{
    TLOGINFO("sample---------------------------------access size:" << msg.size() << endl);

    return 0;
}
