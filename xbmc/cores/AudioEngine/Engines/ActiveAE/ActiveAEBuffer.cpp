/*
 *      Copyright (C) 2010-2013 Team XBMC
 *      http://www.xbmc.org
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

#include "ActiveAEBuffer.h"
#include "AEFactory.h"
#include "ActiveAE.h"

using namespace ActiveAE;

/* typecast AE to CActiveAE */
#define AE (*((CActiveAE*)CAEFactory::GetEngine()))

CSoundPacket::CSoundPacket(SampleConfig conf, int samples) : config(conf)
{
  data = AE.AllocSoundSample(config, samples, bytes_per_sample, planes, linesize);
  max_nb_samples = samples;
  nb_samples = 0;
}

CSoundPacket::~CSoundPacket()
{
  if (data)
    AE.FreeSoundSample(data);
}

CSampleBuffer::CSampleBuffer() : pkt(NULL), pool(NULL)
{

}

CSampleBuffer::~CSampleBuffer()
{
  if (pkt)
    delete pkt;
}

void CSampleBuffer::Return()
{
  if (pool)
    pool->ReturnBuffer(this);
}

CActiveAEBufferPool::CActiveAEBufferPool(AEAudioFormat format)
{
  m_format = format;
  if (AE_IS_RAW(m_format.m_dataFormat))
    m_format.m_dataFormat = AE_FMT_S16NE;
}

CActiveAEBufferPool::~CActiveAEBufferPool()
{
  CSampleBuffer *buffer;
  while(!m_allSamples.empty())
  {
    buffer = m_allSamples.front();
    m_allSamples.pop_front();
    delete buffer;
  }
}

void CActiveAEBufferPool::ReturnBuffer(CSampleBuffer *buffer)
{
  buffer->pkt->nb_samples = 0;
  m_freeSamples.push_back(buffer);
}

bool CActiveAEBufferPool::Create()
{
  CSampleBuffer *buffer;
  SampleConfig config;
  config.fmt = CActiveAEResample::GetAVSampleFormat(m_format.m_dataFormat);
  config.channels = m_format.m_channelLayout.Count();
  config.sample_rate = m_format.m_sampleRate;
  config.channel_layout = CActiveAEResample::GetAVChannelLayout(m_format.m_channelLayout);

  for(int i=0; i<5; i++)
  {
    buffer = new CSampleBuffer();
    buffer->pool = this;
    buffer->pkt = new CSoundPacket(config, m_format.m_frames);

    m_allSamples.push_back(buffer);
    m_freeSamples.push_back(buffer);
  }

  return true;
}

//-----------------------------------------------------------------------------

CActiveAEBufferPoolResample::CActiveAEBufferPoolResample(AEAudioFormat inputFormat, AEAudioFormat outputFormat)
  : CActiveAEBufferPool(outputFormat)
{
  m_inputFormat = inputFormat;
  if (AE_IS_RAW(m_inputFormat.m_dataFormat))
    m_inputFormat.m_dataFormat = AE_FMT_S16NE;
  m_resampler = NULL;
  m_fillPackets = false;
  m_drain = false;
  m_procSample = NULL;
}

CActiveAEBufferPoolResample::~CActiveAEBufferPoolResample()
{
  if (m_resampler)
    delete m_resampler;
}

bool CActiveAEBufferPoolResample::Create()
{
  CActiveAEBufferPool::Create();

  if (m_inputFormat.m_channelLayout != m_format.m_channelLayout ||
      m_inputFormat.m_sampleRate != m_format.m_sampleRate ||
      m_inputFormat.m_dataFormat != m_format.m_dataFormat)
  {
    m_resampler = new CActiveAEResample();
    m_resampler->Init(CActiveAEResample::GetAVChannelLayout(m_format.m_channelLayout),
                                m_format.m_channelLayout.Count(),
                                m_format.m_sampleRate,
                                CActiveAEResample::GetAVSampleFormat(m_format.m_dataFormat),
                                CActiveAEResample::GetAVChannelLayout(m_inputFormat.m_channelLayout),
                                m_inputFormat.m_channelLayout.Count(),
                                m_inputFormat.m_sampleRate,
                                CActiveAEResample::GetAVSampleFormat(m_inputFormat.m_dataFormat));
  }
  return true;
}

bool CActiveAEBufferPoolResample::ResampleBuffers()
{
  bool busy = false;
  CSampleBuffer *in;
  if (!m_resampler)
  {
    while(!m_inputSamples.empty())
    {
      in = m_inputSamples.front();
      m_inputSamples.pop_front();
      m_outputSamples.push_back(in);
      busy = true;
    }
  }
  else if (m_procSample || !m_freeSamples.empty())
  {
    if (!m_procSample)
    {
      m_procSample = m_freeSamples.front();
      m_freeSamples.pop_front();
    }

    int out_samples = m_resampler->GetBufferedSamples();
    bool skipInput = false;
    if (out_samples > (m_procSample->pkt->max_nb_samples - m_procSample->pkt->nb_samples) * 2)
      skipInput = true;

    bool hasInput = !m_inputSamples.empty();

    if (hasInput || skipInput || m_drain)
    {
      if (hasInput && !skipInput)
      {
        in = m_inputSamples.front();
        m_inputSamples.pop_front();
      }
      else
        in = NULL;

      int start = m_procSample->pkt->nb_samples *
                  m_procSample->pkt->bytes_per_sample *
                  m_procSample->pkt->config.channels /
                  m_procSample->pkt->planes;

      for(int i=0; i<m_procSample->pkt->planes; i++)
      {
        m_planes[i] = m_procSample->pkt->data[i] + start;
      }

      out_samples = m_resampler->Resample(m_planes,
                                          m_procSample->pkt->max_nb_samples - m_procSample->pkt->nb_samples,
                                          in ? in->pkt->data : NULL,
                                          in ? in->pkt->nb_samples : 0);
      m_procSample->pkt->nb_samples += out_samples;
      busy = true;

      // some methods like encode require completely filled packets
      if (!m_fillPackets || (m_procSample->pkt->nb_samples == m_procSample->pkt->max_nb_samples))
      {
        m_outputSamples.push_back(m_procSample);
        m_procSample = NULL;
      }
      else if (m_drain && m_resampler->GetBufferedSamples() == 0)
      {
        if (m_fillPackets)
        {
          // pad with zero
          start = m_procSample->pkt->nb_samples *
                  m_procSample->pkt->bytes_per_sample *
                  m_procSample->pkt->config.channels /
                  m_procSample->pkt->planes;
          for(int i=0; i<m_procSample->pkt->planes; i++)
          {
            memset(m_procSample->pkt->data[i]+start, 0, m_procSample->pkt->linesize-start);
          }
        }
        m_outputSamples.push_back(m_procSample);
        m_procSample = NULL;
      }

      if (in)
        in->Return();
    }
  }
  return busy;
}

float CActiveAEBufferPoolResample::GetDelay()
{
  float delay = 0;
  std::deque<CSampleBuffer*>::iterator itBuf;

  if (m_procSample)
    delay += m_procSample->pkt->nb_samples / m_procSample->pkt->config.sample_rate;

  for(itBuf=m_inputSamples.begin(); itBuf!=m_inputSamples.end(); ++itBuf)
  {
    delay += (float)(*itBuf)->pkt->nb_samples / (*itBuf)->pkt->config.sample_rate;
  }

  for(itBuf=m_outputSamples.begin(); itBuf!=m_outputSamples.end(); ++itBuf)
  {
    delay += (float)(*itBuf)->pkt->nb_samples / (*itBuf)->pkt->config.sample_rate;
  }

  if (m_resampler)
  {
    int samples = m_resampler->GetBufferedSamples();
    delay += (float)samples / m_format.m_sampleRate;
  }

  return delay;
}