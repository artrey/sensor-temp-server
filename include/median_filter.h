#ifndef _MEDIAN_FILTER_H_
#define _MEDIAN_FILTER_H_

template<typename T, uint8_t SIZE>
class MedianFilter
{
private:
  uint8_t m_pos = 0;
  uint8_t m_count = 0;
  T m_buffer[SIZE];

protected:
  T medianValue() const
  {
    T* localBuffer = new T[m_count];
    for (uint8_t i = 0; i < m_count; ++i)
    {
      localBuffer[i] = m_buffer[i];
    }

    for (uint8_t i = 0; i < m_count - 1; ++i)
    {
      bool flag = true;

      for (uint8_t j = 0; j < m_count - (i + 1); ++j)
      {
        if (localBuffer[j] > localBuffer[j + 1])
        {
          flag = false;

          T buff = localBuffer[j];
          localBuffer[j] = localBuffer[j + 1];
          localBuffer[j + 1] = buff;
        }
      }

      if (flag)
      {
        break;
      }
    }

    T val = localBuffer[m_count / 2];
    delete[] localBuffer;
    return val;
  }

public:
  T operator() (T value)
  {
    m_buffer[m_pos] = value;
    m_count = min((uint8_t)(m_count + 1), SIZE);
    if (++m_pos >= SIZE)
    {
      m_pos = 0;
    }
    return medianValue();
  }
};

#endif
