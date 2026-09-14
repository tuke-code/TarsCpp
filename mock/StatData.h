#pragma once

#include "servant/StatF.h"

using StatData = std::map<tars::StatMicMsgHead, tars::StatMicMsgBody>;
using StatDataList = std::vector<StatData>;

void appendClientStatData(const StatData &data);
void appendServerStatData(const StatData &data);
void clearClientStatData();
StatDataList getClientStatData();
