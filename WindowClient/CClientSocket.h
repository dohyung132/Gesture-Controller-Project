#pragma once

// CClientSocket 명령 대상

class CClientSocket : public CSocket
{
public:
	CClientSocket();
	virtual ~CClientSocket();
//	virtual int Receive(void* lpBuf, int nBufLen, int nFlags = 0);
	virtual void OnClose(int nErrorCode);
	virtual void OnReceive(int nErrorCode);

	struct SensorData {
		float pitch, roll;
		int lux;
	};

	bool ParseSensorData(CString strData, SensorData& data);
	CString GetActiveWindowInfo(bool& bIsTargetActive);
	void ProcessGesture(int direction, const SensorData& data);
};

