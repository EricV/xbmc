#pragma once
/*
 *      Copyright (C) 2010-2013 Team XBMC
 *      http://xbmc.org
 *
 *  This Program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This Program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with XBMC; see the file COPYING.  If not, see
 *  <http://www.gnu.org/licenses/>.
 *
 */

#include "threads/Event.h"
#include "threads/Thread.h"
#include "utils/ActorProtocol.h"
#include "Interfaces/AE.h"
#include "Interfaces/AESink.h"
#include "AESinkFactory.h"
#include "ActiveAEResample.h"

namespace ActiveAE
{
using namespace Actor;

class CEngineStats;

struct SinkConfig
{
  AEAudioFormat format;
  bool passthrough;
  CEngineStats *stats;
};

class CSinkControlProtocol : public Protocol
{
public:
  CSinkControlProtocol(std::string name, CEvent* inEvent, CEvent *outEvent) : Protocol(name, inEvent, outEvent) {};
  enum OutSignal
  {
    CONFIGURE,
    DRAIN,
    TIMEOUT,
  };
  enum InSignal
  {
    ACC,
    ERROR,
    STATS,
  };
};

class CSinkDataProtocol : public Protocol
{
public:
  CSinkDataProtocol(std::string name, CEvent* inEvent, CEvent *outEvent) : Protocol(name, inEvent, outEvent) {};
  enum OutSignal
  {
    SAMPLE = 0,
  };
  enum InSignal
  {
    RETURNSAMPLE,
  };
};

class CActiveAESink : private CThread
{
public:
  CActiveAESink(CEvent *inMsgEvent);
  void EnumerateSinkList();
  void EnumerateOutputDevices(AEDeviceList &devices, bool passthrough);
  std::string GetDefaultDevice(bool passthrough);
  void Start();
  void Dispose();
  bool IsCompatible(const AEAudioFormat format, const std::string &device);
  CSinkControlProtocol m_controlPort;
  CSinkDataProtocol m_dataPort;

protected:
  void Process();
  void StateMachine(int signal, Protocol *port, Message *msg);
  void PrintSinks();
  void GetDeviceFriendlyName(std::string &device);
  void OpenSink();
  void ReturnBuffers();

  void OutputSamples(CSampleBuffer* samples);

  CEvent m_outMsgEvent;
  CEvent *m_inMsgEvent;
  int m_state;
  bool m_bStateMachineSelfTrigger;
  int m_extTimeout;
  bool m_extError;

  CSampleBuffer m_sampleOfSilence;

  std::string m_deviceFriendlyName;
  AESinkInfoList m_sinkInfoList;
  IAESink *m_sink;
  AEAudioFormat m_sinkFormat, m_requestedFormat;
  CEngineStats *m_stats;
};

}