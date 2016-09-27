#include "stdafx.h"
#include "iUart.h"

#include "DeConsole.h"

#define XON 0x11
#define XOFF 0x13

iUart::~iUart()
{
	if (isConnected())
		CloseHandle(hUartCom);
}


//���ļ���ʽ�򿪴���
bool iUart::OpenCom(bool isBlockMode){
	HANDLE hCom;

	//��10���ϵĴ���
	ComName = "\\\\.\\"+ComName;
	if (isBlockMode)
	{
		hCom = CreateFile(ComName,//COM��  
			GENERIC_READ | GENERIC_WRITE, //��������д  
			0, //��ռ��ʽ  
			NULL,
			OPEN_EXISTING, //�򿪶����Ǵ���  
			FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED,//�첽��ʽ
			NULL);

		//���÷Ƕ�������
		memset(&m_osRead, 0, sizeof(OVERLAPPED));
		m_osRead.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);

		//���÷Ƕ�������
		memset(&m_osWrite, 0, sizeof(OVERLAPPED));
		m_osWrite.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	}
	else
	{
		hCom = CreateFile(ComName,//COM��  
			GENERIC_READ | GENERIC_WRITE, //��������д  
			0, //��ռ��ʽ  
			NULL,
			OPEN_EXISTING, //�򿪶����Ǵ���  
			0, //ͬ����ʽ
			NULL);
	}


	//��ȡʧ��
	if (hCom == INVALID_HANDLE_VALUE)
	{
		TRACE("Open %s Fail\n",ComName);
		return false;
	}

	SetCommMask(hCom, EV_RXCHAR | EV_TXEMPTY);//�����¼�����������
	SetupComm(hCom, 1024, 1024); //���뻺����������������Ĵ�С����1024

	COMMTIMEOUTS TimeOuts;
	//�趨����ʱ  
	TimeOuts.ReadIntervalTimeout = MAXDWORD;
	TimeOuts.ReadTotalTimeoutMultiplier = 0;
	TimeOuts.ReadTotalTimeoutConstant = 0;
	//�ڶ�һ�����뻺���������ݺ���������������أ�  
	//�������Ƿ������Ҫ����ַ�
	//�趨д��ʱ
	TimeOuts.WriteTotalTimeoutMultiplier = 100;
	TimeOuts.WriteTotalTimeoutConstant = 500;
	SetCommTimeouts(hCom, &TimeOuts); //���ó�ʱ
	hUartCom=hCom;
	TRACE("h:%p\n",hUartCom);
	DCB dcb;
	GetCommState(hCom, &dcb);
	///��������ο�DCB���ڽṹ��
	dcb.BaudRate = uartConfig.BaudRate;
	dcb.ByteSize = uartConfig.ByteSize;
	dcb.Parity = uartConfig.Parity;
	dcb.StopBits = uartConfig.StopBits;

	TRACE("open: baud:%d,size:%d,stop:%d,ecc:%d\n", uartConfig.BaudRate,
		uartConfig.ByteSize, uartConfig.StopBits, uartConfig.Parity);
	//ȡ��������
	dcb.fRtsControl = RTS_CONTROL_DISABLE; //CTS
	dcb.fDtrControl = DTR_CONTROL_DISABLE;	//DSR,RLSD
	//��������
	dcb.fBinary = uartConfig.fBinary;
	//����0�ַ�
	dcb.fNull = false;
	//�ص�
	dcb.fInX = false;
	dcb.fOutX = false;
	//dcb.XonChar = XON; //�����ͷ���������ʱ���ַ� 0x11  
	//dcb.XoffChar = XOFF; //�����ͷ�ֹͣ����ʱ���ַ� 0x13  
	//dcb.XonLim = 50;
	//dcb.XoffLim = 50;

	if (SetCommState(hUartCom, &dcb) == 0)
	{
		TRACE("Com Config fail\n");
		ClosePort();
		return false;
	}

	//������պͷ��ͻ�����
	PurgeComm(hUartCom, PURGE_TXCLEAR | PURGE_RXCLEAR);
	return true;
}

bool iUart::ConfigUart(CString comName,DCB mConfig)
{
	ComName = comName;

	uartConfig.BaudRate = mConfig.BaudRate;
	uartConfig.ByteSize = mConfig.ByteSize;
	uartConfig.Parity = mConfig.Parity;
	uartConfig.StopBits = mConfig.StopBits;
	uartConfig.fBinary = true;
	

	return true;
}


void iUart::GetComList(CComboBox *cblist)
{
	HKEY hKey;
	int   i = 0;
	TCHAR   portName[256], commName[256];
	DWORD   dwLong, dwSize;
	// ����ע�����ˢ��
	//�򿪴���ע���  
	int rtn = ::RegOpenKeyEx(HKEY_LOCAL_MACHINE, "Hardware\\DeviceMap\\SerialComm", NULL, KEY_READ, &hKey);
	if (rtn == ERROR_SUCCESS)
	{
		while (TRUE)
		{
			dwLong = dwSize = sizeof(portName);
			memset(portName, 0, sizeof(portName));
			memset(commName, 0, sizeof(commName));

			rtn = RegEnumValue(hKey, i, portName, &dwLong, NULL, NULL, (PUCHAR)commName, &dwSize);

			if (rtn == ERROR_NO_MORE_ITEMS)   //   ö�ٴ���     
				break; 
			//�����б�
			cblist->AddString(commName);
			i++;
		}

		RegCloseKey(hKey);
	}
	//����ˢ�´���ѡ��
	cblist->AddString("ˢ�´���");
}

int iUart::WriteCString(const CString &cBuffer)
{
	unsigned long dwBytesWrite;
	BOOL bWriteStat;

	if (!isConnected())
		return false;

	bWriteStat = WriteFile(hUartCom, cBuffer, cBuffer.GetAllocLength(), &dwBytesWrite, NULL);
	//���󷵻� -1
	if (!bWriteStat)
		return -1;
	//���ط����ֽ�
	return dwBytesWrite;
}


CString iUart::ReadCString(void)
{
	unsigned long dwBytesRead;
	BOOL bReadStat;
	COMSTAT ComStat;
	DWORD dwErrorFlags;

	CString dataStr;
	dataStr.Empty();

	if (!isConnected())
	{
		TRACE("Read:no connect\n");
		return dataStr;
	}

	ClearCommError(hUartCom, &dwErrorFlags, &ComStat);
	//cbInQue�����ڴ�������������������е��ַ���
	dwBytesRead = ComStat.cbInQue;

	if (dwBytesRead == 0)
	{
		TRACE("Read:no data\n");
		return dataStr;
	}

	dataStr.GetBufferSetLength(dwBytesRead);
	bReadStat = ReadFile(hUartCom, dataStr.GetBuffer(0), dwBytesRead, &dwBytesRead, NULL);
	if (!bReadStat)
	{
		TRACE("Read:read failed\n");
		dataStr.Empty();
		return dataStr;
	}

	PurgeComm(hUartCom, PURGE_RXABORT | PURGE_RXCLEAR);
	return dataStr;
}


UINT RxThreadFunc(LPVOID mThreadPara)
{
	OVERLAPPED os;
	DWORD dwMask, dwTrans;
	COMSTAT ComStat;
	DWORD dwErrorFlags;

	ThreadPara *pPara = (ThreadPara *)mThreadPara;
	HANDLE hComm = *(pPara->commHandle);
	if (hComm == NULL){
		TRACE("Handle not correct\n");
		return -1;
	}

	memset(&os, 0, sizeof(OVERLAPPED));
	os.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);

	if (os.hEvent == NULL)
	{
		TRACE("hEvent Failed\n");
		return (UINT)-1;
	}
	TRACE("Rx Listenner Thread Start\n");

	while (pPara->stopFlag != RT_REQ_EXIT)
	{
		ClearCommError(hComm, &dwErrorFlags, &ComStat);
	
		if (ComStat.cbInQue)
		{
			pPara->stopFlag = RT_NOT_EXIT;
			///�����ݷ�����Ϣ��UI�߳�
			//������Ϣ�����д����߳��˳�������
			::SendMessage(::AfxGetMainWnd()->m_hWnd, WM_COMM_RX_MSG, 1, 0);
			//TRACE("Rec\n");
			pPara->stopFlag = RT_PRE_EXIT;
		}
		dwMask = 0;
		//�ȴ��¼�
		if (!WaitCommEvent(hComm, &dwMask, &os))
		{
			if (pPara->stopFlag == 1 && GetLastError() == ERROR_IO_PENDING)
			{
				GetOverlappedResult(hComm, &os, &dwTrans, TRUE);
			}
			else
			{
				CloseHandle(os.hEvent);
				pPara->stopFlag = RT_SUC_EXIT;
				return(UINT)-1;
			}
		}

	}
	CloseHandle(os.hEvent);
	pPara->stopFlag = RT_SUC_EXIT;
	TRACE("Rx Listenner Thread Stop\n");
	return EXIT_SUCCESS;
}


void iUart::CloseWaitThread(void)
{
	TRACE("TrigStop\n");

	for (int i = 0; i < 100000; i++)
	{
		if (mThreadPara.stopFlag == RT_PRE_EXIT)
			break;
	}
	//while (mThreadPara.stopFlag != RT_PRE_EXIT);
	mThreadPara.stopFlag = RT_REQ_EXIT;
	TRACE("Req ok\n");
	for (int i = 0; i < 20; i++)
	{
		Sleep(10);
		if (mThreadPara.stopFlag == RT_SUC_EXIT){
			TRACE("Exit ok\n");
			break;
		}
	}
	//while (mThreadPara.stopFlag == 0);
}


//����̷߳�ʽ�Ķ�ȡ
int iUart::UnblockRead(CString &dataStr)
{
	COMSTAT ComStat;
	DWORD dwErrorFlags;
	DWORD dwBytesRead;
	BOOL bReadStatus;

	ClearCommError(hUartCom, &dwErrorFlags, &ComStat);
	//cbInQue�����ڴ�������������������е��ַ���
	dwBytesRead = ComStat.cbInQue;

	dataStr.GetBufferSetLength(dwBytesRead);
	//��ȡ
	bReadStatus = ReadFile(hUartCom, dataStr.GetBuffer(0), dwBytesRead, &dwBytesRead, &m_osRead);
	if (!bReadStatus)
	{
		//����ص�����δ���,�ȴ�ֱ���������
		if (GetLastError() == ERROR_IO_PENDING)
		{
			GetOverlappedResult(hUartCom, &m_osRead, &dwBytesRead, TRUE);
			m_osRead.Offset = 0;
		}
		else
		{
			dwBytesRead = 0;
		}
	}
	//���ض�ȡ����
	return dwBytesRead;
}


int iUart::UnblockSend(const CString &dataStr)
{
	BOOL bWriteStatus;
	COMSTAT ComStat;
	DWORD dwErrorFlags, dwLength;

	ClearCommError(hUartCom, &dwErrorFlags, &ComStat);
	if (dwErrorFlags>0)
	{
		TRACE("Unblock Write Failed\n");
		PurgeComm(hUartCom, PURGE_TXABORT | PURGE_TXCLEAR);
		return 0;
	}
	m_osWrite.Offset = 0;
	dwLength = dataStr.GetAllocLength();
	bWriteStatus = WriteFile(hUartCom, dataStr, dwLength, &dwLength, &m_osWrite);

	if (!bWriteStatus)
	{
		if (GetLastError() == ERROR_IO_PENDING)
		{
			//����ص�����δ���,�ȴ�ֱ���������
			GetOverlappedResult(hUartCom, &m_osWrite, &dwLength, TRUE);
		}
		else
			dwLength = 0;
	}
	return dwLength;
}